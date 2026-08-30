#include "watchy/package_runtime.h"

#include "watchy/display.h"
#include "watchy/package_host.h"
#include "watchy/package_crypto.h"
#include "watchy/storage.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_dlfcn.h"
#include "esp_memory_utils.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "private/esp_dlmod.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#define WATCHY_IDF_PATH_MAX 256u
#define WATCHY_PACKAGE_NVS_NAMESPACE "watchy_pkg"
#define WATCHY_PACKAGE_NVS_INDEX_KEY "index"
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
static watchy_package_install_workspace_t s_install_workspace;
static package_runner_t s_runner;
static package_loader_context_t s_loader_context;
static SemaphoreHandle_t s_package_mutex;
static uint8_t s_index_wire[WATCHY_PACKAGE_INDEX_WIRE_MAX];
static DIR *s_remove_directories[WATCHY_REMOVE_DEPTH_MAX];
static size_t s_remove_lengths[WATCHY_REMOVE_DEPTH_MAX];
static char s_remove_path[WATCHY_IDF_PATH_MAX];
static bool s_initialized;
static atomic_flag s_init_lock = ATOMIC_FLAG_INIT;

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

static bool owned_range(uintptr_t allocation_start,
                        size_t allocation_size,
                        const void *address,
                        size_t size) {
    const uintptr_t start = (uintptr_t)address;
    uintptr_t allocation_end;
    uintptr_t end;
    return allocation_size != 0u && size != 0u &&
           allocation_start <= UINTPTR_MAX - allocation_size &&
           start <= UINTPTR_MAX - size &&
           (allocation_end = allocation_start + allocation_size) >= allocation_start &&
           (end = start + size) >= start && start >= allocation_start && end <= allocation_end;
}

static bool idf_readable(void *context, const void *address, size_t size) {
    const package_loader_context_t *loader = (const package_loader_context_t *)context;
    return loader != NULL && loader->handle != NULL && target_range(address, size, 0) &&
           (owned_range(loader->text_start, loader->text_size, address, size) ||
            owned_range(loader->data_start, loader->data_size, address, size));
}

static bool idf_writable(void *context, void *address, size_t size) {
    (void)context;
    return target_range(address, size, 1);
}

static bool idf_executable(void *context, const void *address, size_t size) {
    const package_loader_context_t *loader = (const package_loader_context_t *)context;
    return loader != NULL && loader->handle != NULL && target_range(address, size, 2) &&
           owned_range(loader->text_start, loader->text_size, address, size);
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

static void watchdog_kick(void *context) {
    (void)context;
    (void)esp_task_wdt_reset();
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

static bool read_manifest(const char *path, watchy_package_manifest_t *out_manifest) {
    uint8_t *bytes;
    struct stat info;
    FILE *file;
    watchy_package_status_t status;
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode) || S_ISLNK(info.st_mode) ||
        info.st_size <= 0 || info.st_size > (off_t)WATCHY_PACKAGE_MANIFEST_BYTES_MAX) {
        return false;
    }
    bytes = malloc((size_t)info.st_size);
    file = bytes != NULL ? fopen(path, "rb") : NULL;
    if (file == NULL) {
        free(bytes);
        return false;
    }
    if (fread(bytes, 1u, (size_t)info.st_size, file) != (size_t)info.st_size ||
        fclose(file) != 0) {
        free(bytes);
        return false;
    }
    status = watchy_package_manifest_parse(bytes, (size_t)info.st_size, out_manifest);
    free(bytes);
    return status == WATCHY_PACKAGE_OK;
}

static bool package_index_contains_ref(const char *reference) {
    return watchy_package_is_installed(&s_index, reference, NULL);
}

