#include "watchy/package_runtime.h"

#include "watchy/display.h"
#include "watchy/factory_seed.h"
#include "watchy/package_host.h"
#include "watchy/package_crypto.h"
#include "watchy/storage.h"
#include "watchy/watchdog.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_dlfcn.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "private/esp_dlmod.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#define WATCHY_IDF_PATH_MAX 256u
#define WATCHY_PACKAGE_NVS_NAMESPACE "watchy_pkg"
#define WATCHY_PACKAGE_NVS_INDEX_KEY "index"
#define WATCHY_PACKAGE_NVS_FACTORY_SEED_KEY "factory_seed"
#define WATCHY_REMOVE_DEPTH_MAX (WATCHY_IDF_PATH_MAX / 2u)

typedef struct {
    watchy_package_manifest_t manifest;
    watchy_package_host_context_t host;
    watchy_package_session_t session;
    char reference[WATCHY_PACKAGE_REF_MAX + 1u];
    bool active;
    bool host_ready;
    bool attempt_started;
    bool pending;
    bool rendered;
} package_runner_t;

typedef struct {
    void *handle;
    uintptr_t text_start;
    size_t text_size;
    uintptr_t data_start;
    size_t data_size;
} package_loader_context_t;

static watchy_package_index_manager_t s_index;
static watchy_package_manifest_t s_reconcile_manifest;
static watchy_package_index_t s_empty_index;
static watchy_package_install_workspace_t s_install_workspace;
static package_runner_t s_runner;
static package_loader_context_t s_loader_context;
static SemaphoreHandle_t s_package_mutex;
static uint8_t s_index_wire[WATCHY_PACKAGE_INDEX_WIRE_MAX];
static DIR *s_remove_directories[WATCHY_REMOVE_DEPTH_MAX];
static size_t s_remove_lengths[WATCHY_REMOVE_DEPTH_MAX];
static char s_remove_path[WATCHY_IDF_PATH_MAX];
static watchy_package_runtime_init_state_t s_runtime_init_state;
static atomic_flag s_init_lock = ATOMIC_FLAG_INIT;
static atomic_flag s_upload_lock = ATOMIC_FLAG_INIT;
static atomic_flag s_factory_seed_lock = ATOMIC_FLAG_INIT;
static uint8_t s_factory_seed_catalog_wire[WATCHY_FACTORY_SEED_CATALOG_BYTES];
static watchy_factory_seed_catalog_t s_factory_seed_catalog;

typedef struct {
    int descriptor;
    size_t expected_size;
    size_t received_size;
    char path[WATCHY_IDF_PATH_MAX];
    bool active;
} package_upload_t;

static package_upload_t s_upload = {.descriptor = -1};

static watchy_index_store_result_t idf_index_load(void *context,
                                                  watchy_package_index_t *out_index) {
    nvs_handle_t handle = 0u;
    size_t size = 0u;
    esp_err_t error;
    (void)context;
    error = nvs_open(WATCHY_PACKAGE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        return WATCHY_INDEX_STORE_ERROR;
    }
    error = nvs_get_blob(handle, WATCHY_PACKAGE_NVS_INDEX_KEY, NULL, &size);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return WATCHY_INDEX_STORE_NOT_FOUND;
    }
    if (error != ESP_OK || size == 0u || size > sizeof(s_index_wire)) {
        nvs_close(handle);
        return WATCHY_INDEX_STORE_ERROR;
    }
    error = nvs_get_blob(handle, WATCHY_PACKAGE_NVS_INDEX_KEY, s_index_wire, &size);
    nvs_close(handle);
    return error == ESP_OK &&
                   watchy_package_index_decode(s_index_wire, size, out_index) == WATCHY_PACKAGE_OK
               ? WATCHY_INDEX_STORE_OK : WATCHY_INDEX_STORE_ERROR;
}

static bool idf_index_save(void *context, const watchy_package_index_t *index) {
    nvs_handle_t handle = 0u;
    size_t size = 0u;
    esp_err_t error;
    (void)context;
    if (watchy_package_index_encode(index, s_index_wire, sizeof(s_index_wire), &size) !=
        WATCHY_PACKAGE_OK) {
        return false;
    }
    error = nvs_open(WATCHY_PACKAGE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_set_blob(handle, WATCHY_PACKAGE_NVS_INDEX_KEY, s_index_wire, size);
    }
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    if (handle != 0u) {
        nvs_close(handle);
    }
    return error == ESP_OK;
}

static bool idf_sha256(void *context,
                       const watchy_byte_region_t *regions,
                       size_t region_count,
                       uint8_t out_digest[WATCHY_PACKAGE_DIGEST_SIZE]) {
    return watchy_package_sha256_regions(context, regions, region_count, out_digest);
}

static bool idf_mkdirs_path(const char *path) {
    char copy[WATCHY_IDF_PATH_MAX];
    size_t length;
    if (path == NULL || (length = strnlen(path, sizeof(copy))) == sizeof(copy)) {
        return false;
    }
    memcpy(copy, path, length + 1u);
    for (char *cursor = copy + 1; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') {
            *cursor = '\0';
            if (mkdir(copy, 0700) != 0 && errno != EEXIST) {
                return false;
            }
            *cursor = '/';
        }
    }
    return mkdir(copy, 0700) == 0 || errno == EEXIST;
}

static bool idf_mkdirs(void *context, const char *path) {
    (void)context;
    return idf_mkdirs_path(path);
}

static bool idf_mkdir_exclusive(void *context, const char *path) {
    (void)context;
    return mkdir(path, 0700) == 0;
}

static bool idf_parent_mkdirs(char path[WATCHY_IDF_PATH_MAX]) {
    char *separator = strrchr(path, '/');
    bool result;
    if (separator == NULL) {
        return false;
    }
    *separator = '\0';
    result = idf_mkdirs_path(path);
    *separator = '/';
    return result;
}

static bool write_descriptor(int descriptor, const uint8_t *bytes, size_t size) {
    size_t offset = 0u;
    while (offset < size) {
        const ssize_t written = write(descriptor, bytes + offset, size - offset);
        if (written <= 0) {
            return false;
        }
        offset += (size_t)written;
    }
    return fsync(descriptor) == 0;
}

static bool idf_write_file(void *context,
                           const char *path,
                           const uint8_t *bytes,
                           size_t size,
                           bool semantic_read_only) {
    char mutable_path[WATCHY_IDF_PATH_MAX];
    size_t length;
    int descriptor;
    bool result;
    (void)context;
    (void)semantic_read_only;
    if (path == NULL || (bytes == NULL && size != 0u) ||
        (length = strnlen(path, sizeof(mutable_path))) == sizeof(mutable_path)) {
        return false;
    }
    memcpy(mutable_path, path, length + 1u);
    if (!idf_parent_mkdirs(mutable_path)) {
        return false;
    }
    descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (descriptor < 0) {
        return false;
    }
    result = write_descriptor(descriptor, bytes, size);
    result = close(descriptor) == 0 && result;
    if (!result) {
        (void)unlink(path);
    }
    return result;
}

static bool idf_write_file_exclusive(void *context,
                                     const char *path,
                                     const uint8_t *bytes,
                                     size_t size) {
    char mutable_path[WATCHY_IDF_PATH_MAX];
    size_t length;
    int descriptor;
    bool result;
    (void)context;
    if (path == NULL || (bytes == NULL && size != 0u) ||
        (length = strnlen(path, sizeof(mutable_path))) == sizeof(mutable_path)) {
        return false;
    }
    memcpy(mutable_path, path, length + 1u);
    if (!idf_parent_mkdirs(mutable_path)) {
        return false;
    }
    descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (descriptor < 0) {
        return false;
    }
    result = write_descriptor(descriptor, bytes, size);
    result = close(descriptor) == 0 && result;
    if (!result) {
        (void)unlink(path);
    }
    return result;
}

static bool idf_read_file(void *context,
                          const char *path,
                          uint8_t *bytes,
                          size_t capacity,
                          size_t *out_size) {
    struct stat info;
    FILE *file;
    size_t size;
    (void)context;
    if (path == NULL || bytes == NULL || out_size == NULL || stat(path, &info) != 0 ||
        !S_ISREG(info.st_mode) || S_ISLNK(info.st_mode) || info.st_size < 0 ||
        (uintmax_t)info.st_size > capacity) {
        return false;
    }
    size = (size_t)info.st_size;
    file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    if ((size != 0u && fread(bytes, 1u, size, file) != size) || fclose(file) != 0) {
        return false;
    }
    *out_size = size;
    return true;
}

static bool idf_sync_tree(void *context, const char *path) {
    struct stat info;
    (void)context;
    if (stat(path, &info) != 0 || S_ISLNK(info.st_mode)) {
        return false;
    }
    if (S_ISREG(info.st_mode)) {
        const int descriptor = open(path, O_RDONLY);
        bool result;
        if (descriptor < 0) {
            return false;
        }
        result = fsync(descriptor) == 0;
        return close(descriptor) == 0 && result;
    }
    /* idf_write_file fsyncs and closes every child before this point.
     * LittleFS exposes no directory-fsync operation, so for a directory the
     * durable boundary is those file syncs followed by its atomic rename. */
    return S_ISDIR(info.st_mode);
}

