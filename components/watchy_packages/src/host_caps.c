#include "watchy/package_host.h"

#include "watchy/battery.h"
#include "watchy/buttons.h"
#include "watchy/display.h"
#include "watchy/haptics.h"
#include "watchy/motion.h"
#include "watchy/radios.h"
#include "watchy/rtc.h"
#include "watchy/rtc_calendar.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define WATCHY_PACKAGE_IO_LIMIT WATCHY_PACKAGE_ASSETS_BYTES_MAX
#define WATCHY_PACKAGE_LOG_MAX 256u

static const char *TAG = "watchy_package";

static bool target_range(const void *address, size_t size, bool writable) {
    const uintptr_t start = (uintptr_t)address;
    uintptr_t cursor;
    uintptr_t end;
    if (address == NULL || size == 0u || start > UINTPTR_MAX - (size - 1u)) {
        return false;
    }
    end = start + size - 1u;
    for (cursor = start;; ++cursor) {
        const void *pointer = (const void *)cursor;
        if (writable ? !esp_ptr_in_dram(pointer)
                     : !(esp_ptr_in_dram(pointer) || esp_ptr_in_drom(pointer))) {
            return false;
        }
        if (cursor == end) {
            break;
        }
    }
    return true;
}

static bool permitted(const watchy_package_host_context_t *context, uint32_t capability) {
    return context != NULL && (context->capabilities & capability) != 0u;
}

static bool bounded_string(const char *value, size_t maximum, size_t *out_length) {
    if (value == NULL) {
        return false;
    }
    for (size_t length = 0u; length <= maximum; ++length) {
        if (!target_range(value + length, 1u, false)) {
            return false;
        }
        if (value[length] == '\0') {
            if (out_length != NULL) {
                *out_length = length;
            }
            return true;
        }
    }
    return false;
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
    if (context == NULL || !bounded_string(path, WATCHY_PACKAGE_ASSET_PATH_MAX + 7u, NULL)) {
        return false;
    }
    return watchy_package_resolve_storage_path(context->package_root, context->state_root,
                                                path, write, out,
                                                WATCHY_PACKAGE_HOST_PATH_MAX);
}

static bool tree_size(watchy_package_host_context_t *context,
                      const char *root,
                      const char *skip,
                      size_t *out_size) {
    size_t pending = 1u;
    size_t total = 0u;
    if (!bounded_string(root, WATCHY_PACKAGE_HOST_PATH_MAX - 1u, NULL)) {
        return false;
    }
    memcpy(context->traversal[0], root, strlen(root) + 1u);
    while (pending != 0u) {
        struct stat info;
        DIR *directory;
        struct dirent *entry;
        char current[WATCHY_PACKAGE_HOST_PATH_MAX];
        const char *path;
        --pending;
        memcpy(current, context->traversal[pending],
               strlen(context->traversal[pending]) + 1u);
        path = current;
        if (strcmp(path, skip) == 0) {
            continue;
        }
        /* /data is LittleFS: the backend has no symlink object or creation API,
         * so stat is no-follow by storage-policy construction. */
        if (stat(path, &info) != 0) {
            if (errno == ENOENT) {
                continue;
            }
            return false;
        }
        if (S_ISLNK(info.st_mode)) {
            return false;
        }
        if (S_ISREG(info.st_mode)) {
            if (info.st_size < 0 || (size_t)info.st_size > SIZE_MAX - total) {
                return false;
            }
            total += (size_t)info.st_size;
            continue;
        }
        if (!S_ISDIR(info.st_mode) || (directory = opendir(path)) == NULL) {
            return false;
        }
        while ((entry = readdir(directory)) != NULL) {
            int written;
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            if (pending >= WATCHY_PACKAGE_STATE_NODE_MAX) {
                (void)closedir(directory);
                return false;
            }
            written = snprintf(context->traversal[pending], WATCHY_PACKAGE_HOST_PATH_MAX,
                               "%s/%s", path, entry->d_name);
            if (written < 0 || written >= WATCHY_PACKAGE_HOST_PATH_MAX) {
                (void)closedir(directory);
                return false;
            }
            ++pending;
        }
        if (closedir(directory) != 0) {
            return false;
        }
    }
    *out_size = total;
    return true;
}

