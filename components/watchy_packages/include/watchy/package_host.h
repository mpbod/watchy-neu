#ifndef WATCHY_PACKAGE_HOST_H
#define WATCHY_PACKAGE_HOST_H

#include "watchy/packages.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHY_PACKAGE_HOST_PATH_MAX 192u
#define WATCHY_PACKAGE_STATE_NODE_MAX 32u
#define WATCHY_PACKAGE_CALLBACK_BUDGET_MS 4000u
#define WATCHY_PACKAGE_STATE_QUOTA (16u * 1024u)
#define WATCHY_PACKAGE_RADIO_REQUEST_TIMEOUT_MS 15000u
#define WATCHY_PACKAGE_UPLOAD_HEAP_RESERVE_BYTES (32u * 1024u)

typedef struct {
    watchy_request_id_t id;
    uint32_t operation;
    watchy_async_status_t status;
    uint32_t deadline_ms;
    bool occupied;
    bool started;
} watchy_package_async_slot_t;

typedef struct {
    uint32_t started_ms;
    uint32_t reserved_sleep_ms;
    bool active;
} watchy_package_callback_budget_t;

typedef struct {
    watchy_transition_request_v1_t request;
    bool occupied;
} watchy_package_transition_latch_t;

typedef enum {
    WATCHY_PACKAGE_POST_CONTINUE = 0,
    WATCHY_PACKAGE_POST_CLEAN_EXIT = 1,
    WATCHY_PACKAGE_POST_FAIL_CLEANUP = 2,
} watchy_package_post_action_t;

typedef enum {
    WATCHY_PACKAGE_PRESENT_TARGET = 0,
    WATCHY_PACKAGE_PRESENT_CANCELLED,
    WATCHY_PACKAGE_PRESENT_FAILED,
} watchy_package_presentation_outcome_t;

typedef enum {
    WATCHY_PACKAGE_STORAGE_REGULAR = 0,
    WATCHY_PACKAGE_STORAGE_DIRECTORY = 1,
    WATCHY_PACKAGE_STORAGE_LINK = 2,
    WATCHY_PACKAGE_STORAGE_OTHER = 3,
} watchy_package_storage_node_t;

typedef watchy_status_t (*watchy_package_async_execute_fn_t)(void *context,
                                                             uint32_t operation);
typedef watchy_async_status_t (*watchy_package_async_observe_fn_t)(void *context,
                                                                   uint32_t operation);
typedef bool (*watchy_package_readable_fn_t)(void *context,
                                             const void *address,
                                             size_t size);
typedef watchy_status_t (*watchy_package_present_fn_t)(
    void *context,
    watchy_refresh_mode_t mode,
    const watchy_transition_request_v1_t *request);

typedef struct {
    uint32_t capabilities;
    char package_root[WATCHY_PACKAGE_HOST_PATH_MAX];
    char state_root[WATCHY_PACKAGE_HOST_PATH_MAX];
    void *state_mutex;
    char traversal[WATCHY_PACKAGE_STATE_NODE_MAX][WATCHY_PACKAGE_HOST_PATH_MAX];
    watchy_canvas_t bound_canvas;
    size_t bound_canvas_bytes;
    bool canvas_acquired;
    watchy_package_callback_budget_t callback_budget;
    watchy_package_transition_latch_t transition;
    watchy_package_readable_fn_t request_readable;
    void *request_readable_context;
    watchy_request_id_t next_request_id;
    watchy_package_async_slot_t network_request;
    watchy_package_async_slot_t bluetooth_request;
    bool exit_requested;
    bool refresh_requested;
    watchy_refresh_mode_t refresh_mode;
    watchy_canvas_api_v1_t canvas;
    watchy_clock_api_v1_t clock;
    watchy_input_api_v1_t input;
    watchy_motion_api_v1_t motion;
    watchy_battery_api_v1_t battery;
    watchy_haptics_api_v1_t haptics;
    watchy_storage_api_v1_t storage;
    watchy_network_api_v1_t network;
    watchy_bluetooth_api_v1_t bluetooth;
    watchy_system_api_v1_t system;
    watchy_host_caps_v1_t host;
} watchy_package_host_context_t;