static void reconcile_directory(const char *root, bool staging) {
    DIR *first = opendir(root);
    struct dirent *entry;
    if (first == NULL) {
        return;
    }
    while ((entry = readdir(first)) != NULL) {
        char first_path[WATCHY_IDF_PATH_MAX];
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
            snprintf(first_path, sizeof(first_path), "%s/%s", root, entry->d_name) >=
                (int)sizeof(first_path)) {
            continue;
        }
        if (staging) {
            (void)idf_remove_tree(NULL, first_path);
            continue;
        }
        DIR *versions = opendir(first_path);
        struct dirent *version;
        if (versions == NULL) {
            (void)idf_remove_tree(NULL, first_path);
            continue;
        }
        while ((version = readdir(versions)) != NULL) {
            char version_path[WATCHY_IDF_PATH_MAX];
            char reference[WATCHY_PACKAGE_REF_MAX + 1u];
            if (strcmp(version->d_name, ".") == 0 || strcmp(version->d_name, "..") == 0 ||
                snprintf(version_path, sizeof(version_path), "%s/%s", first_path,
                         version->d_name) >= (int)sizeof(version_path)) {
                continue;
            }
            if (strstr(version->d_name, ".new") != NULL ||
                snprintf(reference, sizeof(reference), "%s@%s", entry->d_name,
                         version->d_name) >= (int)sizeof(reference) ||
                !package_index_contains_ref(reference)) {
                (void)idf_remove_tree(NULL, version_path);
            }
        }
        (void)closedir(versions);
    }
    (void)closedir(first);
}

static void reconcile_storage(void) {
    reconcile_directory("/data/staging", true);
    reconcile_directory("/data/packages", false);
}

watchy_package_status_t watchy_packages_runtime_init(void) {
    static const watchy_package_index_store_t store = {
        .load = idf_index_load, .save = idf_index_save, .context = NULL,
    };
    watchy_package_status_t status;
    esp_err_t watchdog_status;
    while (atomic_flag_test_and_set_explicit(&s_init_lock, memory_order_acquire)) {
        vTaskDelay(1u);
    }
    if (s_initialized) {
        atomic_flag_clear_explicit(&s_init_lock, memory_order_release);
        return WATCHY_PACKAGE_OK;
    }
    if (s_package_mutex == NULL && (s_package_mutex = xSemaphoreCreateMutex()) == NULL) {
        atomic_flag_clear_explicit(&s_init_lock, memory_order_release);
        return WATCHY_PACKAGE_ERR_STATE;
    }
    watchdog_status = esp_task_wdt_status(NULL);
    if (watchdog_status == ESP_ERR_NOT_FOUND) {
        watchdog_status = esp_task_wdt_add(NULL);
    }
    if (watchdog_status != ESP_OK) {
        atomic_flag_clear_explicit(&s_init_lock, memory_order_release);
        return WATCHY_PACKAGE_ERR_STATE;
    }
    status = watchy_package_index_init(&s_index, &store);
    if (status == WATCHY_PACKAGE_OK) {
        reconcile_storage();
        s_initialized = true;
    }
    atomic_flag_clear_explicit(&s_init_lock, memory_order_release);
    return status;
}

static void runner_finish(bool clean) {
    bool stopped_cleanly = true;
    if (watchy_package_session_loaded(&s_runner.session)) {
        stopped_cleanly = watchy_package_session_stop(&s_runner.session) == WATCHY_PACKAGE_OK;
    }
    if (s_runner.host_ready) {
        watchy_package_host_deinit(&s_runner.host);
        s_runner.host_ready = false;
    }
    if (s_runner.attempt_started) {
        (void)watchy_package_finish_attempt(&s_index,
                                            s_runner.reference,
                                            clean && stopped_cleanly);
    }
    if (s_runner.pending && (!clean || !s_runner.rendered)) {
        (void)watchy_package_rollback_pending(&s_index, s_runner.reference);
    }
    s_runner.active = false;
    s_runner.attempt_started = false;
    s_runner.pending = false;
    s_runner.rendered = false;
    memset(s_runner.reference, 0, sizeof(s_runner.reference));
}

