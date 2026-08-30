#include "watchy/package_host.h"

#include "watchy/battery.h"
#include "watchy/buttons.h"
#include "watchy/display.h"
#include "watchy/haptics.h"
#include "watchy/motion.h"
#include "watchy/radios.h"
#include "watchy/rtc.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WATCHY_PACKAGE_STATE_QUOTA (16u * 1024u)
#define WATCHY_PACKAGE_IO_LIMIT WATCHY_PACKAGE_ASSETS_BYTES_MAX
#define WATCHY_PACKAGE_LOG_MAX 256u

static const char *TAG = "watchy_package";

static bool permitted(const watchy_package_host_context_t *context, uint32_t capability) {
    return context != NULL && (context->capabilities & capability) != 0u;
}

static bool bounded_string(const char *value, size_t maximum, size_t *out_length) {
    size_t length;
    if (value == NULL) {
        return false;
    }
    length = strnlen(value, maximum + 1u);
    if (length > maximum) {
        return false;
    }
    if (out_length != NULL) {
        *out_length = length;
    }
    return true;
}

static bool mkdirs(const char *path) {
    char copy[WATCHY_PACKAGE_HOST_PATH_MAX];
    size_t length;
    if (!bounded_string(path, sizeof(copy) - 1u, &length)) {
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

static bool parent_mkdirs(char path[WATCHY_PACKAGE_HOST_PATH_MAX]) {
    char *separator = strrchr(path, '/');
    bool result;
    if (separator == NULL) {
        return false;
    }
    *separator = '\0';
    result = mkdirs(path);
    *separator = '/';
    return result;
}

static bool resolve_storage_path(const watchy_package_host_context_t *context,
                                 const char *path,
                                 bool write,
                                 char out[WATCHY_PACKAGE_HOST_PATH_MAX]) {
    const char *relative;
    const char *root;
    int written;

    if (context == NULL || !bounded_string(path, WATCHY_PACKAGE_ASSET_PATH_MAX + 7u, NULL)) {
        return false;
    }
    if (strncmp(path, "assets/", 7u) == 0 && !write) {
        relative = path + 7u;
        root = context->package_root;
    } else if (strncmp(path, "state/", 6u) == 0) {
        relative = path + 6u;
        root = context->state_root;
    } else {
        return false;
    }
    if (!watchy_package_relative_path_valid(relative)) {
        return false;
    }
    written = snprintf(out, WATCHY_PACKAGE_HOST_PATH_MAX, "%s/%s", root, relative);
    return written >= 0 && written < (int)WATCHY_PACKAGE_HOST_PATH_MAX;
}

static bool tree_size(const char *path, const char *skip, size_t *out_size) {
    struct stat info;
    DIR *directory;
    struct dirent *entry;
    size_t total = 0u;

    if (strcmp(path, skip) == 0) {
        *out_size = 0u;
        return true;
    }
    if (stat(path, &info) != 0) {
        if (errno == ENOENT) {
            *out_size = 0u;
            return true;
        }
        return false;
    }
    if (S_ISREG(info.st_mode)) {
        *out_size = (size_t)info.st_size;
        return true;
    }
    if (!S_ISDIR(info.st_mode) || (directory = opendir(path)) == NULL) {
        return false;
    }
    while ((entry = readdir(directory)) != NULL) {
        char child[WATCHY_PACKAGE_HOST_PATH_MAX];
        size_t child_size;
        int written;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        written = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (written < 0 || written >= (int)sizeof(child) ||
            !tree_size(child, skip, &child_size) || child_size > SIZE_MAX - total) {
            closedir(directory);
            return false;
        }
        total += child_size;
    }
    if (closedir(directory) != 0) {
        return false;
    }
    *out_size = total;
    return true;
}

static watchy_canvas_t host_canvas_acquire(void *opaque) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    if (!permitted(context, WATCHY_CAP_CANVAS)) {
        return (watchy_canvas_t){0};
    }
    return watchy_display_acquire();
}

static void host_canvas_release(void *opaque, const watchy_canvas_t *canvas) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    if (!permitted(context, WATCHY_CAP_CANVAS) || canvas == NULL || canvas->pixels == NULL ||
        canvas->width != WATCHY_DISPLAY_WIDTH || canvas->height != WATCHY_DISPLAY_HEIGHT ||
        canvas->stride != WATCHY_DISPLAY_STRIDE || canvas->format != WATCHY_PIXEL_MONO) {
        return;
    }
}

static watchy_status_t host_canvas_refresh(void *opaque, watchy_refresh_mode_t mode) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    if (!permitted(context, WATCHY_CAP_CANVAS)) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    if (mode != WATCHY_REFRESH_PARTIAL && mode != WATCHY_REFRESH_FULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    return watchy_display_refresh(mode);
}