static bool idf_path_exists(void *context, const char *path) {
    struct stat info;
    (void)context;
    return stat(path, &info) == 0;
}

static bool idf_rename_noreplace(void *context, const char *source, const char *destination) {
    (void)context;
    if (idf_path_exists(NULL, destination)) {
        return false;
    }
    return watchy_storage_atomic_rename(source, destination) == WATCHY_STATUS_OK;
}

static void close_remove_directories(size_t depth) {
    for (size_t index = 0u; index <= depth; ++index) {
        if (s_remove_directories[index] != NULL) {
            (void)closedir(s_remove_directories[index]);
            s_remove_directories[index] = NULL;
        }
    }
}

static bool idf_remove_tree(void *context, const char *path) {
    struct stat info;
    size_t depth = 0u;
    size_t length;
    (void)context;
    if (path == NULL || (length = strnlen(path, sizeof(s_remove_path))) ==
                            sizeof(s_remove_path)) {
        return false;
    }
    /* Package storage is mounted on LittleFS, which cannot represent links;
     * deletion therefore cannot follow a link by construction. The explicit
     * type checks also fail closed if that backend guarantee ever changes. */
    if (stat(path, &info) != 0) {
        return errno == ENOENT;
    }
    if (S_ISLNK(info.st_mode) || S_ISREG(info.st_mode)) {
        return unlink(path) == 0;
    }
    if (!S_ISDIR(info.st_mode)) {
        return false;
    }
    memset(s_remove_directories, 0, sizeof(s_remove_directories));
    memcpy(s_remove_path, path, length + 1u);
    s_remove_lengths[0] = length;
    s_remove_directories[0] = opendir(s_remove_path);
    if (s_remove_directories[0] == NULL) {
        return false;
    }
    for (;;) {
        struct dirent *entry = readdir(s_remove_directories[depth]);
        if (entry == NULL) {
            if (closedir(s_remove_directories[depth]) != 0) {
                s_remove_directories[depth] = NULL;
                close_remove_directories(depth);
                return false;
            }
            s_remove_directories[depth] = NULL;
            if (rmdir(s_remove_path) != 0) {
                close_remove_directories(depth);
                return false;
            }
            if (depth == 0u) {
                return true;
            }
            --depth;
            s_remove_path[s_remove_lengths[depth]] = '\0';
            continue;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        length = s_remove_lengths[depth];
        const int written = snprintf(s_remove_path + length,
                                     sizeof(s_remove_path) - length,
                                     "/%s", entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(s_remove_path) - length ||
            stat(s_remove_path, &info) != 0) {
            s_remove_path[length] = '\0';
            close_remove_directories(depth);
            return false;
        }
        if (S_ISLNK(info.st_mode) || S_ISREG(info.st_mode)) {
            const bool removed = unlink(s_remove_path) == 0;
            s_remove_path[length] = '\0';
            if (!removed) {
                close_remove_directories(depth);
                return false;
            }
            continue;
        }
        if (!S_ISDIR(info.st_mode) || depth + 1u >= WATCHY_REMOVE_DEPTH_MAX) {
            s_remove_path[length] = '\0';
            close_remove_directories(depth);
            return false;
        }
        ++depth;
        s_remove_lengths[depth] = length + (size_t)written;
        s_remove_directories[depth] = opendir(s_remove_path);
        if (s_remove_directories[depth] == NULL) {
            s_remove_path[length] = '\0';
            close_remove_directories(depth);
            return false;
        }
    }
}

static uint32_t idf_unique_id(void *context) {
    (void)context;
    return esp_random();
}

static bool target_range(const void *address, size_t size, int kind) {
    const uintptr_t start = (uintptr_t)address;
    uintptr_t cursor;
    uintptr_t end;
    if (address == NULL || size == 0u || start > UINTPTR_MAX - (size - 1u)) {
        return false;
    }
    end = start + size - 1u;
    for (cursor = start;; ++cursor) {
        const void *pointer = (const void *)cursor;
        const bool valid = kind == 2 ? esp_ptr_in_iram(pointer)
                           : kind == 1 ? esp_ptr_in_dram(pointer)
                                       : (esp_ptr_in_dram(pointer) || esp_ptr_in_drom(pointer));
        if (!valid) {
            return false;
        }
        if (cursor == end) {
            break;
        }
    }
    return true;
}

static bool idf_readable(void *context, const void *address, size_t size) {
    const package_loader_context_t *loader = (const package_loader_context_t *)context;
    return loader != NULL && loader->handle != NULL && target_range(address, size, 0) &&
           (watchy_package_range_within(loader->text_start, loader->text_size, address, size) ||
            watchy_package_range_within(loader->data_start, loader->data_size, address, size));
}

static bool idf_writable(void *context, void *address, size_t size) {
    (void)context;
    return target_range(address, size, 1);
}

static bool idf_executable(void *context, const void *address, size_t size) {
    const package_loader_context_t *loader = (const package_loader_context_t *)context;
    return loader != NULL && loader->handle != NULL && target_range(address, size, 2) &&
           watchy_package_range_within(loader->text_start, loader->text_size, address, size);
}

static void *idf_dlopen(void *context, const char *path, int mode) {
    package_loader_context_t *loader = (package_loader_context_t *)context;
    struct dlmod_slist_t *module;
    esp_elf_t *elf;
    uintptr_t data_cursor;
    size_t data_size = 0u;
    void *handle;
    if (loader == NULL) {
        return NULL;
    }
    memset(loader, 0, sizeof(*loader));
    handle = dlopen(path, mode);
    module = (struct dlmod_slist_t *)handle;
    if (handle == NULL || !dlmod_validate_handle(module) || module->elf == NULL) {
        if (handle != NULL) {
            (void)dlclose(handle);
        }
        return NULL;
    }
    elf = module->elf;
    if (elf->sec[ELF_SEC_DATA].size > SIZE_MAX - data_size ||
        (data_size += elf->sec[ELF_SEC_DATA].size,
         elf->sec[ELF_SEC_RODATA].size > SIZE_MAX - data_size) ||
        (data_size += elf->sec[ELF_SEC_RODATA].size,
         elf->sec[ELF_SEC_DRLRO].size > SIZE_MAX - data_size) ||
        (data_size += elf->sec[ELF_SEC_DRLRO].size,
         elf->sec[ELF_SEC_BSS].size > SIZE_MAX - data_size)) {
        (void)dlclose(handle);
        return NULL;
    }
    data_size += elf->sec[ELF_SEC_BSS].size;
    data_cursor = (uintptr_t)elf->pdata;
    if (elf->ptext == NULL || elf->sec[ELF_SEC_TEXT].addr != (uintptr_t)elf->ptext ||
        !target_range(elf->ptext, elf->sec[ELF_SEC_TEXT].size, 2) ||
        (data_size != 0u &&
         (elf->pdata == NULL || !target_range(elf->pdata, data_size, 1)))) {
        (void)dlclose(handle);
        return NULL;
    }
    const unsigned data_sections[] = {
        ELF_SEC_DATA, ELF_SEC_RODATA, ELF_SEC_DRLRO, ELF_SEC_BSS,
    };
    for (size_t index = 0u; index < sizeof(data_sections) / sizeof(data_sections[0]); ++index) {
        const esp_elf_sec_t *section = &elf->sec[data_sections[index]];
        if (section->size != 0u && section->addr != data_cursor) {
            (void)dlclose(handle);
            return NULL;
        }
        data_cursor += section->size;
    }
    loader->handle = handle;
    loader->text_start = (uintptr_t)elf->ptext;
    loader->text_size = elf->sec[ELF_SEC_TEXT].size;
    loader->data_start = (uintptr_t)elf->pdata;
    loader->data_size = data_size;
    return handle;
}

static void *idf_dlsym(void *context, void *handle, const char *name) {
    (void)context;
    return dlsym(handle, name);
}

static int idf_dlclose(void *context, void *handle) {
    package_loader_context_t *loader = (package_loader_context_t *)context;
    const int result = dlclose(handle);
    if (result == 0 && loader != NULL && loader->handle == handle) {
        memset(loader, 0, sizeof(*loader));
    }
    return result;
}

static watchy_watchdog_membership_t task_watchdog_status(void *context) {
    const esp_err_t status = esp_task_wdt_status(NULL);
    (void)context;
    if (status == ESP_OK) {
        return WATCHY_WATCHDOG_ENROLLED;
    }
    return status == ESP_ERR_NOT_FOUND ? WATCHY_WATCHDOG_NOT_ENROLLED
                                       : WATCHY_WATCHDOG_MEMBERSHIP_ERROR;
}

static bool task_watchdog_enroll(void *context) {
    (void)context;
    return esp_task_wdt_add(NULL) == ESP_OK;
}

static bool task_watchdog_feed(void *context) {
    (void)context;
    return esp_task_wdt_reset() == ESP_OK;
}

static bool task_watchdog_unenroll(void *context) {
    (void)context;
    return esp_task_wdt_delete(NULL) == ESP_OK;
}

static const watchy_watchdog_ops_t s_task_watchdog_ops = {
    .status = task_watchdog_status,
    .enroll = task_watchdog_enroll,
    .feed = task_watchdog_feed,
    .unenroll = task_watchdog_unenroll,
};

static bool watchdog_ensure_current(void *context) {
    (void)context;
    return task_watchdog_status(NULL) == WATCHY_WATCHDOG_ENROLLED;
}

static void watchdog_before_callback(void *context) {
    watchy_package_host_context_t *host = (watchy_package_host_context_t *)context;
    watchy_package_callback_budget_begin(&host->callback_budget,
                                          (uint32_t)(esp_timer_get_time() / 1000));
    (void)esp_task_wdt_reset();
}

static bool watchdog_after_callback(void *context) {
    watchy_package_host_context_t *host = (watchy_package_host_context_t *)context;
    const bool within_budget = watchy_package_callback_budget_may_feed(
        &host->callback_budget, (uint32_t)(esp_timer_get_time() / 1000));
    if (within_budget) {
        (void)esp_task_wdt_reset();
    }
    watchy_package_callback_budget_end(&host->callback_budget);
    return within_budget;
}

static watchy_package_status_t runner_release_scope(watchy_watchdog_scope_t *scope,
                                                    watchy_package_status_t status) {
    const watchy_status_t watchdog_status = watchy_watchdog_scope_end(scope);
    const BaseType_t semaphore_status = xSemaphoreGive(s_package_mutex);
    if (watchdog_status != WATCHY_STATUS_OK || semaphore_status != pdTRUE) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    return status;
}

static bool split_package_ref(const char *package_ref,
                              char id[WATCHY_PACKAGE_ID_MAX + 1u],
                              char version[WATCHY_PACKAGE_VERSION_MAX + 1u]) {
    const char *separator = strchr(package_ref, '@');
    size_t id_size;
    size_t version_size;
    if (separator == NULL) {
        return false;
    }
    id_size = (size_t)(separator - package_ref);
    version_size = strlen(separator + 1u);
    if (id_size == 0u || id_size > WATCHY_PACKAGE_ID_MAX || version_size == 0u ||
        version_size > WATCHY_PACKAGE_VERSION_MAX) {
        return false;
    }
    memcpy(id, package_ref, id_size);
    id[id_size] = '\0';
    memcpy(version, separator + 1u, version_size + 1u);
    return true;
}

static watchy_package_status_t read_manifest_status(
    const char *path,
    watchy_package_manifest_t *out_manifest) {
    uint8_t *bytes;
    struct stat info;
    FILE *file;
    watchy_package_status_t status;
    bool read_ok;
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode) || S_ISLNK(info.st_mode) ||
        info.st_size <= 0 || info.st_size > (off_t)WATCHY_PACKAGE_MANIFEST_BYTES_MAX) {
        return WATCHY_PACKAGE_ERR_STORE;
    }
    bytes = malloc((size_t)info.st_size);
    file = bytes != NULL ? fopen(path, "rb") : NULL;
    if (file == NULL) {
        free(bytes);
        return WATCHY_PACKAGE_ERR_STORE;
    }
    read_ok = fread(bytes, 1u, (size_t)info.st_size, file) == (size_t)info.st_size;
    if (fclose(file) != 0 || !read_ok) {
        free(bytes);
        return WATCHY_PACKAGE_ERR_STORE;
    }
    status = watchy_package_manifest_parse(bytes, (size_t)info.st_size, out_manifest);
    free(bytes);
    return status;
}