static watchy_package_status_t runner_post_callback(void) {
    watchy_refresh_mode_t mode;
    if (watchy_package_host_pump(&s_runner.host) != WATCHY_PACKAGE_OK) {
        runner_finish(false);
        return WATCHY_PACKAGE_ERR_STATE;
    }
    if (watchy_package_host_take_refresh(&s_runner.host, &mode) &&
        watchy_display_refresh(mode) != WATCHY_STATUS_OK) {
        runner_finish(false);
        return WATCHY_PACKAGE_ERR_CALLBACK;
    }
    if (watchy_package_host_take_exit(&s_runner.host)) {
        runner_finish(true);
    }
    return WATCHY_PACKAGE_OK;
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
        .before_callback = watchdog_kick, .after_callback = watchdog_kick, .context = NULL,
    };
    char identifier[WATCHY_PACKAGE_ID_MAX + 1u];
    char version[WATCHY_PACKAGE_VERSION_MAX + 1u];
    char manifest_path[WATCHY_IDF_PATH_MAX];
    char elf_path[WATCHY_IDF_PATH_MAX];
    watchy_package_type_t installed_type;
    watchy_package_status_t status;
    const watchy_package_index_t *index;

    if (package_ref == NULL || safe_mode) {
        return safe_mode ? WATCHY_PACKAGE_ERR_SAFE_MODE : WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    status = watchy_packages_runtime_init();
    if (status != WATCHY_PACKAGE_OK ||
        xSemaphoreTake(s_package_mutex, portMAX_DELAY) != pdTRUE) {
        return status != WATCHY_PACKAGE_OK ? status : WATCHY_PACKAGE_ERR_STATE;
    }
    if (s_runner.active || !watchy_package_is_installed(&s_index, package_ref, &installed_type) ||
        watchy_package_is_quarantined(&s_index, package_ref) ||
        strnlen(package_ref, sizeof(s_runner.reference)) == sizeof(s_runner.reference) ||
        !split_package_ref(package_ref, identifier, version) ||
        snprintf(manifest_path, sizeof(manifest_path), "/data/packages/%s/%s/manifest.json",
                 identifier, version) >= (int)sizeof(manifest_path) ||
        snprintf(elf_path, sizeof(elf_path), "packages/%s/%s/package.so",
                 identifier, version) >= (int)sizeof(elf_path) ||
        !read_manifest(manifest_path, &s_runner.manifest) ||
        strcmp(s_runner.manifest.id, identifier) != 0 ||
        strcmp(s_runner.manifest.version, version) != 0 ||
        s_runner.manifest.type != installed_type) {
        (void)xSemaphoreGive(s_package_mutex);
        return WATCHY_PACKAGE_ERR_STATE;
    }
    memcpy(s_runner.reference, package_ref, strlen(package_ref) + 1u);
    index = watchy_package_index_snapshot(&s_index);
    s_runner.pending = strcmp(index->pending_watchface, package_ref) == 0;
    status = watchy_package_begin_attempt(&s_index, package_ref, false);
    if (status == WATCHY_PACKAGE_OK) {
        s_runner.attempt_started = true;
        status = watchy_package_host_init(&s_runner.host, &s_runner.manifest);
    }
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
        runner_finish(false);
    }
    (void)xSemaphoreGive(s_package_mutex);
    return status;
}

watchy_package_status_t watchy_packages_runner_event(const watchy_event_t *event) {
    watchy_package_status_t status;
    if (event == NULL || watchy_packages_runtime_init() != WATCHY_PACKAGE_OK ||
        xSemaphoreTake(s_package_mutex, portMAX_DELAY) != pdTRUE) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    if (!s_runner.active) {
        (void)xSemaphoreGive(s_package_mutex);
        return WATCHY_PACKAGE_ERR_STATE;
    }
    status = watchy_package_session_event(&s_runner.session, event);
    if (status == WATCHY_PACKAGE_OK) {
        status = runner_post_callback();
    } else {
        runner_finish(false);
    }
    (void)xSemaphoreGive(s_package_mutex);
    return status;
}

watchy_package_status_t watchy_packages_runner_render(void) {
    watchy_canvas_t canvas;
    watchy_refresh_mode_t mode = WATCHY_REFRESH_PARTIAL;
    watchy_package_status_t status;
    if (watchy_packages_runtime_init() != WATCHY_PACKAGE_OK ||
        xSemaphoreTake(s_package_mutex, portMAX_DELAY) != pdTRUE) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    if (!s_runner.active || s_runner.host.canvas.acquire == NULL) {
        (void)xSemaphoreGive(s_package_mutex);
        return WATCHY_PACKAGE_ERR_STATE;
    }
    canvas = s_runner.host.canvas.acquire(s_runner.host.canvas.context);
    status = watchy_package_session_render(&s_runner.session, &canvas, &mode);
    s_runner.host.canvas.release(s_runner.host.canvas.context, &canvas);
    if (status == WATCHY_PACKAGE_OK && s_runner.host.canvas_acquired) {
        status = WATCHY_PACKAGE_ERR_CALLBACK;
    }
    if (status == WATCHY_PACKAGE_OK && watchy_display_refresh(mode) == WATCHY_STATUS_OK) {
        s_runner.rendered = true;
        if (s_runner.pending) {
            status = watchy_package_promote_pending(&s_index, s_runner.reference);
            if (status == WATCHY_PACKAGE_OK) {
                s_runner.pending = false;
            }
        }
    } else if (status == WATCHY_PACKAGE_OK) {
        status = WATCHY_PACKAGE_ERR_CALLBACK;
    }
    if (status == WATCHY_PACKAGE_OK) {
        status = runner_post_callback();
    } else {
        runner_finish(false);
    }
    (void)xSemaphoreGive(s_package_mutex);
    return status;
}