static watchy_status_t host_clock_now(void *opaque, watchy_time_t *out_time) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    if (!permitted(context, WATCHY_CAP_CLOCK)) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    return out_time != NULL ? watchy_rtc_read_local(out_time) : WATCHY_STATUS_INVALID_ARGUMENT;
}

static watchy_status_t host_clock_alarm(void *opaque, const watchy_time_t *alarm_time) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    if (!permitted(context, WATCHY_CAP_CLOCK)) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    if (alarm_time == NULL || alarm_time->minute > 59u) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    return watchy_rtc_set_minute_alarm(alarm_time->minute);
}

static bool host_input_pressed(void *opaque, watchy_button_t button) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    watchy_button_mask_t required;
    if (!permitted(context, WATCHY_CAP_INPUT)) {
        return false;
    }
    switch (button) {
        case WATCHY_BUTTON_UP: required = WATCHY_BUTTON_MASK_UP; break;
        case WATCHY_BUTTON_DOWN: required = WATCHY_BUTTON_MASK_DOWN; break;
        case WATCHY_BUTTON_CONFIRM: required = WATCHY_BUTTON_MASK_MENU; break;
        case WATCHY_BUTTON_BACK: required = WATCHY_BUTTON_MASK_BACK; break;
        default: return false;
    }
    return (watchy_buttons_sample() & required) != 0u;
}

static watchy_status_t host_motion_sample(void *opaque, watchy_motion_sample_t *out_sample) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    watchy_motion_data_t data;
    watchy_status_t status;
    if (!permitted(context, WATCHY_CAP_MOTION)) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    if (out_sample == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    status = watchy_motion_read(&data);
    if (status == WATCHY_STATUS_OK) {
        *out_sample = (watchy_motion_sample_t){
            .x_mg = data.x_mg, .y_mg = data.y_mg, .z_mg = data.z_mg, .shake = false,
        };
    }
    return status;
}

static watchy_status_t host_battery_read(void *opaque, watchy_battery_state_t *out_state) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    if (!permitted(context, WATCHY_CAP_BATTERY)) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    return out_state != NULL ? watchy_battery_read(out_state) : WATCHY_STATUS_INVALID_ARGUMENT;
}

static watchy_status_t host_haptics_pulse(void *opaque, uint16_t duration_ms, uint8_t strength) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    if (!permitted(context, WATCHY_CAP_HAPTICS)) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    if (duration_ms > WATCHY_HAPTICS_MAX_DURATION_MS) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    return watchy_haptics_pulse(duration_ms, strength);
}

static watchy_status_t host_storage_read(void *opaque,
                                         const char *path,
                                         void *buffer,
                                         uint32_t buffer_size,
                                         uint32_t *out_size) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    char resolved[WATCHY_PACKAGE_HOST_PATH_MAX];
    FILE *file;
    long length;
    if (!permitted(context, WATCHY_CAP_STORAGE)) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    if (out_size == NULL || buffer_size > WATCHY_PACKAGE_IO_LIMIT ||
        (buffer == NULL && buffer_size != 0u) || !resolve_storage_path(context, path, false, resolved)) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    file = fopen(resolved, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        (unsigned long)length > UINT32_MAX || fseek(file, 0, SEEK_SET) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return WATCHY_STATUS_INVALID_STATE;
    }
    *out_size = (uint32_t)length;
    if ((uint32_t)length > buffer_size) {
        fclose(file);
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if ((length != 0 && fread(buffer, 1u, (size_t)length, file) != (size_t)length) ||
        fclose(file) != 0) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    return WATCHY_STATUS_OK;
}

static watchy_status_t host_storage_write(void *opaque,
                                          const char *path,
                                          const void *data,
                                          uint32_t data_size) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    char resolved[WATCHY_PACKAGE_HOST_PATH_MAX];
    char temporary[WATCHY_PACKAGE_HOST_PATH_MAX];
    size_t current_size;
    int descriptor;
    int temporary_length;
    size_t written_total = 0u;

    if (!permitted(context, WATCHY_CAP_STORAGE)) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    if (data_size > WATCHY_PACKAGE_STATE_QUOTA || (data == NULL && data_size != 0u) ||
        !resolve_storage_path(context, path, true, resolved) ||
        !tree_size(context->state_root, resolved, &current_size) ||
        current_size > WATCHY_PACKAGE_STATE_QUOTA - data_size ||
        !parent_mkdirs(resolved)) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    temporary_length = snprintf(temporary, sizeof(temporary), "%s.tmp", resolved);
    if (temporary_length < 0 || temporary_length >= (int)sizeof(temporary)) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    descriptor = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (descriptor < 0) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    while (written_total < data_size) {
        const ssize_t written = write(descriptor,
                                      (const uint8_t *)data + written_total,
                                      (size_t)data_size - written_total);
        if (written <= 0) {
            close(descriptor);
            unlink(temporary);
            return WATCHY_STATUS_INVALID_STATE;
        }
        written_total += (size_t)written;
    }
    if (fsync(descriptor) != 0) {
        close(descriptor);
        unlink(temporary);
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (close(descriptor) != 0 || rename(temporary, resolved) != 0) {
        unlink(temporary);
        return WATCHY_STATUS_INVALID_STATE;
    }
    return WATCHY_STATUS_OK;
}