static bool read_manifest(const char *path, watchy_package_manifest_t *out_manifest) {
    return read_manifest_status(path, out_manifest) == WATCHY_PACKAGE_OK;
}

static watchy_package_status_t read_installed_manifest(
    void *context,
    const char *package_ref,
    watchy_package_manifest_t *out_manifest) {
    char identifier[WATCHY_PACKAGE_ID_MAX + 1u];
    char version[WATCHY_PACKAGE_VERSION_MAX + 1u];
    char path[WATCHY_IDF_PATH_MAX];
    (void)context;
    if (package_ref == NULL || out_manifest == NULL ||
        !split_package_ref(package_ref, identifier, version) ||
        snprintf(path, sizeof(path), "/data/packages/%s/%s/manifest.json",
                 identifier, version) >= (int)sizeof(path)) {
        return WATCHY_PACKAGE_ERR_MANIFEST;
    }
    return read_manifest_status(path, out_manifest);
}

static bool validate_installed_elf(const char *path, uint32_t declared_runtime_bytes) {
    uint8_t *bytes;
    struct stat info;
    FILE *file;
    watchy_package_status_t status;
    uint32_t runtime_bytes = 0u;
    bool read_ok;
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode) || S_ISLNK(info.st_mode) ||
        info.st_size <= 0 || info.st_size > (off_t)WATCHY_PACKAGE_ELF_BYTES_MAX) {
        return false;
    }
    bytes = malloc((size_t)info.st_size);
    file = bytes != NULL ? fopen(path, "rb") : NULL;
    if (file == NULL) {
        free(bytes);
        return false;
    }
    read_ok = fread(bytes, 1u, (size_t)info.st_size, file) == (size_t)info.st_size;
    if (fclose(file) != 0 || !read_ok) {
        free(bytes);
        return false;
    }
    status = watchy_package_elf_validate(bytes, (size_t)info.st_size,
                                         declared_runtime_bytes, &runtime_bytes);
    free(bytes);
    return status == WATCHY_PACKAGE_OK &&
           runtime_bytes <= WATCHY_PACKAGE_RUNTIME_BYTES_MAX;
}

static bool package_index_contains_ref(const char *reference) {
    return watchy_package_is_installed(&s_index, reference, NULL);
}

static bool installed_package_valid(const char *reference, watchy_package_type_t type) {
    char identifier[WATCHY_PACKAGE_ID_MAX + 1u];
    char version[WATCHY_PACKAGE_VERSION_MAX + 1u];
    char root[WATCHY_IDF_PATH_MAX];
    char path[WATCHY_IDF_PATH_MAX];
    struct stat info;
    watchy_package_manifest_t *manifest = &s_reconcile_manifest;
    memset(manifest, 0, sizeof(*manifest));
    if (!split_package_ref(reference, identifier, version) ||
        snprintf(root, sizeof(root), "/data/packages/%s/%s", identifier, version) >=
            (int)sizeof(root) ||
        stat(root, &info) != 0 || !S_ISDIR(info.st_mode) || S_ISLNK(info.st_mode) ||
        snprintf(path, sizeof(path), "%s/manifest.json", root) >= (int)sizeof(path) ||
        !read_manifest(path, manifest) || strcmp(manifest->id, identifier) != 0 ||
        strcmp(manifest->version, version) != 0 || manifest->type != type ||
        snprintf(path, sizeof(path), "%s/package.so", root) >= (int)sizeof(path) ||
        !validate_installed_elf(path, manifest->max_runtime_bytes)) {
        return false;
    }
    for (size_t asset = 0u; asset < manifest->asset_count; ++asset) {
        const watchy_package_asset_t *entry = &manifest->assets[asset];
        if (snprintf(path, sizeof(path), "%s/%s", root, entry->path) >= (int)sizeof(path) ||
            stat(path, &info) != 0 || !S_ISREG(info.st_mode) || S_ISLNK(info.st_mode) ||
            info.st_size < 0 || (uintmax_t)info.st_size != entry->size) {
            return false;
        }
    }
    return true;
}

static watchy_package_status_t reconcile_indexed_packages(void) {
    size_t record = 0u;
    while (record < watchy_package_index_snapshot(&s_index)->installed_count) {
        const watchy_package_index_t *index = watchy_package_index_snapshot(&s_index);
        char reference[WATCHY_PACKAGE_REF_MAX + 1u];
        char identifier[WATCHY_PACKAGE_ID_MAX + 1u];
        char version[WATCHY_PACKAGE_VERSION_MAX + 1u];
        char package_path[WATCHY_IDF_PATH_MAX];
        char state_path[WATCHY_IDF_PATH_MAX];
        watchy_package_status_t status;
        memcpy(reference, index->installed[record], sizeof(reference));
        if (installed_package_valid(reference, index->installed_types[record])) {
            ++record;
            continue;
        }
        if (!split_package_ref(reference, identifier, version) ||
            snprintf(package_path, sizeof(package_path), "/data/packages/%s/%s",
                     identifier, version) >= (int)sizeof(package_path) ||
            snprintf(state_path, sizeof(state_path), "/data/state/%s", identifier) >=
                (int)sizeof(state_path)) {
            return WATCHY_PACKAGE_ERR_STATE;
        }
        status = watchy_package_unregister(&s_index, reference);
        if (status != WATCHY_PACKAGE_OK) {
            return status;
        }
        const bool last_version = !watchy_package_index_has_id(&s_index, identifier);
        if (!idf_remove_tree(NULL, package_path) ||
            (last_version && !idf_remove_tree(NULL, state_path))) {
            return WATCHY_PACKAGE_ERR_FILESYSTEM;
        }
    }
    return WATCHY_PACKAGE_OK;
}

