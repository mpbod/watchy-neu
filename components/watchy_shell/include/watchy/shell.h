#ifndef WATCHY_SHELL_H
#define WATCHY_SHELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "watchy/power.h"
#include "watchy/buttons.h"
#include "watchy/package_runtime.h"
#include "watchy/diagnostics.h"
#include "watchy/transition.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHY_SHELL_LAUNCHER_ITEMS 3u
#define WATCHY_SHELL_VISIBLE_ROWS 3u
#define WATCHY_SHELL_PACKAGE_PAGE_ITEMS WATCHY_SHELL_VISIBLE_ROWS
#define WATCHY_SHELL_PACKAGE_LABEL_SIZE 32u
#define WATCHY_SHELL_DIAGNOSTIC_LABEL_SIZE 24u
#define WATCHY_SHELL_ERROR_MESSAGE_MAX 24u
#define WATCHY_SHELL_HISTORY_DEPTH 4u

typedef enum {
    WATCHY_SHELL_WATCHFACE = 0,
    WATCHY_SHELL_LAUNCHER,
    WATCHY_SHELL_PACKAGE_APPS,
    WATCHY_SHELL_SETTINGS,
    WATCHY_SHELL_TIMEZONE,
    WATCHY_SHELL_MANUAL_TIME,
    WATCHY_SHELL_NTP_SYNC,
    WATCHY_SHELL_CONNECTIVITY,
    WATCHY_SHELL_PACKAGE_PORTAL,
    WATCHY_SHELL_DIAGNOSTICS,
    WATCHY_SHELL_ABOUT,
    WATCHY_SHELL_ERROR,
    WATCHY_SHELL_SAFE_MODE,
} watchy_shell_screen_t;

#define WATCHY_SHELL_WATCHFACE_SELECTOR \
    ((watchy_shell_screen_t)(WATCHY_SHELL_SAFE_MODE + 1))

typedef enum {
    WATCHY_SHELL_INPUT_UP = 0,
    WATCHY_SHELL_INPUT_DOWN,
    WATCHY_SHELL_INPUT_MENU,
    WATCHY_SHELL_INPUT_BACK,
    WATCHY_SHELL_INPUT_IDLE,
} watchy_shell_input_t;

typedef struct {
    watchy_shell_screen_t from;
    watchy_shell_screen_t to;
    watchy_shell_input_t input;
    bool saved;
    bool sync_progress;
    bool sleep_requested;
    bool safe_mode;
    bool has_rect;
    watchy_transition_rect_t rect;
} watchy_shell_transition_context_t;

typedef enum {
    WATCHY_SHELL_PRESENT_TARGET = 0,
    WATCHY_SHELL_PRESENT_RECOVERY,
    WATCHY_SHELL_PRESENT_FAILED,
    WATCHY_SHELL_PRESENT_CANCELLED,
} watchy_shell_presentation_outcome_t;

typedef enum {
    WATCHY_SHELL_SLEEP_ENTER = 0,
    WATCHY_SHELL_SLEEP_PROCESS_INPUT,
    WATCHY_SHELL_SLEEP_FAIL_CLOSED,
} watchy_shell_sleep_route_t;

typedef struct {
    bool sleep_deferred;
    watchy_button_mask_t pending_buttons;
} watchy_shell_presentation_state_t;

typedef enum {
    WATCHY_SHELL_ACTION_NONE = 0,
    WATCHY_SHELL_ACTION_START_PORTAL_CLIENT,
    WATCHY_SHELL_ACTION_START_PORTAL_AP,
    WATCHY_SHELL_ACTION_SYNC_NTP,
    WATCHY_SHELL_ACTION_SAVE_SETTINGS,
    WATCHY_SHELL_ACTION_MANUAL_INCREMENT,
    WATCHY_SHELL_ACTION_MANUAL_DECREMENT,
    WATCHY_SHELL_ACTION_SAVE_MANUAL_TIME,
    WATCHY_SHELL_ACTION_RUN_PACKAGE,
    WATCHY_SHELL_ACTION_REMOVE_PACKAGE,
    WATCHY_SHELL_ACTION_PURGE_PACKAGES,
    WATCHY_SHELL_ACTION_NORMAL_REBOOT,
    WATCHY_SHELL_ACTION_SELECT_BUILTIN,
    WATCHY_SHELL_ACTION_SELECT_WATCHFACE,
    WATCHY_SHELL_ACTION_SAVE_TIMEZONE,
} watchy_shell_action_t;

typedef struct {
    watchy_shell_action_t action;
    bool has_package;
    size_t catalog_index;
    int32_t numeric_value;
    char package_ref[WATCHY_PACKAGE_REF_MAX + 1u];
} watchy_shell_action_request_t;

typedef enum {
    WATCHY_SHELL_ERROR_NONE = 0,
    WATCHY_SHELL_ERROR_SETTINGS_LOAD,
    WATCHY_SHELL_ERROR_SETTINGS_SAVE,
    WATCHY_SHELL_ERROR_MANUAL_TIME,
    WATCHY_SHELL_ERROR_NTP,
    WATCHY_SHELL_ERROR_PORTAL,
    WATCHY_SHELL_ERROR_DISPLAY,
    WATCHY_SHELL_ERROR_PACKAGE,
    WATCHY_SHELL_ERROR_INPUT,
} watchy_shell_error_t;

