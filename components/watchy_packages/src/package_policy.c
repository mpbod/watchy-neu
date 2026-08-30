#include "watchy/package_host.h"
#include "watchy/package_runtime.h"

#include <string.h>

bool watchy_package_transaction_name_valid(const char *name) {
    static const char hex[] = "0123456789abcdef";
    const size_t prefix_size = sizeof(WATCHY_PACKAGE_TRANSACTION_PREFIX) - 1u;
    if (name == NULL || strnlen(name, prefix_size + 9u) != prefix_size + 8u ||
        memcmp(name, WATCHY_PACKAGE_TRANSACTION_PREFIX, prefix_size) != 0) {
        return false;
    }
    for (size_t index = prefix_size; index < prefix_size + 8u; ++index) {
        if (strchr(hex, name[index]) == NULL) {
            return false;
        }
    }
    return true;
}

bool watchy_package_run_watchface_cycle(const watchy_package_watchface_runner_t *runner) {
    watchy_package_status_t status;
    bool rendered = false;
    if (runner == NULL || runner->start == NULL || runner->active == NULL ||
        runner->render == NULL || runner->stop == NULL) {
        return false;
    }
    status = runner->start(runner->context);
    if (status == WATCHY_PACKAGE_OK && runner->active(runner->context)) {
        status = runner->render(runner->context);
        rendered = status == WATCHY_PACKAGE_OK;
    }
    if (runner->active(runner->context)) {
        const watchy_package_status_t stop_status = runner->stop(runner->context);
        if (status == WATCHY_PACKAGE_OK) {
            status = stop_status;
        }
    }
    return rendered && status == WATCHY_PACKAGE_OK;
}

watchy_package_status_t watchy_package_upload_finalize_status(bool active,
                                                              bool output_valid,
                                                              bool content_complete,
                                                              bool sync_ok,
                                                              bool close_ok) {
    if (!active || !output_valid) return WATCHY_PACKAGE_ERR_ARGUMENT;
    if (!content_complete) return WATCHY_PACKAGE_ERR_WPK;
    if (!sync_ok || !close_ok) return WATCHY_PACKAGE_ERR_FILESYSTEM;
    return WATCHY_PACKAGE_OK;
}

watchy_package_reconcile_action_t watchy_package_reconcile_version(const char *name,
                                                                    bool indexed) {
    if (watchy_package_transaction_name_valid(name)) {
        return WATCHY_PACKAGE_RECONCILE_REMOVE_TRANSACTION;
    }
    if (!watchy_package_version_valid(name)) {
        return WATCHY_PACKAGE_RECONCILE_REMOVE_INVALID;
    }
    return indexed ? WATCHY_PACKAGE_RECONCILE_KEEP
                   : WATCHY_PACKAGE_RECONCILE_REMOVE_UNINDEXED;
}

watchy_package_status_t watchy_package_watchdog_ensure_current(
    const watchy_package_watchdog_api_t *watchdog) {
    return watchdog != NULL && watchdog->ensure_current != NULL &&
                   watchdog->ensure_current(watchdog->context)
               ? WATCHY_PACKAGE_OK : WATCHY_PACKAGE_ERR_STATE;
}