static bool host_network_connected(void *opaque) {
    return permitted((watchy_package_host_context_t *)opaque, WATCHY_CAP_NETWORK) &&
           watchy_wifi_state() == WATCHY_WIFI_STA_CONNECTED;
}

static bool host_bluetooth_enabled(void *opaque) {
    return permitted((watchy_package_host_context_t *)opaque, WATCHY_CAP_BLUETOOTH) &&
           watchy_ble_state() != WATCHY_BLE_STOPPED;
}

static uint32_t host_system_millis(void *opaque) {
    if (!permitted((watchy_package_host_context_t *)opaque, WATCHY_CAP_SYSTEM)) {
        return 0u;
    }
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void host_system_sleep(void *opaque, uint32_t duration_ms) {
    if (!permitted((watchy_package_host_context_t *)opaque, WATCHY_CAP_SYSTEM) ||
        duration_ms > 10000u) {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
}

static void host_system_log(void *opaque, const char *message) {
    if (!permitted((watchy_package_host_context_t *)opaque, WATCHY_CAP_SYSTEM) ||
        !bounded_string(message, WATCHY_PACKAGE_LOG_MAX, NULL)) {
        return;
    }
    ESP_LOGI(TAG, "package: %s", message);
}

watchy_package_status_t watchy_package_host_init(watchy_package_host_context_t *context,
                                                 const watchy_package_manifest_t *manifest) {
    int written;
    if (context == NULL || manifest == NULL || !watchy_package_id_valid(manifest->id)) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    memset(context, 0, sizeof(*context));
    context->capabilities = manifest->capabilities;
    written = snprintf(context->package_root, sizeof(context->package_root),
                       "/data/packages/%s/%s", manifest->id, manifest->version);
    if (written < 0 || written >= (int)sizeof(context->package_root)) {
        return WATCHY_PACKAGE_ERR_LIMIT;
    }
    written = snprintf(context->state_root, sizeof(context->state_root),
                       "/data/state/%s", manifest->id);
    if (written < 0 || written >= (int)sizeof(context->state_root) ||
        (permitted(context, WATCHY_CAP_STORAGE) && !mkdirs(context->state_root))) {
        return WATCHY_PACKAGE_ERR_FILESYSTEM;
    }

    context->canvas = (watchy_canvas_api_v1_t){context, host_canvas_acquire,
                                               host_canvas_release, host_canvas_refresh};
    context->clock = (watchy_clock_api_v1_t){context, host_clock_now, host_clock_alarm};
    context->input = (watchy_input_api_v1_t){context, host_input_pressed};
    context->motion = (watchy_motion_api_v1_t){context, host_motion_sample};
    context->battery = (watchy_battery_api_v1_t){context, host_battery_read};
    context->haptics = (watchy_haptics_api_v1_t){context, host_haptics_pulse};
    context->storage = (watchy_storage_api_v1_t){context, host_storage_read, host_storage_write};
    context->network = (watchy_network_api_v1_t){context, host_network_connected};
    context->bluetooth = (watchy_bluetooth_api_v1_t){context, host_bluetooth_enabled};
    context->system = (watchy_system_api_v1_t){context, host_system_millis,
                                               host_system_sleep, host_system_log};
    context->host = (watchy_host_caps_v1_t){
        .abi = {.major = WATCHY_ABI_V1_MAJOR, .minor = WATCHY_ABI_V1_MINOR},
        .size = sizeof(watchy_host_caps_v1_t),
        .canvas = permitted(context, WATCHY_CAP_CANVAS) ? &context->canvas : NULL,
        .clock = permitted(context, WATCHY_CAP_CLOCK) ? &context->clock : NULL,
        .input = permitted(context, WATCHY_CAP_INPUT) ? &context->input : NULL,
        .motion = permitted(context, WATCHY_CAP_MOTION) ? &context->motion : NULL,
        .battery = permitted(context, WATCHY_CAP_BATTERY) ? &context->battery : NULL,
        .haptics = permitted(context, WATCHY_CAP_HAPTICS) ? &context->haptics : NULL,
        .storage = permitted(context, WATCHY_CAP_STORAGE) ? &context->storage : NULL,
        .network = permitted(context, WATCHY_CAP_NETWORK) ? &context->network : NULL,
        .bluetooth = permitted(context, WATCHY_CAP_BLUETOOTH) ? &context->bluetooth : NULL,
        .system = permitted(context, WATCHY_CAP_SYSTEM) ? &context->system : NULL,
    };
    return WATCHY_PACKAGE_OK;
}

const watchy_host_caps_v1_t *watchy_package_host_caps(
    const watchy_package_host_context_t *context) {
    return context != NULL ? &context->host : NULL;
}