static watchy_canvas_t host_canvas_acquire(void *opaque) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    watchy_canvas_t canvas;
    if (!permitted(context, WATCHY_CAP_CANVAS)) {
        return (watchy_canvas_t){0};
    }
    canvas = watchy_display_acquire();
    if (canvas.pixels == NULL || canvas.width == 0u || canvas.height == 0u ||
        canvas.stride == 0u || canvas.rotation > 3u ||
        (canvas.format != WATCHY_PIXEL_MONO && canvas.format != WATCHY_PIXEL_GRAY4) ||
        canvas.stride > SIZE_MAX / canvas.height) {
        return (watchy_canvas_t){0};
    }
    context->bound_canvas = canvas;
    context->bound_canvas_bytes = (size_t)canvas.stride * canvas.height;
    context->canvas_acquired = true;
    return canvas;
}

static void host_canvas_release(void *opaque, const watchy_canvas_t *canvas) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    if (!permitted(context, WATCHY_CAP_CANVAS) || !context->canvas_acquired ||
        !target_range(canvas, sizeof(*canvas), false) ||
        !watchy_package_canvas_binding_valid(&context->bound_canvas, canvas,
                                             context->bound_canvas_bytes)) {
        return;
    }
    context->canvas_acquired = false;
    memset(&context->bound_canvas, 0, sizeof(context->bound_canvas));
    context->bound_canvas_bytes = 0u;
}

static watchy_status_t host_canvas_refresh(void *opaque, watchy_refresh_mode_t mode) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    if (!permitted(context, WATCHY_CAP_CANVAS)) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    if (mode != WATCHY_REFRESH_PARTIAL && mode != WATCHY_REFRESH_FULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    context->refresh_requested = true;
    context->refresh_mode = mode;
    return WATCHY_STATUS_OK;
}

static watchy_status_t host_clock_now(void *opaque, watchy_time_t *out_time) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    if (!permitted(context, WATCHY_CAP_CLOCK)) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    return target_range(out_time, sizeof(*out_time), true)
               ? watchy_rtc_read_local(out_time) : WATCHY_STATUS_INVALID_ARGUMENT;
}

