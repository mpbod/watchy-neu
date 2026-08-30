#include "watchy/package_runtime.h"

#include "watchy/display.h"
#include "watchy/package_host.h"
#include "watchy/storage.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_dlfcn.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "mbedtls/sha256.h"
#include "nvs.h"

#define WATCHY_IDF_PATH_MAX 256u
#define WATCHY_PACKAGE_NVS_NAMESPACE "watchy_pkg"
#define WATCHY_PACKAGE_NVS_INDEX_KEY "index"

static watchy_package_index_manager_t s_index;
static bool s_initialized;

static watchy_index_store_result_t idf_index_load(void *context,
                                                  watchy_package_index_t *out_index) {
    nvs_handle_t handle = 0u;
    size_t size = sizeof(*out_index);
    esp_err_t error;
    (void)context;
    error = nvs_open(WATCHY_PACKAGE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        return WATCHY_INDEX_STORE_ERROR;
    }
    error = nvs_get_blob(handle, WATCHY_PACKAGE_NVS_INDEX_KEY, out_index, &size);
    nvs_close(handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return WATCHY_INDEX_STORE_NOT_FOUND;
    }
    return error == ESP_OK && size == sizeof(*out_index) ? WATCHY_INDEX_STORE_OK
                                                         : WATCHY_INDEX_STORE_ERROR;
}