static bool reconcile_directory(const char *root, bool staging) {
    bool result = true;
    DIR *first = opendir(root);
    if (first == NULL) {
        return errno == ENOENT;
    }
    for (;;) {
        struct dirent *entry;
        char first_path[WATCHY_IDF_PATH_MAX];
        errno = 0;
        entry = readdir(first);
        if (entry == NULL) {
            result = errno == 0 && result;
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (snprintf(first_path, sizeof(first_path), "%s/%s", root, entry->d_name) >=
            (int)sizeof(first_path)) {
            result = false;
            continue;
        }
        if (staging) {
            result = idf_remove_tree(NULL, first_path) && result;
            continue;
        }
        if (!watchy_package_id_valid(entry->d_name)) {
            result = idf_remove_tree(NULL, first_path) && result;
            continue;
        }
        DIR *versions = opendir(first_path);
        struct dirent *version;
        if (versions == NULL) {
            result = idf_remove_tree(NULL, first_path) && result;
            continue;
        }
        for (;;) {
            char version_path[WATCHY_IDF_PATH_MAX];
            char reference[WATCHY_PACKAGE_REF_MAX + 1u];
            int reference_size;
            errno = 0;
            version = readdir(versions);
            if (version == NULL) {
                result = errno == 0 && result;
                break;
            }
            if (strcmp(version->d_name, ".") == 0 || strcmp(version->d_name, "..") == 0) {
                continue;
            }
            if (snprintf(version_path, sizeof(version_path), "%s/%s", first_path,
                         version->d_name) >= (int)sizeof(version_path)) {
                result = false;
                continue;
            }
            reference_size = snprintf(reference, sizeof(reference), "%s@%s",
                                      entry->d_name, version->d_name);
            const bool reference_valid = reference_size >= 0 &&
                                         reference_size < (int)sizeof(reference);
            const bool indexed = reference_valid && package_index_contains_ref(reference);
            if (watchy_package_reconcile_version(version->d_name, indexed) !=
                WATCHY_PACKAGE_RECONCILE_KEEP) {
                result = idf_remove_tree(NULL, version_path) && result;
            }
        }
        result = closedir(versions) == 0 && result;
    }
    return closedir(first) == 0 && result;
}

static bool scrub_state_directory_temporaries(const char *root) {
    bool result = true;
    DIR *directory = opendir(root);
    if (directory == NULL) {
        return false;
    }
    for (;;) {
        struct dirent *entry;
        char path[WATCHY_IDF_PATH_MAX];
        errno = 0;
        entry = readdir(directory);
        if (entry == NULL) {
            result = errno == 0 && result;
            break;
        }
        if (!watchy_package_state_temporary_name_valid(entry->d_name)) {
            continue;
        }
        if (snprintf(path, sizeof(path), "%s/%s", root, entry->d_name) >=
            (int)sizeof(path)) {
            result = false;
            continue;
        }
        result = idf_remove_tree(NULL, path) && result;
    }
    return closedir(directory) == 0 && result;
}

static bool reconcile_state_directory(void) {
    bool result = true;
    DIR *directory = opendir("/data/state");
    if (directory == NULL) {
        return errno == ENOENT;
    }
    for (;;) {
        struct dirent *entry;
        char path[WATCHY_IDF_PATH_MAX];
        struct stat info;
        bool keep;
        errno = 0;
        entry = readdir(directory);
        if (entry == NULL) {
            result = errno == 0 && result;
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (snprintf(path, sizeof(path), "/data/state/%s", entry->d_name) >=
            (int)sizeof(path)) {
            result = false;
            continue;
        }
        keep = watchy_package_id_valid(entry->d_name) &&
               watchy_package_index_has_id(&s_index, entry->d_name) &&
               stat(path, &info) == 0 && S_ISDIR(info.st_mode) && !S_ISLNK(info.st_mode);
        if (!keep) {
            result = idf_remove_tree(NULL, path) && result;
        } else {
            result = scrub_state_directory_temporaries(path) && result;
        }
    }
    return closedir(directory) == 0 && result;
}

static watchy_package_status_t reconcile_storage(void) {
    watchy_package_status_t status = reconcile_indexed_packages();
    if (status != WATCHY_PACKAGE_OK) {
        return status;
    }
    return reconcile_directory("/data/staging", true) &&
                   reconcile_directory("/data/packages", false) &&
                   reconcile_state_directory()
               ? WATCHY_PACKAGE_OK : WATCHY_PACKAGE_ERR_FILESYSTEM;
}

static void runtime_init_lock(void) {
    while (atomic_flag_test_and_set_explicit(&s_init_lock, memory_order_acquire)) {
        vTaskDelay(1u);
    }
}

static void runtime_init_unlock(void) {
    atomic_flag_clear_explicit(&s_init_lock, memory_order_release);
}

static watchy_package_status_t ensure_package_mutex(void) {
    if (s_package_mutex == NULL && (s_package_mutex = xSemaphoreCreateMutex()) == NULL) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    return WATCHY_PACKAGE_OK;
}

static watchy_package_status_t initialize_package_index(void *context) {
    static const watchy_package_index_store_t store = {
        .load = idf_index_load, .save = idf_index_save, .context = NULL,
    };
    (void)context;
    return watchy_package_index_init(&s_index, &store);
}

static watchy_package_status_t reconcile_package_storage(void *context) {
    watchy_package_status_t status;
    BaseType_t give_status;
    (void)context;
    if (xSemaphoreTake(s_package_mutex, portMAX_DELAY) != pdTRUE) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    status = reconcile_storage();
    give_status = xSemaphoreGive(s_package_mutex);
    return give_status == pdTRUE ? status : WATCHY_PACKAGE_ERR_STATE;
}

static watchy_package_status_t select_builtin_with_package_lock(void *context) {
    watchy_package_status_t status;
    BaseType_t give_status;
    (void)context;
    if (xSemaphoreTake(s_package_mutex, portMAX_DELAY) != pdTRUE) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    status = watchy_package_select_builtin(&s_index);
    give_status = xSemaphoreGive(s_package_mutex);
    return give_status == pdTRUE ? status : WATCHY_PACKAGE_ERR_STATE;
}

static const watchy_package_runtime_init_ops_t s_runtime_init_ops = {
    .initialize_index = initialize_package_index,
    .reconcile_storage = reconcile_package_storage,
    .select_builtin = select_builtin_with_package_lock,
    .context = NULL,
};

watchy_package_status_t watchy_packages_runtime_init(void) {
    watchy_package_status_t status;
    runtime_init_lock();
    status = ensure_package_mutex();
    if (status == WATCHY_PACKAGE_OK) {
        status = watchy_package_runtime_prepare(&s_runtime_init_state,
                                                WATCHY_PACKAGE_INIT_FULL,
                                                &s_runtime_init_ops);
    }
    runtime_init_unlock();
    return status;
}

typedef struct {
    const char *filename;
    const char *package_ref;
} factory_seed_identity_t;

static const factory_seed_identity_t s_factory_seed_identities[WATCHY_FACTORY_SEED_COUNT] = {
    {"grid-01.wpk", "watchy.firstparty.grid01@1.0.0"},
    {"grid-02.wpk", "watchy.firstparty.grid02@1.0.0"},
    {"grid-03.wpk", "watchy.firstparty.grid03@1.0.0"},
    {"orbit.wpk", "watchy.firstparty.orbit@1.0.0"},
    {"slab.wpk", "watchy.firstparty.slab@1.0.0"},
    {"term-01.wpk", "watchy.firstparty.term01@1.0.0"},
    {"term-02.wpk", "watchy.firstparty.term02@1.0.0"},
    {"term-03.wpk", "watchy.firstparty.term03@1.0.0"},
};

static watchy_factory_seed_marker_result_t idf_factory_seed_read_marker(
    void *context,
    uint16_t *out_version) {
    nvs_handle_t handle = 0u;
    esp_err_t error;
    (void)context;
    if (out_version == NULL) {
        return WATCHY_FACTORY_SEED_MARKER_ERROR;
    }
    error = nvs_open(WATCHY_PACKAGE_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return WATCHY_FACTORY_SEED_MARKER_NOT_FOUND;
    if (error != ESP_OK) return WATCHY_FACTORY_SEED_MARKER_ERROR;
    error = nvs_get_u16(handle, WATCHY_PACKAGE_NVS_FACTORY_SEED_KEY, out_version);
    nvs_close(handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return WATCHY_FACTORY_SEED_MARKER_NOT_FOUND;
    return error == ESP_OK ? WATCHY_FACTORY_SEED_MARKER_FOUND
                           : WATCHY_FACTORY_SEED_MARKER_ERROR;
}

static bool idf_factory_seed_write_marker(void *context, uint16_t version) {
    nvs_handle_t handle = 0u;
    esp_err_t error;
    (void)context;
    error = nvs_open(WATCHY_PACKAGE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_set_u16(handle, WATCHY_PACKAGE_NVS_FACTORY_SEED_KEY, version);
    }
    if (error == ESP_OK) error = nvs_commit(handle);
    if (handle != 0u) nvs_close(handle);
    return error == ESP_OK;
}

static watchy_factory_seed_file_result_t idf_factory_seed_read_path(
    const char *path,
    uint8_t *bytes,
    size_t capacity,
    size_t *out_size) {
    struct stat info;
    if (path == NULL || bytes == NULL || out_size == NULL) {
        return WATCHY_FACTORY_SEED_FILE_ERROR;
    }
    if (stat(path, &info) != 0) {
        return errno == ENOENT ? WATCHY_FACTORY_SEED_FILE_NOT_FOUND
                               : WATCHY_FACTORY_SEED_FILE_ERROR;
    }
    if (!S_ISREG(info.st_mode) || S_ISLNK(info.st_mode) || info.st_size <= 0 ||
        (uintmax_t)info.st_size > capacity ||
        !idf_read_file(NULL, path, bytes, capacity, out_size)) {
        return WATCHY_FACTORY_SEED_FILE_ERROR;
    }
    return WATCHY_FACTORY_SEED_FILE_OK;
}

static watchy_factory_seed_file_result_t idf_factory_seed_read_catalog(
    void *context,
    uint8_t *bytes,
    size_t capacity,
    size_t *out_size) {
    (void)context;
    return idf_factory_seed_read_path("/data/factory/seed.bin", bytes, capacity, out_size);
}

static watchy_factory_seed_file_result_t idf_factory_seed_read_package(
    void *context,
    const char *filename,
    uint8_t *bytes,
    size_t capacity,
    size_t *out_size) {
    char path[WATCHY_IDF_PATH_MAX];
    (void)context;
    const int written = filename != NULL
                            ? snprintf(path, sizeof(path), "/data/factory/%s", filename) : -1;
    if (written < 0 || written >= (int)sizeof(path)) return WATCHY_FACTORY_SEED_FILE_ERROR;
    return idf_factory_seed_read_path(path, bytes, capacity, out_size);
}

static bool idf_factory_seed_package_installed(void *context, const char *filename) {
    bool installed = false;
    (void)context;
    if (xSemaphoreTake(s_package_mutex, portMAX_DELAY) != pdTRUE) return false;
    for (size_t index = 0u; index < WATCHY_FACTORY_SEED_COUNT; ++index) {
        if (strcmp(filename, s_factory_seed_identities[index].filename) == 0) {
            installed = watchy_package_is_installed(
                &s_index, s_factory_seed_identities[index].package_ref, NULL);
            break;
        }
    }
    (void)xSemaphoreGive(s_package_mutex);
    return installed;
}

static bool idf_factory_seed_sha256(void *context,
                                    const uint8_t *bytes,
                                    size_t size,
                                    uint8_t out_digest[WATCHY_PACKAGE_DIGEST_SIZE]) {
    const watchy_byte_region_t region = {.bytes = bytes, .size = size};
    (void)context;
    return watchy_package_sha256_regions(NULL, &region, 1u, out_digest);
}

static watchy_package_status_t idf_factory_seed_install(void *context,
                                                        uint8_t *bytes,
                                                        size_t size) {
    char package_ref[WATCHY_PACKAGE_REF_MAX + 1u];
    (void)context;
    return watchy_packages_install_blob(bytes, size, package_ref);
}

static bool idf_factory_seed_remove(void *context, const char *filename) {
    char path[WATCHY_IDF_PATH_MAX];
    (void)context;
    const int written = filename != NULL
                            ? snprintf(path, sizeof(path), "/data/factory/%s", filename) : -1;
    return written >= 0 && written < (int)sizeof(path) && unlink(path) == 0;
}

static bool idf_factory_seed_acquire_workspace(void *context,
                                               uint8_t **out_bytes,
                                               size_t *out_capacity) {
    (void)context;
    if (out_bytes == NULL || out_capacity == NULL) return false;
    const size_t capabilities = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const size_t free_internal = heap_caps_get_free_size(capabilities);
    const size_t largest_internal = heap_caps_get_largest_free_block(capabilities);
    if (!watchy_package_upload_heap_allows(WATCHY_PACKAGE_WPK_BYTES_MAX,
                                           free_internal, largest_internal)) {
        return false;
    }
    *out_bytes = heap_caps_malloc(WATCHY_PACKAGE_WPK_BYTES_MAX, capabilities);
    if (*out_bytes == NULL) return false;
    *out_capacity = WATCHY_PACKAGE_WPK_BYTES_MAX;
    return true;
}

static void idf_factory_seed_release_workspace(void *context, uint8_t *bytes) {
    (void)context;
    free(bytes);
}

watchy_package_status_t watchy_packages_import_factory_seed(bool safe_mode) {
    static const watchy_factory_seed_ops_t operations = {
        .read_marker = idf_factory_seed_read_marker,
        .write_marker = idf_factory_seed_write_marker,
        .read_catalog = idf_factory_seed_read_catalog,
        .read_package = idf_factory_seed_read_package,
        .package_installed = idf_factory_seed_package_installed,
        .sha256 = idf_factory_seed_sha256,
        .install_package = idf_factory_seed_install,
        .remove_package = idf_factory_seed_remove,
        .acquire_workspace = idf_factory_seed_acquire_workspace,
        .release_workspace = idf_factory_seed_release_workspace,
        .context = NULL,
    };
    watchy_package_status_t status;
    if (safe_mode) return WATCHY_PACKAGE_OK;
    status = watchy_packages_runtime_init();
    if (status != WATCHY_PACKAGE_OK) return status;
    if (atomic_flag_test_and_set_explicit(&s_factory_seed_lock, memory_order_acquire)) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    status = watchy_factory_seed_import(false, &operations,
                                        s_factory_seed_catalog_wire,
                                        sizeof(s_factory_seed_catalog_wire),
                                        &s_factory_seed_catalog);
    atomic_flag_clear_explicit(&s_factory_seed_lock, memory_order_release);
    return status;
}

static watchy_package_status_t runner_finish(watchy_package_status_t lifecycle_status) {
    watchy_package_status_t stop_status = WATCHY_PACKAGE_OK;
    watchy_package_status_t result = lifecycle_status;
    if (watchy_package_session_loaded(&s_runner.session)) {
        stop_status = watchy_package_session_stop(&s_runner.session);
    }
    if (s_runner.host_ready) {
        watchy_package_transition_cleanup(&s_runner.host);
        watchy_package_host_deinit(&s_runner.host);
        s_runner.host_ready = false;
    }
    if (s_runner.attempt_started) {
        result = watchy_package_finalize_watchface_attempt(
            &s_index, s_runner.reference, s_runner.pending, s_runner.rendered,
            lifecycle_status, stop_status);
    } else if (result == WATCHY_PACKAGE_OK) {
        result = stop_status;
    }
    s_runner.active = false;
    s_runner.attempt_started = false;
    s_runner.pending = false;
    s_runner.rendered = false;
    memset(s_runner.reference, 0, sizeof(s_runner.reference));
    return result;
}

static watchy_package_status_t runner_post_callback(void) {
    watchy_refresh_mode_t mode;
    const bool pump_ok = watchy_package_host_pump(&s_runner.host) == WATCHY_PACKAGE_OK;
    const bool refresh_requested = watchy_package_host_take_refresh(&s_runner.host, &mode);
    const watchy_status_t refresh_status =
        refresh_requested ? watchy_display_refresh(mode) : WATCHY_STATUS_OK;
    const watchy_package_presentation_outcome_t refresh_outcome =
        watchy_package_classify_presentation(refresh_status);
    const bool exit_requested = watchy_package_host_take_exit(&s_runner.host);
    switch (watchy_package_post_action(pump_ok, refresh_requested,
                                       refresh_outcome, exit_requested)) {
    case WATCHY_PACKAGE_POST_FAIL_CLEANUP:
        return runner_finish(pump_ok ? WATCHY_PACKAGE_ERR_CALLBACK
                                     : WATCHY_PACKAGE_ERR_STATE);
    case WATCHY_PACKAGE_POST_CLEAN_EXIT:
        return runner_finish(WATCHY_PACKAGE_OK);
    case WATCHY_PACKAGE_POST_CONTINUE:
        return WATCHY_PACKAGE_OK;
    }
    return runner_finish(WATCHY_PACKAGE_ERR_STATE);
}

watchy_package_status_t watchy_packages_runner_start(const char *package_ref, bool safe_mode) {
    static const watchy_package_loader_api_t loader = {
        .open = idf_dlopen,
        .symbol = idf_dlsym,
        .close = idf_dlclose,
        .readable = idf_readable,
        .writable = idf_writable,
        .executable = idf_executable,
        .context = &s_loader_context,
    };
    static const watchy_package_watchdog_api_t watchdog = {
        .before_callback = watchdog_before_callback,
        .after_callback = watchdog_after_callback,
        .ensure_current = watchdog_ensure_current,
        .context = &s_runner.host,
    };
    char identifier[WATCHY_PACKAGE_ID_MAX + 1u];
    char version[WATCHY_PACKAGE_VERSION_MAX + 1u];
    char manifest_path[WATCHY_IDF_PATH_MAX];
    char elf_path[WATCHY_IDF_PATH_MAX];
    char absolute_elf_path[WATCHY_IDF_PATH_MAX];
    watchy_package_type_t installed_type;
    watchy_package_status_t status;
    watchy_watchdog_scope_t watchdog_scope = {0};
    const watchy_package_index_t *index;

    if (package_ref == NULL || safe_mode) {
        return safe_mode ? WATCHY_PACKAGE_ERR_SAFE_MODE : WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    status = watchy_packages_runtime_init();
    if (status != WATCHY_PACKAGE_OK ||
        xSemaphoreTake(s_package_mutex, portMAX_DELAY) != pdTRUE) {
        return status != WATCHY_PACKAGE_OK ? status : WATCHY_PACKAGE_ERR_STATE;
    }
    if (watchy_watchdog_scope_begin(&watchdog_scope, &s_task_watchdog_ops) !=
        WATCHY_STATUS_OK) {
        return runner_release_scope(&watchdog_scope, WATCHY_PACKAGE_ERR_STATE);
    }
    status = watchy_package_watchdog_ensure_current(&watchdog);
    if (status != WATCHY_PACKAGE_OK) {
        return runner_release_scope(&watchdog_scope, status);
    }
    if (s_runner.active || watchy_package_session_loaded(&s_runner.session) ||
        !watchy_package_is_installed(&s_index, package_ref, &installed_type) ||
        watchy_package_is_quarantined(&s_index, package_ref) ||
        strnlen(package_ref, sizeof(s_runner.reference)) == sizeof(s_runner.reference) ||
        !split_package_ref(package_ref, identifier, version) ||
        snprintf(manifest_path, sizeof(manifest_path), "/data/packages/%s/%s/manifest.json",
                 identifier, version) >= (int)sizeof(manifest_path) ||
        snprintf(elf_path, sizeof(elf_path), "packages/%s/%s/package.so",
                 identifier, version) >= (int)sizeof(elf_path) ||
        snprintf(absolute_elf_path, sizeof(absolute_elf_path),
                 "/data/packages/%s/%s/package.so", identifier, version) >=
            (int)sizeof(absolute_elf_path)) {
        return runner_release_scope(&watchdog_scope, WATCHY_PACKAGE_ERR_STATE);
    }
    memcpy(s_runner.reference, package_ref, strlen(package_ref) + 1u);
    index = watchy_package_index_snapshot(&s_index);
    s_runner.pending = strcmp(index->pending_watchface, package_ref) == 0;
    status = watchy_package_begin_attempt(&s_index, package_ref, false);
    if (status != WATCHY_PACKAGE_OK) {
        s_runner.pending = false;
        memset(s_runner.reference, 0, sizeof(s_runner.reference));
        return runner_release_scope(&watchdog_scope, status);
    }
    s_runner.attempt_started = true;
    if (!read_manifest(manifest_path, &s_runner.manifest) ||
        strcmp(s_runner.manifest.id, identifier) != 0 ||
        strcmp(s_runner.manifest.version, version) != 0 ||
        s_runner.manifest.type != installed_type ||
        !validate_installed_elf(absolute_elf_path,
                                s_runner.manifest.max_runtime_bytes)) {
        status = runner_finish(WATCHY_PACKAGE_ERR_STATE);
        return runner_release_scope(&watchdog_scope, status);
    }
    status = watchy_package_host_init(&s_runner.host, &s_runner.manifest);
    if (status == WATCHY_PACKAGE_OK) {
        s_runner.host_ready = true;
        status = watchy_package_session_init(&s_runner.session, &loader, &watchdog);
    }
    if (status == WATCHY_PACKAGE_OK) {
        status = watchy_package_session_load(&s_runner.session, elf_path, &s_runner.manifest,
                                             watchy_package_host_caps(&s_runner.host), false, false);
    }
    if (status == WATCHY_PACKAGE_OK) {
        status = watchy_package_session_start(&s_runner.session);
    }
    if (status == WATCHY_PACKAGE_OK) {
        s_runner.active = true;
        status = runner_post_callback();
    } else {
        status = runner_finish(status);
    }
    return runner_release_scope(&watchdog_scope, status);
}

watchy_package_status_t watchy_packages_runner_event(const watchy_event_t *event) {
    watchy_package_status_t status;
    watchy_watchdog_scope_t watchdog_scope = {0};
    if (event == NULL || watchy_packages_runtime_init() != WATCHY_PACKAGE_OK ||
        xSemaphoreTake(s_package_mutex, portMAX_DELAY) != pdTRUE) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    if (!s_runner.active) {
        (void)xSemaphoreGive(s_package_mutex);
        return WATCHY_PACKAGE_ERR_STATE;
    }
    if (watchy_watchdog_scope_begin(&watchdog_scope, &s_task_watchdog_ops) !=
            WATCHY_STATUS_OK ||
        !watchdog_ensure_current(&s_runner.host)) {
        (void)runner_finish(WATCHY_PACKAGE_ERR_STATE);
        return runner_release_scope(&watchdog_scope, WATCHY_PACKAGE_ERR_STATE);
    }
    status = watchy_package_session_event(&s_runner.session, event);
    if (status == WATCHY_PACKAGE_OK) {
        status = runner_post_callback();
    } else {
        (void)runner_finish(status);
    }
    return runner_release_scope(&watchdog_scope, status);
}

static watchy_status_t runner_display_present(
    void *context,
    watchy_refresh_mode_t mode,
    const watchy_transition_request_v1_t *request) {
    (void)context;
    return watchy_display_present(mode, request);
}

watchy_package_status_t watchy_packages_runner_render_with_transition(
    const watchy_transition_request_v1_t *transition) {
    watchy_canvas_t canvas;
    watchy_refresh_mode_t mode = WATCHY_REFRESH_PARTIAL;
    watchy_status_t display_status;
    watchy_package_status_t status;
    watchy_watchdog_scope_t watchdog_scope = {0};
    if (watchy_packages_runtime_init() != WATCHY_PACKAGE_OK ||
        xSemaphoreTake(s_package_mutex, portMAX_DELAY) != pdTRUE) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    if (!s_runner.active || s_runner.host.canvas.acquire == NULL) {
        (void)xSemaphoreGive(s_package_mutex);
        return WATCHY_PACKAGE_ERR_STATE;
    }
    if (watchy_watchdog_scope_begin(&watchdog_scope, &s_task_watchdog_ops) !=
            WATCHY_STATUS_OK ||
        !watchdog_ensure_current(&s_runner.host)) {
        (void)runner_finish(WATCHY_PACKAGE_ERR_STATE);
        return runner_release_scope(&watchdog_scope, WATCHY_PACKAGE_ERR_STATE);
    }
    canvas = s_runner.host.canvas.acquire(s_runner.host.canvas.context);
    status = watchy_package_session_render(&s_runner.session, &canvas, &mode);
    s_runner.host.canvas.release(s_runner.host.canvas.context, &canvas);
    if (status == WATCHY_PACKAGE_OK && s_runner.host.canvas_acquired) {
        status = WATCHY_PACKAGE_ERR_CALLBACK;
    }
    display_status = watchy_package_transition_present_override_after_render(
        &s_runner.host.transition, status == WATCHY_PACKAGE_OK, mode,
        transition, runner_display_present, NULL);
    if (status == WATCHY_PACKAGE_OK) {
        switch (watchy_package_classify_presentation(display_status)) {
        case WATCHY_PACKAGE_PRESENT_TARGET:
            s_runner.rendered = true;
            break;
        case WATCHY_PACKAGE_PRESENT_CANCELLED:
            break;
        case WATCHY_PACKAGE_PRESENT_FAILED:
            status = WATCHY_PACKAGE_ERR_CALLBACK;
            break;
        }
    }
    if (status == WATCHY_PACKAGE_OK) {
        status = runner_post_callback();
    } else {
        status = runner_finish(status);
    }
    return runner_release_scope(&watchdog_scope, status);
}

watchy_package_status_t watchy_packages_runner_render(void) {
    return watchy_packages_runner_render_with_transition(NULL);
}

watchy_package_status_t watchy_packages_runner_stop(void) {
    watchy_watchdog_scope_t watchdog_scope = {0};
    if (watchy_packages_runtime_init() != WATCHY_PACKAGE_OK ||
        xSemaphoreTake(s_package_mutex, portMAX_DELAY) != pdTRUE) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    if (!s_runner.active) {
        (void)xSemaphoreGive(s_package_mutex);
        return WATCHY_PACKAGE_ERR_STATE;
    }
    if (watchy_watchdog_scope_begin(&watchdog_scope, &s_task_watchdog_ops) !=
            WATCHY_STATUS_OK ||
        !watchdog_ensure_current(&s_runner.host)) {
        (void)runner_finish(WATCHY_PACKAGE_ERR_STATE);
        return runner_release_scope(&watchdog_scope, WATCHY_PACKAGE_ERR_STATE);
    }
    watchy_package_status_t status = runner_finish(WATCHY_PACKAGE_OK);
    status = runner_release_scope(&watchdog_scope, status);
    return status;
}

bool watchy_packages_runner_active(void) {
    return s_runner.active;
}

typedef struct {
    const char *reference;
    bool force_full_refresh;
    bool has_transition;
    watchy_transition_request_v1_t transition;
} watchface_cycle_context_t;

static watchy_package_status_t watchface_cycle_start(void *context) {
    const watchface_cycle_context_t *cycle = context;
    return watchy_packages_runner_start(cycle->reference, false);
}

static bool watchface_cycle_active(void *context) {
    (void)context;
    return watchy_packages_runner_active();
}

static watchy_package_status_t watchface_cycle_render(void *context) {
    const watchface_cycle_context_t *cycle = context;
    if (cycle->force_full_refresh && !cycle->has_transition) {
        /* Package startup may request a refresh of its own. Invalidate again
         * immediately before on_render so the selected face target itself is
         * always committed with a full physical refresh. */
        watchy_display_invalidate_previous();
    }
    return watchy_packages_runner_render_with_transition(
        cycle->has_transition ? &cycle->transition : NULL);
}

static watchy_package_status_t watchface_cycle_stop(void *context) {
    (void)context;
    return watchy_packages_runner_stop();
}

bool watchy_packages_run_watchface(bool safe_mode, bool force_full_refresh) {
    return watchy_packages_run_watchface_with_transition(
        safe_mode, force_full_refresh, NULL);
}

bool watchy_packages_run_watchface_with_transition(
    bool safe_mode,
    bool force_full_refresh,
    const watchy_transition_request_v1_t *transition) {
    const watchy_package_index_t *index;
    char reference[WATCHY_PACKAGE_REF_MAX + 1u];
    if (safe_mode ||
        (transition != NULL && watchy_transition_validate(transition) != WATCHY_STATUS_OK) ||
        watchy_packages_runtime_init() != WATCHY_PACKAGE_OK ||
        xSemaphoreTake(s_package_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    index = watchy_package_index_snapshot(&s_index);
    const char *selected = index->pending_watchface[0] != '\0'
                               ? index->pending_watchface : index->active_watchface;
    if (selected[0] == '\0' || strnlen(selected, sizeof(reference)) == sizeof(reference)) {
        (void)xSemaphoreGive(s_package_mutex);
        return false;
    }
    memcpy(reference, selected, strlen(selected) + 1u);
    (void)xSemaphoreGive(s_package_mutex);
    watchface_cycle_context_t cycle = {
        .reference = reference,
        .force_full_refresh = force_full_refresh,
        .has_transition = transition != NULL,
    };
    if (transition != NULL) {
        cycle.transition = *transition;
        if (force_full_refresh) {
            cycle.transition.flags |= WATCHY_TRANSITION_PREFER_FULL;
        }
    }
    const watchy_package_watchface_runner_t runner = {
        .start = watchface_cycle_start,
        .active = watchface_cycle_active,
        .render = watchface_cycle_render,
        .stop = watchface_cycle_stop,
        .context = &cycle,
    };
    return watchy_package_run_watchface_cycle(&runner);
}

watchy_package_status_t watchy_packages_install_blob(
    uint8_t *wpk,
    size_t wpk_size,
    char out_package_ref[WATCHY_PACKAGE_REF_MAX + 1u]) {
    static const watchy_crypto_api_t crypto = {.sha256 = idf_sha256, .context = NULL};
    static const watchy_package_fs_api_t filesystem = {
        .write_file = idf_write_file,
        .write_file_exclusive = idf_write_file_exclusive,
        .read_file = idf_read_file,
        .mkdirs = idf_mkdirs,
        .mkdir_exclusive = idf_mkdir_exclusive,
        .sync_tree = idf_sync_tree,
        .rename_noreplace = idf_rename_noreplace,
        .path_exists = idf_path_exists,
        .remove_tree = idf_remove_tree,
        .unique_id = idf_unique_id,
        .context = NULL,
    };
    watchy_package_status_t status = watchy_packages_runtime_init();
    if (status != WATCHY_PACKAGE_OK || wpk == NULL || out_package_ref == NULL) {
        return status != WATCHY_PACKAGE_OK ? status : WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    if (wpk_size > WATCHY_PACKAGE_WPK_BYTES_MAX) {
        return WATCHY_PACKAGE_ERR_LIMIT;
    }
    if (xSemaphoreTake(s_package_mutex, portMAX_DELAY) != pdTRUE) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    /* Installation is excluded from a resident loader and reuses the mutable
     * upload allocation for the durable stage readback. This keeps the 80 KiB
     * ceiling to one contiguous WPK allocation on the no-PSRAM Watchy 2.0. */
    if (s_runner.active || watchy_package_session_loaded(&s_runner.session)) {
        (void)xSemaphoreGive(s_package_mutex);
        return WATCHY_PACKAGE_ERR_STATE;
    }
    s_install_workspace.stage_bytes = wpk;
    s_install_workspace.stage_capacity = wpk_size;
    status = watchy_package_install(&s_index, &filesystem, &s_install_workspace,
                                    wpk, wpk_size, &crypto, out_package_ref);
    s_install_workspace.stage_bytes = NULL;
    s_install_workspace.stage_capacity = 0u;
    (void)xSemaphoreGive(s_package_mutex);
    return status;
}

watchy_package_status_t watchy_packages_select_watchface(const char *package_ref) {
    watchy_package_status_t status = watchy_packages_runtime_init();
    if (status != WATCHY_PACKAGE_OK || package_ref == NULL ||
        xSemaphoreTake(s_package_mutex, portMAX_DELAY) != pdTRUE) {
        return status != WATCHY_PACKAGE_OK ? status : WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    status = watchy_package_select_watchface(&s_index, package_ref);
    (void)xSemaphoreGive(s_package_mutex);
    return status;
}

watchy_package_status_t watchy_packages_select_builtin(void) {
    watchy_package_status_t status;
    runtime_init_lock();
    status = ensure_package_mutex();
    if (status == WATCHY_PACKAGE_OK) {
        status = watchy_package_runtime_select_builtin(&s_runtime_init_state,
                                                        &s_runtime_init_ops);
    }
    runtime_init_unlock();
    return status;
}

watchy_package_status_t watchy_packages_recover_stale_pending(void) {
    watchy_package_status_t status = watchy_packages_runtime_init();
    if (status != WATCHY_PACKAGE_OK ||
        xSemaphoreTake(s_package_mutex, portMAX_DELAY) != pdTRUE) {
        return status != WATCHY_PACKAGE_OK ? status : WATCHY_PACKAGE_ERR_STATE;
    }
    status = watchy_package_recover_stale_pending(&s_index);
    (void)xSemaphoreGive(s_package_mutex);
    return status;
}

watchy_package_status_t watchy_packages_snapshot(watchy_package_catalog_t *out_catalog) {
    watchy_package_status_t status;
    if (out_catalog == NULL) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    memset(out_catalog, 0, sizeof(*out_catalog));
    status = watchy_packages_runtime_init();
    if (status != WATCHY_PACKAGE_OK ||
        xSemaphoreTake(s_package_mutex, portMAX_DELAY) != pdTRUE) {
        return status != WATCHY_PACKAGE_OK ? status : WATCHY_PACKAGE_ERR_STATE;
    }
    status = watchy_package_catalog_snapshot(&s_index, read_installed_manifest,
                                              NULL, &s_reconcile_manifest, out_catalog);
    (void)xSemaphoreGive(s_package_mutex);
    return status;
}

watchy_package_mutation_result_t watchy_packages_remove_observed(
    const char *package_ref) {
    char identifier[WATCHY_PACKAGE_ID_MAX + 1u];
    char version[WATCHY_PACKAGE_VERSION_MAX + 1u];
    char path[WATCHY_IDF_PATH_MAX];
    char state_path[WATCHY_IDF_PATH_MAX];
    char identifier_path[WATCHY_IDF_PATH_MAX];
    watchy_package_status_t status = watchy_packages_runtime_init();
    watchy_package_mutation_result_t result = {.status = status};
    if (status != WATCHY_PACKAGE_OK || package_ref == NULL ||
        !split_package_ref(package_ref, identifier, version) ||
        snprintf(path, sizeof(path), "/data/packages/%s/%s", identifier, version) >=
            (int)sizeof(path) ||
        snprintf(state_path, sizeof(state_path), "/data/state/%s", identifier) >=
            (int)sizeof(state_path) ||
        snprintf(identifier_path, sizeof(identifier_path), "/data/packages/%s", identifier) >=
            (int)sizeof(identifier_path)) {
        result.status = status != WATCHY_PACKAGE_OK ? status : WATCHY_PACKAGE_ERR_ARGUMENT;
        return result;
    }
    if (xSemaphoreTake(s_package_mutex, portMAX_DELAY) != pdTRUE) {
        result.status = WATCHY_PACKAGE_ERR_STATE;
        return result;
    }
    if (s_runner.active || watchy_package_session_loaded(&s_runner.session)) {
        (void)xSemaphoreGive(s_package_mutex);
        result.status = WATCHY_PACKAGE_ERR_STATE;
        return result;
    }
    status = watchy_package_unregister(&s_index, package_ref);
    result.index_mutated = status == WATCHY_PACKAGE_OK;
    if (status == WATCHY_PACKAGE_OK && !idf_remove_tree(NULL, path)) {
        status = WATCHY_PACKAGE_ERR_FILESYSTEM;
    }
    if (status == WATCHY_PACKAGE_OK && !watchy_package_index_has_id(&s_index, identifier) &&
        (!idf_remove_tree(NULL, state_path) || !idf_remove_tree(NULL, identifier_path))) {
        status = WATCHY_PACKAGE_ERR_FILESYSTEM;
    }
    (void)xSemaphoreGive(s_package_mutex);
    result.status = status;
    return result;
}

watchy_package_status_t watchy_packages_remove(const char *package_ref) {
    return watchy_packages_remove_observed(package_ref).status;
}

watchy_package_mutation_result_t watchy_packages_safe_mode_purge_observed(void) {
    watchy_package_status_t index_status;
    watchy_package_mutation_result_t result = {0};
    bool filesystem_ok;
    runtime_init_lock();
    if (ensure_package_mutex() != WATCHY_PACKAGE_OK) {
        runtime_init_unlock();
        result.status = WATCHY_PACKAGE_ERR_STATE;
        return result;
    }
    if (xSemaphoreTake(s_package_mutex, portMAX_DELAY) != pdTRUE) {
        runtime_init_unlock();
        result.status = WATCHY_PACKAGE_ERR_STATE;
        return result;
    }
    if (s_runner.active || watchy_package_session_loaded(&s_runner.session)) {
        (void)xSemaphoreGive(s_package_mutex);
        runtime_init_unlock();
        result.status = WATCHY_PACKAGE_ERR_STATE;
        return result;
    }
    memset(&s_empty_index, 0, sizeof(s_empty_index));
    s_empty_index.magic = WATCHY_PACKAGE_INDEX_MAGIC;
    s_empty_index.version = WATCHY_PACKAGE_INDEX_VERSION;
    index_status = s_runtime_init_state.index_ready
                       ? watchy_package_index_clear(&s_index)
                       : (idf_index_save(NULL, &s_empty_index)
                              ? WATCHY_PACKAGE_OK : WATCHY_PACKAGE_ERR_STORE);
    if (index_status != WATCHY_PACKAGE_OK) {
        (void)xSemaphoreGive(s_package_mutex);
        runtime_init_unlock();
        result.status = index_status;
        return result;
    }
    result.index_mutated = true;
    const bool staging_ok = idf_remove_tree(NULL, "/data/staging");
    const bool packages_ok = idf_remove_tree(NULL, "/data/packages");
    const bool state_ok = idf_remove_tree(NULL, "/data/state");
    filesystem_ok = staging_ok && packages_ok && state_ok;
    memset(&s_index, 0, sizeof(s_index));
    memset(&s_runtime_init_state, 0, sizeof(s_runtime_init_state));
    (void)xSemaphoreGive(s_package_mutex);
    runtime_init_unlock();
    if (!filesystem_ok) {
        result.status = WATCHY_PACKAGE_ERR_FILESYSTEM;
        return result;
    }
    result.status = watchy_packages_runtime_init();
    return result;
}

watchy_package_status_t watchy_packages_safe_mode_purge(void) {
    return watchy_packages_safe_mode_purge_observed().status;
}

watchy_package_status_t watchy_packages_upload_begin(size_t expected_size) {
    watchy_package_status_t status;
    if (expected_size == 0u || expected_size > WATCHY_PACKAGE_WPK_BYTES_MAX) {
        return expected_size > WATCHY_PACKAGE_WPK_BYTES_MAX ? WATCHY_PACKAGE_ERR_LIMIT
                                                            : WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    status = watchy_packages_runtime_init();
    if (status != WATCHY_PACKAGE_OK) {
        return status;
    }
    if (atomic_flag_test_and_set_explicit(&s_upload_lock, memory_order_acquire)) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    memset(&s_upload, 0, sizeof(s_upload));
    s_upload.descriptor = -1;
    s_upload.expected_size = expected_size;
    if (!idf_mkdirs_path("/data/staging")) {
        atomic_flag_clear_explicit(&s_upload_lock, memory_order_release);
        return WATCHY_PACKAGE_ERR_FILESYSTEM;
    }
    for (unsigned attempt = 0u; attempt < 8u; ++attempt) {
        const int result = snprintf(s_upload.path, sizeof(s_upload.path),
                                    "/data/staging/.watchy-portal-%08" PRIx32 ".wpk",
                                    esp_random());
        if (result < 0 || result >= (int)sizeof(s_upload.path)) {
            break;
        }
        s_upload.descriptor = open(s_upload.path, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (s_upload.descriptor >= 0) {
            s_upload.active = true;
            return WATCHY_PACKAGE_OK;
        }
    }
    memset(&s_upload, 0, sizeof(s_upload));
    s_upload.descriptor = -1;
    atomic_flag_clear_explicit(&s_upload_lock, memory_order_release);
    return WATCHY_PACKAGE_ERR_FILESYSTEM;
}

watchy_package_status_t watchy_packages_upload_write(const uint8_t *bytes, size_t size) {
    if (!s_upload.active || s_upload.descriptor < 0 || (bytes == NULL && size != 0u) ||
        size > s_upload.expected_size - s_upload.received_size) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    size_t offset = 0u;
    while (offset < size) {
        const ssize_t written = write(s_upload.descriptor, bytes + offset, size - offset);
        if (written <= 0) {
            return WATCHY_PACKAGE_ERR_FILESYSTEM;
        }
        offset += (size_t)written;
    }
    s_upload.received_size += size;
    return WATCHY_PACKAGE_OK;
}

void watchy_packages_upload_abort(void) {
    if (!s_upload.active) {
        return;
    }
    if (s_upload.descriptor >= 0) {
        (void)close(s_upload.descriptor);
    }
    if (s_upload.path[0] != '\0') {
        (void)unlink(s_upload.path);
    }
    memset(&s_upload, 0, sizeof(s_upload));
    s_upload.descriptor = -1;
    atomic_flag_clear_explicit(&s_upload_lock, memory_order_release);
}

watchy_package_status_t watchy_packages_upload_finish(
    char out_package_ref[WATCHY_PACKAGE_REF_MAX + 1u]) {
    uint8_t *bytes;
    size_t received;
    char path[WATCHY_IDF_PATH_MAX];
    watchy_package_status_t status;
    if (!s_upload.active || out_package_ref == NULL) {
        watchy_packages_upload_abort();
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    if (s_upload.received_size != s_upload.expected_size) {
        watchy_packages_upload_abort();
        return WATCHY_PACKAGE_ERR_WPK;
    }
    const bool sync_ok = fsync(s_upload.descriptor) == 0;
    const bool close_ok = close(s_upload.descriptor) == 0;
    s_upload.descriptor = -1;
    status = watchy_package_upload_finalize_status(true, true, true, sync_ok, close_ok);
    if (status != WATCHY_PACKAGE_OK) {
        watchy_packages_upload_abort();
        return status;
    }
    received = s_upload.received_size;
    memcpy(path, s_upload.path, sizeof(path));
    const size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t largest_internal =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!watchy_package_upload_heap_allows(received, free_internal, largest_internal)) {
        watchy_packages_upload_abort();
        return WATCHY_PACKAGE_ERR_LIMIT;
    }
    bytes = heap_caps_malloc(received, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (bytes == NULL) {
        watchy_packages_upload_abort();
        return WATCHY_PACKAGE_ERR_LIMIT;
    }
    if (!idf_read_file(NULL, path, bytes, received, &received)) {
        free(bytes);
        watchy_packages_upload_abort();
        return WATCHY_PACKAGE_ERR_FILESYSTEM;
    }
    status = watchy_packages_install_blob(bytes, received, out_package_ref);
    free(bytes);
    watchy_packages_upload_abort();
    return status;
}

bool watchy_packages_upload_active(void) {
    return s_upload.active;
}

bool watchy_packages_link_smoke(void) {
    watchy_package_status_t (*install_fn)(uint8_t *, size_t, char *) =
        watchy_packages_install_blob;
    watchy_package_status_t (*select_fn)(const char *) = watchy_packages_select_watchface;
    watchy_package_status_t (*builtin_fn)(void) = watchy_packages_select_builtin;
    watchy_package_status_t (*event_fn)(const watchy_event_t *) = watchy_packages_runner_event;
    uintptr_t install_address = 0u;
    uintptr_t select_address = 0u;
    uintptr_t builtin_address = 0u;
    uintptr_t event_address = 0u;
    _Static_assert(sizeof(install_fn) == sizeof(install_address), "function pointer size");
    _Static_assert(sizeof(select_fn) == sizeof(select_address), "function pointer size");
    _Static_assert(sizeof(builtin_fn) == sizeof(builtin_address), "function pointer size");
    _Static_assert(sizeof(event_fn) == sizeof(event_address), "function pointer size");
    memcpy(&install_address, &install_fn, sizeof(install_address));
    memcpy(&select_address, &select_fn, sizeof(select_address));
    memcpy(&builtin_address, &builtin_fn, sizeof(builtin_address));
    memcpy(&event_address, &event_fn, sizeof(event_address));
    return install_address != 0u && select_address != 0u && builtin_address != 0u &&
           event_address != 0u;
}
