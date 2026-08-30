#include "watchy/storage.h"

#include <stdio.h>
#include <string.h>

#include "esp_littlefs.h"
#include "nvs_flash.h"

#define WATCHY_STORAGE_PARTITION "littlefs"

static bool s_ready;

static bool path_is_on_data(const char *path) {
    const size_t base_length = strlen(WATCHY_STORAGE_BASE_PATH);
    return path != NULL && strncmp(path, WATCHY_STORAGE_BASE_PATH, base_length) == 0 &&
           path[base_length] == '/';
}

watchy_status_t watchy_storage_init(void) {
    esp_err_t error = nvs_flash_init();
    esp_vfs_littlefs_conf_t config = {
        .base_path = WATCHY_STORAGE_BASE_PATH,
        .partition_label = WATCHY_STORAGE_PARTITION,
        .format_if_mount_failed = true,
        .read_only = false,
        .dont_mount = false,
        .grow_on_mount = false,
    };

    if (s_ready) {
        return WATCHY_STATUS_OK;
    }
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        error = nvs_flash_erase();
        if (error == ESP_OK) {
            error = nvs_flash_init();
        }
    }
    if (error != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (esp_vfs_littlefs_register(&config) != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    s_ready = true;
    return WATCHY_STATUS_OK;
}

bool watchy_storage_ready(void) {
    return s_ready;
}

watchy_status_t watchy_storage_atomic_rename(const char *source, const char *destination) {
    if (!s_ready) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (!path_is_on_data(source) || !path_is_on_data(destination)) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    return rename(source, destination) == 0 ? WATCHY_STATUS_OK : WATCHY_STATUS_INVALID_STATE;
}

watchy_status_t watchy_storage_space(size_t *out_total_bytes, size_t *out_free_bytes) {
    size_t total;
    size_t used;
    if (!s_ready || out_total_bytes == NULL || out_free_bytes == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (esp_littlefs_info(WATCHY_STORAGE_PARTITION, &total, &used) != ESP_OK || used > total) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    *out_total_bytes = total;
    *out_free_bytes = total - used;
    return WATCHY_STATUS_OK;
}

void watchy_storage_deinit(void) {
    if (s_ready) {
        esp_vfs_littlefs_unregister(WATCHY_STORAGE_PARTITION);
    }
    s_ready = false;
}