watchy_status_t watchy_package_async_begin(watchy_package_async_slot_t *slot,
                                           watchy_request_id_t *next_request_id,
                                           uint32_t operation,
                                           uint32_t maximum,
                                           watchy_request_id_t *out_request_id);
watchy_status_t watchy_package_async_cancel_slot(watchy_package_async_slot_t *slot,
                                                 watchy_request_id_t request_id,
                                                 watchy_package_async_execute_fn_t rollback,
                                                 void *rollback_context);
watchy_status_t watchy_package_async_status_slot(watchy_package_async_slot_t *slot,
                                                 watchy_request_id_t request_id,
                                                 watchy_async_status_t *out_status);
watchy_package_status_t watchy_package_async_pump_slot(
    watchy_package_async_slot_t *slot,
    uint32_t now_ms,
    uint32_t timeout_ms,
    watchy_package_async_execute_fn_t execute,
    watchy_package_async_observe_fn_t observe,
    watchy_package_async_execute_fn_t rollback,
    void *execute_context);
void watchy_package_callback_budget_begin(watchy_package_callback_budget_t *budget,
                                          uint32_t now_ms);
bool watchy_package_callback_budget_reserve_sleep(watchy_package_callback_budget_t *budget,
                                                  uint32_t now_ms,
                                                  uint32_t duration_ms);
bool watchy_package_callback_budget_may_feed(const watchy_package_callback_budget_t *budget,
                                             uint32_t now_ms);
void watchy_package_callback_budget_end(watchy_package_callback_budget_t *budget);
watchy_status_t watchy_package_transition_latch(
    watchy_package_transition_latch_t *latch,
    const watchy_transition_request_v1_t *request);
/* A NULL output discards the occupied request for teardown. */
bool watchy_package_transition_take(watchy_package_transition_latch_t *latch,
                                    watchy_transition_request_v1_t *out_request);
void watchy_package_transition_cleanup(watchy_package_host_context_t *context);
void watchy_package_transition_bind(watchy_package_host_context_t *context,
                                    watchy_package_readable_fn_t readable,
                                    void *readable_context);
watchy_status_t watchy_package_transition_present_after_render(
    watchy_package_transition_latch_t *latch,
    bool render_accepted,
    watchy_refresh_mode_t mode,
    watchy_package_present_fn_t present,
    void *present_context);
watchy_package_presentation_outcome_t watchy_package_classify_presentation(
    watchy_status_t status);
watchy_package_post_action_t watchy_package_post_action(bool pump_ok,
                                                        bool refresh_requested,
                                                        watchy_package_presentation_outcome_t
                                                            refresh_outcome,
                                                        bool exit_requested);
bool watchy_package_state_quota_allows(size_t current_bytes, size_t incoming_bytes);
bool watchy_package_storage_node_allowed(watchy_package_storage_node_t node,
                                         bool allow_directory);
bool watchy_package_range_within(uintptr_t allocation_start,
                                 size_t allocation_size,
                                 const void *address,
                                 size_t size);
bool watchy_package_canvas_binding_valid(const watchy_canvas_t *bound,
                                         const watchy_canvas_t *candidate,
                                         size_t bound_capacity);
bool watchy_package_upload_heap_allows(size_t request_bytes,
                                       size_t free_internal_bytes,
                                       size_t largest_internal_block);
bool watchy_package_state_temporary_name_valid(const char *name);
bool watchy_package_resolve_storage_path(const char *package_root,
                                         const char *state_root,
                                         const char *path,
                                         bool write,
                                         char *out,
                                         size_t out_size);

watchy_package_status_t watchy_package_host_init(watchy_package_host_context_t *context,
                                                 const watchy_package_manifest_t *manifest);
void watchy_package_host_deinit(watchy_package_host_context_t *context);
const watchy_host_caps_v1_t *watchy_package_host_caps(
    const watchy_package_host_context_t *context);
watchy_package_status_t watchy_package_host_pump(watchy_package_host_context_t *context);
bool watchy_package_host_take_exit(watchy_package_host_context_t *context);
bool watchy_package_host_take_refresh(watchy_package_host_context_t *context,
                                      watchy_refresh_mode_t *out_mode);

#ifdef __cplusplus
}
#endif

#endif