static bool idf_index_save(void *context, const watchy_package_index_t *index) {
    nvs_handle_t handle = 0u;
    esp_err_t error;
    (void)context;
    error = nvs_open(WATCHY_PACKAGE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_set_blob(handle, WATCHY_PACKAGE_NVS_INDEX_KEY, index, sizeof(*index));
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
    mbedtls_sha256_context sha;
    int result;
    (void)context;
    mbedtls_sha256_init(&sha);
    result = mbedtls_sha256_starts(&sha, 0);
    for (size_t index = 0u; result == 0 && index < region_count; ++index) {
        result = mbedtls_sha256_update(&sha, regions[index].bytes, regions[index].size);
    }
    if (result == 0) {
        result = mbedtls_sha256_finish(&sha, out_digest);
    }
    mbedtls_sha256_free(&sha);
    return result == 0;
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

static bool idf_write_file(void *context,
                           const char *path,
                           const uint8_t *bytes,
                           size_t size,
                           bool read_only) {
    char mutable_path[WATCHY_IDF_PATH_MAX];
    size_t length;
    size_t offset = 0u;
    int descriptor;
    (void)context;
    if (path == NULL || (bytes == NULL && size != 0u) ||
        (length = strnlen(path, sizeof(mutable_path))) == sizeof(mutable_path)) {
        return false;
    }
    memcpy(mutable_path, path, length + 1u);
    if (!idf_parent_mkdirs(mutable_path)) {
        return false;
    }
    descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, read_only ? 0400 : 0600);
    if (descriptor < 0) {
        return false;
    }
    while (offset < size) {
        const ssize_t written = write(descriptor, bytes + offset, size - offset);
        if (written <= 0) {
            close(descriptor);
            unlink(path);
            return false;
        }
        offset += (size_t)written;
    }
    if (fchmod(descriptor, read_only ? 0400 : 0600) != 0 || fsync(descriptor) != 0) {
        close(descriptor);
        unlink(path);
        return false;
    }
    if (close(descriptor) != 0) {
        unlink(path);
        return false;
    }
    return true;
}

static bool idf_sync_tree(void *context, const char *path) {
    struct stat info;
    DIR *directory;
    struct dirent *entry;
    (void)context;
    if (stat(path, &info) != 0) {
        return false;
    }
    if (S_ISREG(info.st_mode)) {
        const int descriptor = open(path, O_RDONLY);
        bool result;
        if (descriptor < 0) {
            return false;
        }
        result = fsync(descriptor) == 0;
        result = close(descriptor) == 0 && result;
        return result;
    }
    if (!S_ISDIR(info.st_mode) || (directory = opendir(path)) == NULL) {
        return false;
    }
    while ((entry = readdir(directory)) != NULL) {
        char child[WATCHY_IDF_PATH_MAX];
        int written;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        written = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (written < 0 || written >= (int)sizeof(child) || !idf_sync_tree(NULL, child)) {
            closedir(directory);
            return false;
        }
    }
    return closedir(directory) == 0;
}

static bool idf_rename_atomic(void *context, const char *source, const char *destination) {
    (void)context;
    return watchy_storage_atomic_rename(source, destination) == WATCHY_STATUS_OK;
}

static bool idf_remove_tree(void *context, const char *path) {
    struct stat info;
    DIR *directory;
    struct dirent *entry;
    (void)context;
    if (stat(path, &info) != 0) {
        return errno == ENOENT;
    }
    if (S_ISREG(info.st_mode)) {
        return unlink(path) == 0;
    }
    if (!S_ISDIR(info.st_mode) || (directory = opendir(path)) == NULL) {
        return false;
    }
    while ((entry = readdir(directory)) != NULL) {
        char child[WATCHY_IDF_PATH_MAX];
        int written;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        written = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (written < 0 || written >= (int)sizeof(child) || !idf_remove_tree(NULL, child)) {
            closedir(directory);
            return false;
        }
    }
    return closedir(directory) == 0 && rmdir(path) == 0;
}

static void *idf_dlopen(void *context, const char *path, int mode) {
    (void)context;
    return dlopen(path, mode);
}

static void *idf_dlsym(void *context, void *handle, const char *name) {
    (void)context;
    return dlsym(handle, name);
}

static int idf_dlclose(void *context, void *handle) {
    (void)context;
    return dlclose(handle);
}

static void watchdog_kick(void *context) {
    (void)context;
    (void)esp_task_wdt_reset();
}

static bool read_manifest(const char *path, watchy_package_manifest_t *out_manifest) {
    uint8_t *bytes;
    long size;
    FILE *file = fopen(path, "rb");
    watchy_package_status_t status;
    if (file == NULL || fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) <= 0 ||
        size > (long)WATCHY_PACKAGE_MANIFEST_BYTES_MAX || fseek(file, 0, SEEK_SET) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return false;
    }
    bytes = malloc((size_t)size);
    if (bytes == NULL) {
        fclose(file);
        return false;
    }
    if (fread(bytes, 1u, (size_t)size, file) != (size_t)size || fclose(file) != 0) {
        free(bytes);
        return false;
    }
    status = watchy_package_manifest_parse(bytes, (size_t)size, out_manifest);
    free(bytes);
    return status == WATCHY_PACKAGE_OK;
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

watchy_package_status_t watchy_packages_runtime_init(void) {
    static const watchy_package_index_store_t store = {
        .load = idf_index_load,
        .save = idf_index_save,
        .context = NULL,
    };
    watchy_package_status_t status;
    if (s_initialized) {
        return WATCHY_PACKAGE_OK;
    }
    status = watchy_package_index_init(&s_index, &store);
    if (status == WATCHY_PACKAGE_OK) {
        const esp_err_t watchdog_status = esp_task_wdt_status(NULL);
        if (watchdog_status == ESP_ERR_NOT_FOUND) {
            (void)esp_task_wdt_add(NULL);
        }
        s_initialized = true;
    }
    return status;
}

static void record_runtime_failure(const char *package_ref, bool pending) {
    (void)watchy_package_finish_attempt(&s_index, package_ref, false);
    if (pending) {
        (void)watchy_package_rollback_pending(&s_index, package_ref);
    }
}

bool watchy_packages_run_watchface(bool safe_mode) {
    static const watchy_package_loader_api_t loader = {
        .open = idf_dlopen, .symbol = idf_dlsym, .close = idf_dlclose, .context = NULL,
    };
    static const watchy_package_watchdog_api_t watchdog = {
        .before_callback = watchdog_kick, .after_callback = watchdog_kick, .context = NULL,
    };
    const watchy_package_index_t *index;
    const char *package_ref;
    char reference[WATCHY_PACKAGE_REF_MAX + 1u];
    char identifier[WATCHY_PACKAGE_ID_MAX + 1u];
    char version[WATCHY_PACKAGE_VERSION_MAX + 1u];
    char manifest_path[WATCHY_IDF_PATH_MAX];
    char elf_path[WATCHY_IDF_PATH_MAX];
    static watchy_package_manifest_t manifest;
    static watchy_package_host_context_t host;
    static watchy_package_session_t session;
    watchy_canvas_t canvas;
    watchy_refresh_mode_t mode = WATCHY_REFRESH_PARTIAL;
    bool pending;

    if (safe_mode || watchy_packages_runtime_init() != WATCHY_PACKAGE_OK) {
        return false;
    }
    index = watchy_package_index_snapshot(&s_index);
    pending = index->pending_watchface[0] != '\0';
    package_ref = pending ? index->pending_watchface : index->active_watchface;
    if (package_ref[0] == '\0' || watchy_package_is_quarantined(&s_index, package_ref) ||
        strnlen(package_ref, sizeof(reference)) == sizeof(reference)) {
        return false;
    }
    memcpy(reference, package_ref, strlen(package_ref) + 1u);
    if (!split_package_ref(reference, identifier, version) ||
        snprintf(manifest_path, sizeof(manifest_path), "/data/packages/%s/%s/manifest.json",
                 identifier, version) >= (int)sizeof(manifest_path) ||
        snprintf(elf_path, sizeof(elf_path), "packages/%s/%s/package.so",
                 identifier, version) >= (int)sizeof(elf_path) ||
        !read_manifest(manifest_path, &manifest) || strcmp(manifest.id, identifier) != 0 ||
        strcmp(manifest.version, version) != 0 || manifest.type != WATCHY_PACKAGE_TYPE_WATCHFACE ||
        (manifest.capabilities & WATCHY_CAP_CANVAS) == 0u ||
        watchy_package_begin_attempt(&s_index, reference, false) != WATCHY_PACKAGE_OK) {
        if (pending) {
            (void)watchy_package_rollback_pending(&s_index, reference);
        }
        return false;
    }
    if (watchy_package_host_init(&host, &manifest) != WATCHY_PACKAGE_OK ||
        watchy_package_session_init(&session, &loader, &watchdog) != WATCHY_PACKAGE_OK ||
        watchy_package_session_load(&session, elf_path, &manifest,
                                    watchy_package_host_caps(&host), false, false) != WATCHY_PACKAGE_OK ||
        watchy_package_session_start(&session) != WATCHY_PACKAGE_OK) {
        record_runtime_failure(reference, pending);
        return false;
    }
    canvas = watchy_display_acquire();
    if (watchy_package_session_render(&session, &canvas, &mode) != WATCHY_PACKAGE_OK) {
        record_runtime_failure(reference, pending);
        return false;
    }
    if (pending && watchy_package_promote_pending(&s_index, reference) != WATCHY_PACKAGE_OK) {
        (void)watchy_package_session_stop(&session);
        record_runtime_failure(reference, true);
        return false;
    }
    if (watchy_display_refresh(mode) != WATCHY_STATUS_OK ||
        watchy_package_session_stop(&session) != WATCHY_PACKAGE_OK ||
        watchy_package_finish_attempt(&s_index, reference, true) != WATCHY_PACKAGE_OK) {
        record_runtime_failure(reference, false);
        return false;
    }
    return true;
}

watchy_package_status_t watchy_packages_install_blob(
    const uint8_t *wpk,
    size_t wpk_size,
    char out_package_ref[WATCHY_PACKAGE_REF_MAX + 1u]) {
    static const watchy_crypto_api_t crypto = {.sha256 = idf_sha256, .context = NULL};
    static const watchy_package_fs_api_t filesystem = {
        .write_file = idf_write_file,
        .mkdirs = idf_mkdirs,
        .sync_tree = idf_sync_tree,
        .rename_atomic = idf_rename_atomic,
        .remove_tree = idf_remove_tree,
        .context = NULL,
    };
    watchy_package_status_t status = watchy_packages_runtime_init();
    return status == WATCHY_PACKAGE_OK
               ? watchy_package_install(&s_index, &filesystem, wpk, wpk_size, &crypto, out_package_ref)
               : status;
}

watchy_package_status_t watchy_packages_select_watchface(const char *package_ref) {
    watchy_package_status_t status = watchy_packages_runtime_init();
    return status == WATCHY_PACKAGE_OK ? watchy_package_select_watchface(&s_index, package_ref)
                                       : status;
}