static watchy_status_t host_clock_alarm(void *opaque, const watchy_time_t *alarm_time) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    if (!permitted(context, WATCHY_CAP_CLOCK)) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    if (!target_range(alarm_time, sizeof(*alarm_time), false) ||
        !watchy_calendar_valid(alarm_time)) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    return watchy_rtc_set_alarm_next_match(alarm_time);
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
    if (!target_range(out_sample, sizeof(*out_sample), true)) {
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
    return target_range(out_state, sizeof(*out_state), true)
               ? watchy_battery_read(out_state) : WATCHY_STATUS_INVALID_ARGUMENT;
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
    struct stat info;
    bool locked = false;
    if (!permitted(context, WATCHY_CAP_STORAGE)) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    if (!target_range(out_size, sizeof(*out_size), true) ||
        buffer_size > WATCHY_PACKAGE_IO_LIMIT ||
        (buffer_size != 0u && !target_range(buffer, buffer_size, true)) ||
        !resolve_storage_path(context, path, false, resolved)) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (strncmp(path, "state/", 6u) == 0) {
        if (context->state_mutex == NULL ||
            xSemaphoreTake((SemaphoreHandle_t)context->state_mutex, portMAX_DELAY) != pdTRUE) {
            return WATCHY_STATUS_INVALID_STATE;
        }
        locked = true;
    }
    if (stat(resolved, &info) != 0 || !S_ISREG(info.st_mode) || S_ISLNK(info.st_mode)) {
        if (locked) {
            (void)xSemaphoreGive((SemaphoreHandle_t)context->state_mutex);
        }
        return WATCHY_STATUS_INVALID_STATE;
    }
    file = fopen(resolved, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        (unsigned long)length > UINT32_MAX || fseek(file, 0, SEEK_SET) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        if (locked) {
            (void)xSemaphoreGive((SemaphoreHandle_t)context->state_mutex);
        }
        return WATCHY_STATUS_INVALID_STATE;
    }
    *out_size = (uint32_t)length;
    if ((uint32_t)length > buffer_size) {
        fclose(file);
        if (locked) {
            (void)xSemaphoreGive((SemaphoreHandle_t)context->state_mutex);
        }
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if ((length != 0 && fread(buffer, 1u, (size_t)length, file) != (size_t)length) ||
        fclose(file) != 0) {
        if (locked) {
            (void)xSemaphoreGive((SemaphoreHandle_t)context->state_mutex);
        }
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (locked) {
        (void)xSemaphoreGive((SemaphoreHandle_t)context->state_mutex);
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
    int descriptor = -1;
    size_t written_total = 0u;
    struct stat info;
    int stat_result;

    if (!permitted(context, WATCHY_CAP_STORAGE)) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    if (data_size > WATCHY_PACKAGE_STATE_QUOTA ||
        (data_size != 0u && !target_range(data, data_size, false)) ||
        !resolve_storage_path(context, path, true, resolved) || context->state_mutex == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (xSemaphoreTake((SemaphoreHandle_t)context->state_mutex, portMAX_DELAY) != pdTRUE) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    stat_result = stat(resolved, &info);
    if ((stat_result == 0 &&
         !watchy_package_storage_node_allowed(S_ISREG(info.st_mode)
                                                  ? WATCHY_PACKAGE_STORAGE_REGULAR
                                                  : S_ISDIR(info.st_mode)
                                                        ? WATCHY_PACKAGE_STORAGE_DIRECTORY
                                                        : S_ISLNK(info.st_mode)
                                                              ? WATCHY_PACKAGE_STORAGE_LINK
                                                              : WATCHY_PACKAGE_STORAGE_OTHER,
                                              false)) ||
        (stat_result != 0 && errno != ENOENT) ||
        !tree_size(context, context->state_root, resolved, &current_size) ||
        !watchy_package_state_quota_allows(current_size, data_size) ||
        !parent_mkdirs(resolved)) {
        (void)xSemaphoreGive((SemaphoreHandle_t)context->state_mutex);
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    for (unsigned attempt = 0u; attempt < 8u && descriptor < 0; ++attempt) {
        if (snprintf(temporary, sizeof(temporary), "%s/.watchy-state-%08lx",
                     context->state_root, (unsigned long)esp_random()) >=
            (int)sizeof(temporary)) {
            break;
        }
        descriptor = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (descriptor < 0 && errno != EEXIST) {
            break;
        }
    }
    if (descriptor < 0) {
        (void)xSemaphoreGive((SemaphoreHandle_t)context->state_mutex);
        return WATCHY_STATUS_INVALID_STATE;
    }
    while (written_total < data_size) {
        const ssize_t written = write(descriptor,
                                      (const uint8_t *)data + written_total,
                                      (size_t)data_size - written_total);
        if (written <= 0) {
            close(descriptor);
            unlink(temporary);
            (void)xSemaphoreGive((SemaphoreHandle_t)context->state_mutex);
            return WATCHY_STATUS_INVALID_STATE;
        }
        written_total += (size_t)written;
    }
    if (fsync(descriptor) != 0) {
        close(descriptor);
        unlink(temporary);
        (void)xSemaphoreGive((SemaphoreHandle_t)context->state_mutex);
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (close(descriptor) != 0 || rename(temporary, resolved) != 0) {
        unlink(temporary);
        (void)xSemaphoreGive((SemaphoreHandle_t)context->state_mutex);
        return WATCHY_STATUS_INVALID_STATE;
    }
    (void)xSemaphoreGive((SemaphoreHandle_t)context->state_mutex);
    return WATCHY_STATUS_OK;
}

static bool host_network_connected(void *opaque) {
    return permitted((watchy_package_host_context_t *)opaque, WATCHY_CAP_NETWORK) &&
           watchy_wifi_state() == WATCHY_WIFI_STA_CONNECTED;
}

static watchy_status_t async_request(watchy_package_host_context_t *context,
                                    watchy_package_async_slot_t *slot,
                                    uint32_t operation,
                                    uint32_t maximum,
                                    uint32_t capability,
                                    watchy_request_id_t *out_request_id) {
    if (!permitted(context, capability)) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    if (!target_range(out_request_id, sizeof(*out_request_id), true)) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    return watchy_package_async_begin(slot, &context->next_request_id,
                                      operation, maximum, out_request_id);
}

static watchy_status_t async_cancel(watchy_package_host_context_t *context,
                                   watchy_package_async_slot_t *slot,
                                   watchy_request_id_t request_id,
                                   uint32_t capability,
                                   watchy_package_async_execute_fn_t rollback) {
    if (!permitted(context, capability)) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    return watchy_package_async_cancel_slot(slot, request_id, rollback, context);
}

static watchy_status_t host_rollback_network(void *context, uint32_t operation) {
    (void)context;
    (void)operation;
    return watchy_wifi_stop();
}

static watchy_status_t host_rollback_bluetooth(void *context, uint32_t operation) {
    (void)context;
    (void)operation;
    return watchy_ble_stop();
}

static watchy_status_t async_status(watchy_package_host_context_t *context,
                                   watchy_package_async_slot_t *slot,
                                   watchy_request_id_t request_id,
                                   uint32_t capability,
                                   watchy_async_status_t *out_status) {
    if (!permitted(context, capability)) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    if (!target_range(out_status, sizeof(*out_status), true)) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    return watchy_package_async_status_slot(slot, request_id, out_status);
}

static watchy_status_t host_network_request(void *opaque,
                                            watchy_network_request_t operation,
                                            watchy_request_id_t *out_request_id) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    if (context == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    return async_request(context, &context->network_request, (uint32_t)operation,
                         WATCHY_NETWORK_DISCONNECT, WATCHY_CAP_NETWORK, out_request_id);
}

static watchy_status_t host_network_cancel(void *opaque, watchy_request_id_t request_id) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    if (context == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    return async_cancel(context, &context->network_request, request_id, WATCHY_CAP_NETWORK,
                        host_rollback_network);
}

static watchy_status_t host_network_status(void *opaque,
                                           watchy_request_id_t request_id,
                                           watchy_async_status_t *out_status) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    if (context == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    return async_status(context, &context->network_request, request_id,
                        WATCHY_CAP_NETWORK, out_status);
}

static bool host_bluetooth_enabled(void *opaque) {
    return permitted((watchy_package_host_context_t *)opaque, WATCHY_CAP_BLUETOOTH) &&
           watchy_ble_state() != WATCHY_BLE_STOPPED;
}

static watchy_status_t host_bluetooth_request(void *opaque,
                                              watchy_bluetooth_request_t operation,
                                              watchy_request_id_t *out_request_id) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    if (context == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    return async_request(context, &context->bluetooth_request, (uint32_t)operation,
                         WATCHY_BLUETOOTH_STOP, WATCHY_CAP_BLUETOOTH, out_request_id);
}

static watchy_status_t host_bluetooth_cancel(void *opaque, watchy_request_id_t request_id) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    if (context == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    return async_cancel(context, &context->bluetooth_request, request_id,
                        WATCHY_CAP_BLUETOOTH, host_rollback_bluetooth);
}

static watchy_status_t host_bluetooth_status(void *opaque,
                                             watchy_request_id_t request_id,
                                             watchy_async_status_t *out_status) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    if (context == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    return async_status(context, &context->bluetooth_request, request_id,
                        WATCHY_CAP_BLUETOOTH, out_status);
}

static uint32_t host_system_millis(void *opaque) {
    if (!permitted((watchy_package_host_context_t *)opaque, WATCHY_CAP_SYSTEM)) {
        return 0u;
    }
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void host_system_sleep(void *opaque, uint32_t duration_ms) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (!permitted(context, WATCHY_CAP_SYSTEM) ||
        !watchy_package_callback_budget_reserve_sleep(&context->callback_budget,
                                                      now_ms, duration_ms)) {
        return;
    }
    while (duration_ms != 0u) {
        const uint32_t slice = duration_ms > 100u ? 100u : duration_ms;
        if (!watchy_package_callback_budget_may_feed(&context->callback_budget,
                                                     (uint32_t)(esp_timer_get_time() / 1000))) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(slice));
        if (watchy_package_callback_budget_may_feed(&context->callback_budget,
                                                    (uint32_t)(esp_timer_get_time() / 1000))) {
            (void)esp_task_wdt_reset();
        }
        duration_ms -= slice;
    }
}

static watchy_status_t host_system_exit(void *opaque) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    if (!permitted(context, WATCHY_CAP_SYSTEM)) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    context->exit_requested = true;
    return WATCHY_STATUS_OK;
}

static watchy_status_t host_system_refresh(void *opaque, watchy_refresh_mode_t mode) {
    watchy_package_host_context_t *context = (watchy_package_host_context_t *)opaque;
    if (!permitted(context, WATCHY_CAP_SYSTEM)) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    if (mode != WATCHY_REFRESH_PARTIAL && mode != WATCHY_REFRESH_FULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    context->refresh_requested = true;
    context->refresh_mode = mode;
    return WATCHY_STATUS_OK;
}

static watchy_status_t host_request_transition(
    void *opaque, const watchy_transition_request_v1_t *request) {
    watchy_package_host_context_t *context = opaque;
    if (context == NULL || !context->callback_budget.active) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    return watchy_package_transition_latch(&context->transition, request);
}

static void host_system_log(void *opaque, const char *message) {
    if (!permitted((watchy_package_host_context_t *)opaque, WATCHY_CAP_SYSTEM) ||
        !bounded_string(message, WATCHY_PACKAGE_LOG_MAX, NULL)) {
        return;
    }
    ESP_LOGI(TAG, "package: %s", message);
}

static bool scrub_state_temporaries(const char *state_root) {
    DIR *directory = opendir(state_root);
    bool result = true;
    if (directory == NULL) {
        return false;
    }
    for (;;) {
        struct dirent *entry;
        char path[WATCHY_PACKAGE_HOST_PATH_MAX];
        struct stat info;
        int written;
        errno = 0;
        entry = readdir(directory);
        if (entry == NULL) {
            result = errno == 0;
            break;
        }
        if (!watchy_package_state_temporary_name_valid(entry->d_name)) {
            continue;
        }
        written = snprintf(path, sizeof(path), "%s/%s", state_root, entry->d_name);
        if (written < 0 || written >= (int)sizeof(path) || stat(path, &info) != 0 ||
            !S_ISREG(info.st_mode) || S_ISLNK(info.st_mode) || unlink(path) != 0) {
            (void)closedir(directory);
            return false;
        }
    }
    return closedir(directory) == 0 && result;
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
        (permitted(context, WATCHY_CAP_STORAGE) &&
         (!mkdirs(context->state_root) || !scrub_state_temporaries(context->state_root)))) {
        return WATCHY_PACKAGE_ERR_FILESYSTEM;
    }
    if (permitted(context, WATCHY_CAP_STORAGE)) {
        context->state_mutex = xSemaphoreCreateMutex();
        if (context->state_mutex == NULL) {
            return WATCHY_PACKAGE_ERR_STATE;
        }
    }

    context->canvas = (watchy_canvas_api_v1_t){context, host_canvas_acquire,
                                               host_canvas_release, host_canvas_refresh};
    context->clock = (watchy_clock_api_v1_t){context, host_clock_now, host_clock_alarm};
    context->input = (watchy_input_api_v1_t){context, host_input_pressed};
    context->motion = (watchy_motion_api_v1_t){context, host_motion_sample};
    context->battery = (watchy_battery_api_v1_t){context, host_battery_read};
    context->haptics = (watchy_haptics_api_v1_t){context, host_haptics_pulse};
    context->storage = (watchy_storage_api_v1_t){context, host_storage_read, host_storage_write};
    context->network = (watchy_network_api_v1_t){
        .context = context,
        .connected = host_network_connected,
        .request = host_network_request,
        .cancel = host_network_cancel,
        .status = host_network_status,
    };
    context->bluetooth = (watchy_bluetooth_api_v1_t){
        .context = context,
        .enabled = host_bluetooth_enabled,
        .request = host_bluetooth_request,
        .cancel = host_bluetooth_cancel,
        .status = host_bluetooth_status,
    };
    context->system = (watchy_system_api_v1_t){
        .context = context,
        .millis = host_system_millis,
        .sleep_ms = host_system_sleep,
        .log = host_system_log,
        .request_exit = host_system_exit,
        .request_refresh = host_system_refresh,
        .request_transition = host_request_transition,
    };
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

void watchy_package_host_deinit(watchy_package_host_context_t *context) {
    if (context == NULL) {
        return;
    }
    if (context->state_mutex != NULL) {
        vSemaphoreDelete((SemaphoreHandle_t)context->state_mutex);
    }
    (void)watchy_package_transition_take(&context->transition, NULL);
    (void)watchy_radios_stop_all();
    memset(context, 0, sizeof(*context));
}

static watchy_status_t host_execute_network(void *context, uint32_t operation) {
    (void)context;
    return operation == WATCHY_NETWORK_CONNECT ? watchy_wifi_start_stored_sta()
                                                : watchy_wifi_stop();
}

static watchy_status_t host_execute_bluetooth(void *context, uint32_t operation) {
    (void)context;
    return operation == WATCHY_BLUETOOTH_START ? watchy_ble_start()
                                                : watchy_ble_stop();
}

static watchy_async_status_t host_observe_network(void *context, uint32_t operation) {
    const watchy_wifi_state_t state = watchy_wifi_state();
    (void)context;
    if (operation == WATCHY_NETWORK_CONNECT) {
        if (state == WATCHY_WIFI_STA_CONNECTED) {
            return (watchy_async_status_t){WATCHY_ASYNC_SUCCEEDED, WATCHY_STATUS_OK};
        }
        if (state == WATCHY_WIFI_STOPPED || state == WATCHY_WIFI_AP_RUNNING ||
            state == WATCHY_WIFI_STOPPING) {
            return (watchy_async_status_t){WATCHY_ASYNC_FAILED,
                                           WATCHY_STATUS_INVALID_STATE};
        }
    } else if (state == WATCHY_WIFI_STOPPED) {
        return (watchy_async_status_t){WATCHY_ASYNC_SUCCEEDED, WATCHY_STATUS_OK};
    }
    return (watchy_async_status_t){WATCHY_ASYNC_PENDING, WATCHY_STATUS_INVALID_STATE};
}

static watchy_async_status_t host_observe_bluetooth(void *context, uint32_t operation) {
    const watchy_ble_state_t state = watchy_ble_state();
    (void)context;
    if (operation == WATCHY_BLUETOOTH_START) {
        if (state == WATCHY_BLE_HOST_RUNNING ||
            state == WATCHY_BLE_DIAGNOSTICS_ADVERTISING) {
            return (watchy_async_status_t){WATCHY_ASYNC_SUCCEEDED, WATCHY_STATUS_OK};
        }
        if (state == WATCHY_BLE_STOPPED || state == WATCHY_BLE_STOPPING) {
            return (watchy_async_status_t){WATCHY_ASYNC_FAILED,
                                           WATCHY_STATUS_INVALID_STATE};
        }
    } else if (state == WATCHY_BLE_STOPPED) {
        return (watchy_async_status_t){WATCHY_ASYNC_SUCCEEDED, WATCHY_STATUS_OK};
    }
    return (watchy_async_status_t){WATCHY_ASYNC_PENDING, WATCHY_STATUS_INVALID_STATE};
}

watchy_package_status_t watchy_package_host_pump(watchy_package_host_context_t *context) {
    watchy_package_status_t status;
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (context == NULL) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    status = watchy_package_async_pump_slot(
        &context->network_request, now, WATCHY_PACKAGE_RADIO_REQUEST_TIMEOUT_MS,
        host_execute_network, host_observe_network, host_rollback_network, context);
    return status == WATCHY_PACKAGE_OK
               ? watchy_package_async_pump_slot(
                    &context->bluetooth_request, now,
                    WATCHY_PACKAGE_RADIO_REQUEST_TIMEOUT_MS, host_execute_bluetooth,
                    host_observe_bluetooth, host_rollback_bluetooth, context)
               : status;
}

bool watchy_package_host_take_exit(watchy_package_host_context_t *context) {
    const bool requested = context != NULL && context->exit_requested;
    if (context != NULL) {
        context->exit_requested = false;
    }
    return requested;
}

bool watchy_package_host_take_refresh(watchy_package_host_context_t *context,
                                      watchy_refresh_mode_t *out_mode) {
    const bool requested = context != NULL && context->refresh_requested;
    if (requested && out_mode != NULL) {
        *out_mode = context->refresh_mode;
    }
    if (context != NULL) {
        context->refresh_requested = false;
    }
    return requested;
}

const watchy_host_caps_v1_t *watchy_package_host_caps(
    const watchy_package_host_context_t *context) {
    return context != NULL ? &context->host : NULL;
}