watchy_status_t watchy_package_async_begin(watchy_package_async_slot_t *slot,
                                           watchy_request_id_t *next_request_id,
                                           uint32_t operation,
                                           uint32_t maximum,
                                           watchy_request_id_t *out_request_id) {
    if (slot == NULL || next_request_id == NULL || out_request_id == NULL ||
        operation > maximum || (slot->occupied && slot->status.state == WATCHY_ASYNC_PENDING)) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (++*next_request_id == 0u) {
        ++*next_request_id;
    }
    *slot = (watchy_package_async_slot_t){
        .id = *next_request_id,
        .operation = operation,
        .status = {.state = WATCHY_ASYNC_PENDING, .result = WATCHY_STATUS_INVALID_STATE},
        .occupied = true,
    };
    *out_request_id = slot->id;
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_package_async_cancel_slot(watchy_package_async_slot_t *slot,
                                                 watchy_request_id_t request_id) {
    if (slot == NULL || !slot->occupied || slot->id != request_id ||
        slot->status.state != WATCHY_ASYNC_PENDING) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    slot->status.state = WATCHY_ASYNC_CANCELLED;
    slot->status.result = WATCHY_STATUS_OK;
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_package_async_status_slot(const watchy_package_async_slot_t *slot,
                                                 watchy_request_id_t request_id,
                                                 watchy_async_status_t *out_status) {
    if (slot == NULL || out_status == NULL || !slot->occupied || slot->id != request_id) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    *out_status = slot->status;
    return WATCHY_STATUS_OK;
}

watchy_package_status_t watchy_package_async_pump_slot(
    watchy_package_async_slot_t *slot,
    watchy_package_async_execute_fn_t execute,
    void *execute_context) {
    watchy_status_t status;
    if (slot == NULL || execute == NULL) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    if (!slot->occupied || slot->status.state != WATCHY_ASYNC_PENDING) {
        return WATCHY_PACKAGE_OK;
    }
    status = execute(execute_context, slot->operation);
    slot->status.result = status;
    slot->status.state = status == WATCHY_STATUS_OK ? WATCHY_ASYNC_SUCCEEDED
                                                    : WATCHY_ASYNC_FAILED;
    return WATCHY_PACKAGE_OK;
}

void watchy_package_callback_budget_begin(watchy_package_callback_budget_t *budget,
                                          uint32_t now_ms) {
    if (budget != NULL) {
        *budget = (watchy_package_callback_budget_t){
            .started_ms = now_ms,
            .reserved_sleep_ms = 0u,
            .active = true,
        };
    }
}

bool watchy_package_callback_budget_reserve_sleep(watchy_package_callback_budget_t *budget,
                                                  uint32_t now_ms,
                                                  uint32_t duration_ms) {
    uint32_t used;
    if (budget == NULL || !budget->active) {
        return false;
    }
    used = now_ms - budget->started_ms;
    if (used < budget->reserved_sleep_ms) {
        used = budget->reserved_sleep_ms;
    }
    if (used > WATCHY_PACKAGE_CALLBACK_BUDGET_MS ||
        duration_ms > WATCHY_PACKAGE_CALLBACK_BUDGET_MS - used) {
        return false;
    }
    budget->reserved_sleep_ms = used + duration_ms;
    return true;
}

bool watchy_package_callback_budget_may_feed(const watchy_package_callback_budget_t *budget,
                                             uint32_t now_ms) {
    return budget != NULL && budget->active &&
           now_ms - budget->started_ms <= WATCHY_PACKAGE_CALLBACK_BUDGET_MS &&
           budget->reserved_sleep_ms <= WATCHY_PACKAGE_CALLBACK_BUDGET_MS;
}

void watchy_package_callback_budget_end(watchy_package_callback_budget_t *budget) {
    if (budget != NULL) {
        memset(budget, 0, sizeof(*budget));
    }
}

watchy_package_post_action_t watchy_package_post_action(bool pump_ok,
                                                        bool refresh_requested,
                                                        bool refresh_ok,
                                                        bool exit_requested) {
    if (!pump_ok || (refresh_requested && !refresh_ok)) {
        return WATCHY_PACKAGE_POST_FAIL_CLEANUP;
    }
    return exit_requested ? WATCHY_PACKAGE_POST_CLEAN_EXIT
                          : WATCHY_PACKAGE_POST_CONTINUE;
}

bool watchy_package_state_quota_allows(size_t current_bytes, size_t incoming_bytes) {
    return current_bytes <= WATCHY_PACKAGE_STATE_QUOTA &&
           incoming_bytes <= WATCHY_PACKAGE_STATE_QUOTA - current_bytes;
}

bool watchy_package_storage_node_allowed(watchy_package_storage_node_t node,
                                         bool allow_directory) {
    return node == WATCHY_PACKAGE_STORAGE_REGULAR ||
           (allow_directory && node == WATCHY_PACKAGE_STORAGE_DIRECTORY);
}

bool watchy_package_range_within(uintptr_t allocation_start,
                                 size_t allocation_size,
                                 const void *address,
                                 size_t size) {
    const uintptr_t start = (uintptr_t)address;
    uintptr_t allocation_end;
    uintptr_t end;
    if (allocation_size == 0u || size == 0u ||
        allocation_start > UINTPTR_MAX - allocation_size || start > UINTPTR_MAX - size) {
        return false;
    }
    allocation_end = allocation_start + allocation_size;
    end = start + size;
    return start >= allocation_start && end <= allocation_end;
}

bool watchy_package_canvas_binding_valid(const watchy_canvas_t *bound,
                                         const watchy_canvas_t *candidate,
                                         size_t bound_capacity) {
    size_t required;
    uint16_t minimum_stride;
    if (bound == NULL || candidate == NULL || bound->pixels == NULL || bound->width == 0u ||
        bound->height == 0u || bound->stride == 0u || bound->rotation > 3u ||
        (bound->format != WATCHY_PIXEL_MONO && bound->format != WATCHY_PIXEL_GRAY4)) {
        return false;
    }
    minimum_stride = bound->format == WATCHY_PIXEL_MONO
                         ? (uint16_t)((bound->width + 7u) / 8u)
                         : (uint16_t)((bound->width + 1u) / 2u);
    if (bound->stride < minimum_stride || bound->stride > SIZE_MAX / bound->height) {
        return false;
    }
    required = (size_t)bound->stride * bound->height;
    return required <= bound_capacity && candidate->pixels == bound->pixels &&
           candidate->width == bound->width && candidate->height == bound->height &&
           candidate->stride == bound->stride && candidate->rotation == bound->rotation &&
           candidate->format == bound->format;
}