watchy_package_status_t watchy_packages_runner_stop(void) {
    if (watchy_packages_runtime_init() != WATCHY_PACKAGE_OK ||
        xSemaphoreTake(s_package_mutex, portMAX_DELAY) != pdTRUE) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    if (!s_runner.active) {
        (void)xSemaphoreGive(s_package_mutex);
        return WATCHY_PACKAGE_ERR_STATE;
    }
    runner_finish(true);
    (void)xSemaphoreGive(s_package_mutex);
    return watchy_package_session_loaded(&s_runner.session) ? WATCHY_PACKAGE_ERR_LOADER
                                                            : WATCHY_PACKAGE_OK;
}

bool watchy_packages_runner_active(void) {
    return s_runner.active;
}

bool watchy_packages_run_watchface(bool safe_mode) {
    const watchy_package_index_t *index;
    char reference[WATCHY_PACKAGE_REF_MAX + 1u];
    watchy_package_status_t status;
    if (safe_mode || watchy_packages_runtime_init() != WATCHY_PACKAGE_OK ||
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
    status = watchy_packages_runner_start(reference, false);
    if (status == WATCHY_PACKAGE_OK && watchy_packages_runner_active()) {
        status = watchy_packages_runner_render();
    }
    if (status == WATCHY_PACKAGE_OK && watchy_packages_runner_active()) {
        status = watchy_packages_runner_stop();
    }
    return status == WATCHY_PACKAGE_OK;
}

watchy_package_status_t watchy_packages_install_blob(
    const uint8_t *wpk,
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
    uint8_t *stage_bytes;
    if (status != WATCHY_PACKAGE_OK || wpk == NULL || out_package_ref == NULL) {
        return status != WATCHY_PACKAGE_OK ? status : WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    if (wpk_size > WATCHY_PACKAGE_WPK_BYTES_MAX) {
        return WATCHY_PACKAGE_ERR_LIMIT;
    }
    if (xSemaphoreTake(s_package_mutex, portMAX_DELAY) != pdTRUE) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    stage_bytes = malloc(wpk_size);
    if (stage_bytes == NULL) {
        (void)xSemaphoreGive(s_package_mutex);
        return WATCHY_PACKAGE_ERR_LIMIT;
    }
    s_install_workspace.stage_bytes = stage_bytes;
    s_install_workspace.stage_capacity = wpk_size;
    status = watchy_package_install(&s_index, &filesystem, &s_install_workspace,
                                    wpk, wpk_size, &crypto, out_package_ref);
    s_install_workspace.stage_bytes = NULL;
    s_install_workspace.stage_capacity = 0u;
    free(stage_bytes);
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

bool watchy_packages_link_smoke(void) {
    watchy_package_status_t (*install_fn)(const uint8_t *, size_t, char *) =
        watchy_packages_install_blob;
    watchy_package_status_t (*select_fn)(const char *) = watchy_packages_select_watchface;
    watchy_package_status_t (*event_fn)(const watchy_event_t *) = watchy_packages_runner_event;
    uintptr_t install_address = 0u;
    uintptr_t select_address = 0u;
    uintptr_t event_address = 0u;
    _Static_assert(sizeof(install_fn) == sizeof(install_address), "function pointer size");
    _Static_assert(sizeof(select_fn) == sizeof(select_address), "function pointer size");
    _Static_assert(sizeof(event_fn) == sizeof(event_address), "function pointer size");
    memcpy(&install_address, &install_fn, sizeof(install_address));
    memcpy(&select_address, &select_fn, sizeof(select_address));
    memcpy(&event_address, &event_fn, sizeof(event_address));
    return install_address != 0u && select_address != 0u && event_address != 0u;
}
