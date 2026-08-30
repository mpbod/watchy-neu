#ifndef WATCHY_SHELL_H
#define WATCHY_SHELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "watchy/power.h"
#include "watchy/package_runtime.h"
#include "watchy/diagnostics.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHY_SHELL_LAUNCHER_ITEMS 6u
#define WATCHY_SHELL_PACKAGE_PAGE_ITEMS 7u
#define WATCHY_SHELL_PACKAGE_LABEL_SIZE 32u
#define WATCHY_SHELL_DIAGNOSTIC_LABEL_SIZE 24u
#define WATCHY_SHELL_ERROR_MESSAGE_MAX 24u

typedef enum {
    WATCHY_SHELL_WATCHFACE = 0,
    WATCHY_SHELL_LAUNCHER,
    WATCHY_SHELL_PACKAGE_APPS,
    WATCHY_SHELL_SETTINGS,
    WATCHY_SHELL_MANUAL_TIME,
    WATCHY_SHELL_NTP_SYNC,
    WATCHY_SHELL_CONNECTIVITY,
    WATCHY_SHELL_PACKAGE_PORTAL,
    WATCHY_SHELL_DIAGNOSTICS,
    WATCHY_SHELL_ABOUT,
    WATCHY_SHELL_ERROR,
    WATCHY_SHELL_SAFE_MODE,
} watchy_shell_screen_t;

typedef enum {
    WATCHY_SHELL_INPUT_UP = 0,
    WATCHY_SHELL_INPUT_DOWN,
    WATCHY_SHELL_INPUT_MENU,
    WATCHY_SHELL_INPUT_BACK,
    WATCHY_SHELL_INPUT_IDLE,
} watchy_shell_input_t;

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
} watchy_shell_action_t;

typedef enum {
    WATCHY_SHELL_ERROR_NONE = 0,
    WATCHY_SHELL_ERROR_SETTINGS_LOAD,
    WATCHY_SHELL_ERROR_SETTINGS_SAVE,
    WATCHY_SHELL_ERROR_MANUAL_TIME,
    WATCHY_SHELL_ERROR_NTP,
    WATCHY_SHELL_ERROR_PORTAL,
    WATCHY_SHELL_ERROR_DISPLAY,
    WATCHY_SHELL_ERROR_PACKAGE,
} watchy_shell_error_t;

typedef struct {
    watchy_shell_screen_t screen;
    watchy_shell_screen_t return_screen;
    uint8_t selection;
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
} watchy_shell_t;

void watchy_shell_begin(watchy_shell_t *shell,
                        watchy_wake_cause_t wake_cause,
                        bool motion_wake_enabled,
                        bool safe_mode_requested,
                        bool package_watchface_failed);
void watchy_shell_require_manual_time(watchy_shell_t *shell, bool interactive);
void watchy_shell_input(watchy_shell_t *shell, watchy_shell_input_t input);
void watchy_shell_set_package_count(watchy_shell_t *shell, size_t package_count);
void watchy_shell_set_package_catalog(watchy_shell_t *shell,
                                     const watchy_package_catalog_t *catalog,
                                     bool index_readable);
bool watchy_shell_selected_package(const watchy_shell_t *shell, size_t *out_catalog_index);
size_t watchy_shell_package_page_start(const watchy_shell_t *shell);
bool watchy_shell_format_package_label(const watchy_package_info_t *package,
                                       size_t position,
                                       size_t total,
                                       char *out_label,
                                       size_t out_size);
void watchy_shell_set_diagnostic_count(watchy_shell_t *shell, size_t count);
size_t watchy_shell_diagnostic_page_start(const watchy_shell_t *shell);
bool watchy_shell_format_diagnostic_label(const watchy_diagnostic_entry_t *entry,
                                          char *out_label,
                                          size_t out_size);
void watchy_shell_fail(watchy_shell_t *shell, watchy_shell_error_t error);
const char *watchy_shell_error_message(const watchy_shell_t *shell);
watchy_shell_action_t watchy_shell_take_action(watchy_shell_t *shell);
bool watchy_shell_should_render_builtin(const watchy_shell_t *shell);

#ifdef __cplusplus
}
#endif

#endif