typedef struct {
    watchy_shell_screen_t screen;
    uint8_t selection;
    uint8_t view_start;
} watchy_shell_navigation_frame_t;

typedef struct {
    watchy_shell_screen_t screen;
    watchy_shell_screen_t return_screen;
    uint8_t selection;
    uint8_t view_start;
    uint8_t home_timezone_index;
    uint8_t history_depth;
    watchy_shell_navigation_frame_t history[WATCHY_SHELL_HISTORY_DEPTH];
    uint8_t app_count;
    uint8_t app_indices[WATCHY_PACKAGE_INSTALLED_MAX];
    uint8_t face_count;
    uint8_t face_indices[WATCHY_PACKAGE_INSTALLED_MAX];
    uint8_t recovery_count;
    uint8_t recovery_indices[WATCHY_PACKAGE_INSTALLED_MAX];
    bool face_quarantined[WATCHY_PACKAGE_INSTALLED_MAX];
    uint8_t active_face_selection;
    /* Compatibility view for the pre-gallery renderer; Task 5 replaces its use. */
    uint8_t package_count;
    uint8_t package_indices[WATCHY_PACKAGE_INSTALLED_MAX];
    uint8_t diagnostic_count;
    bool safe_mode;
    bool package_index_readable;
    bool package_warning;
    bool sleep_requested;
    bool editing;
    watchy_shell_error_t error;
    watchy_shell_action_t pending_action;
    bool pending_action_has_package;
    uint8_t pending_action_catalog_index;
    int32_t pending_action_numeric_value;
    char pending_action_package_ref[WATCHY_PACKAGE_REF_MAX + 1u];
} watchy_shell_t;

void watchy_shell_begin(watchy_shell_t *shell,
                        watchy_wake_cause_t wake_cause,
                        bool motion_wake_enabled,
                        bool safe_mode_requested,
                        bool package_watchface_failed);
void watchy_shell_require_manual_time(watchy_shell_t *shell, bool interactive);
void watchy_shell_input(watchy_shell_t *shell, watchy_shell_input_t input);
bool watchy_shell_transition_for_change(const watchy_shell_transition_context_t *change,
                                        watchy_transition_request_v1_t *out_request);
watchy_transition_rect_t watchy_shell_settings_confirmation_rect(uint8_t selection,
                                                                 uint8_t view_start);
watchy_transition_rect_t watchy_shell_sync_progress_rect(void);
void watchy_shell_presentation_observe(watchy_shell_presentation_state_t *state,
                                       watchy_shell_t *shell,
                                       watchy_shell_presentation_outcome_t outcome,
                                       watchy_button_mask_t cancelled_buttons);
watchy_button_mask_t watchy_shell_presentation_take_cancelled_buttons(
    watchy_shell_presentation_state_t *state);
bool watchy_shell_presentation_has_pending_input(
    const watchy_shell_presentation_state_t *state);
bool watchy_shell_presentation_allows_sleep(
    const watchy_shell_presentation_state_t *state);
bool watchy_shell_presentation_needs_post_action(
    watchy_shell_presentation_outcome_t outcome);
watchy_shell_sleep_route_t watchy_shell_sleep_route(
    watchy_shell_t *shell,
    bool input_pending,
    watchy_status_t preparation_status);
void watchy_shell_set_package_count(watchy_shell_t *shell, size_t package_count);
void watchy_shell_set_package_catalog(watchy_shell_t *shell,
                                     const watchy_package_catalog_t *catalog,
                                     bool index_readable);
bool watchy_shell_selected_package(const watchy_shell_t *shell, size_t *out_catalog_index);
size_t watchy_shell_package_page_start(const watchy_shell_t *shell);
bool watchy_shell_selected_watchface(const watchy_shell_t *shell,
                                     size_t *out_catalog_index);
size_t watchy_shell_watchface_page_start(const watchy_shell_t *shell);
bool watchy_shell_format_package_label(const watchy_package_info_t *package,
                                       size_t position,
                                       size_t total,
                                       char *out_label,
                                       size_t out_size);
void watchy_shell_set_diagnostic_count(watchy_shell_t *shell, size_t count);
size_t watchy_shell_diagnostic_page_start(const watchy_shell_t *shell);
size_t watchy_shell_visible_start(const watchy_shell_t *shell);
void watchy_shell_set_home_timezone_index(watchy_shell_t *shell, size_t index);
bool watchy_shell_format_diagnostic_label(const watchy_diagnostic_entry_t *entry,
                                          char *out_label,
                                          size_t out_size);
void watchy_shell_fail(watchy_shell_t *shell, watchy_shell_error_t error);
const char *watchy_shell_error_message(const watchy_shell_t *shell);
watchy_shell_action_t watchy_shell_take_action(watchy_shell_t *shell);
bool watchy_shell_take_action_request(watchy_shell_t *shell,
                                      watchy_shell_action_request_t *out_request);
bool watchy_shell_should_render_builtin(const watchy_shell_t *shell);

#ifdef __cplusplus
}
#endif

#endif
