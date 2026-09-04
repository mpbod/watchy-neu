#include "watchy/shell.h"
#include "watchy/settings.h"

#include <stdio.h>
#include <string.h>

static char s_catalog_package_refs[WATCHY_PACKAGE_INSTALLED_MAX]
                                  [WATCHY_PACKAGE_REF_MAX + 1u];
static const watchy_shell_t *s_catalog_owner;

static uint8_t item_count(const watchy_shell_t *shell) {
    if (shell->screen == WATCHY_SHELL_WATCHFACE_SELECTOR) {
        return shell->face_count;
    }
    switch (shell->screen) {
    case WATCHY_SHELL_LAUNCHER:
        return WATCHY_SHELL_LAUNCHER_ITEMS;
    case WATCHY_SHELL_SETTINGS:
        return 11u;
    case WATCHY_SHELL_TIMEZONE:
        return (uint8_t)watchy_settings_timezone_count();
    case WATCHY_SHELL_SAFE_MODE:
        return 3u;
    case WATCHY_SHELL_PACKAGE_APPS:
        if (shell->safe_mode) {
            return shell->recovery_count == 0u ? 1u : shell->recovery_count;
        }
        return shell->app_count == 0u ? 1u : shell->app_count;
    case WATCHY_SHELL_MANUAL_TIME:
        return 5u;
    case WATCHY_SHELL_PACKAGE_PORTAL:
        return 2u;
    case WATCHY_SHELL_DIAGNOSTICS:
        return shell->diagnostic_count == 0u ? 1u : shell->diagnostic_count;
    default:
        return 1u;
    }
}

static void clear_pending_action(watchy_shell_t *shell) {
    shell->pending_action = WATCHY_SHELL_ACTION_NONE;
    shell->pending_action_has_package = false;
    shell->pending_action_catalog_index = 0u;
    shell->pending_action_numeric_value = 0;
    memset(shell->pending_action_package_ref, 0, sizeof(shell->pending_action_package_ref));
}

static void queue_action(watchy_shell_t *shell, watchy_shell_action_t action) {
    clear_pending_action(shell);
    shell->pending_action = action;
}

static void queue_numeric_action(watchy_shell_t *shell,
                                 watchy_shell_action_t action,
                                 int32_t value) {
    clear_pending_action(shell);
    shell->pending_action = action;
    shell->pending_action_numeric_value = value;
}

static bool queue_package_action(watchy_shell_t *shell,
                                 watchy_shell_action_t action,
                                 uint8_t catalog_index) {
    const char *package_ref;
    if (s_catalog_owner != shell || catalog_index >= WATCHY_PACKAGE_INSTALLED_MAX) {
        return false;
    }
    package_ref = s_catalog_package_refs[catalog_index];
    if (package_ref[0] == '\0') {
        return false;
    }
    clear_pending_action(shell);
    shell->pending_action = action;
    shell->pending_action_has_package = true;
    shell->pending_action_catalog_index = catalog_index;
    memcpy(shell->pending_action_package_ref, package_ref,
           strlen(package_ref) + 1u);
    return true;
}

static void sync_package_compatibility_view(watchy_shell_t *shell) {
    const uint8_t count = shell->safe_mode ? shell->recovery_count : shell->app_count;
    const uint8_t *indices = shell->safe_mode ? shell->recovery_indices : shell->app_indices;
    shell->package_count = count;
    memset(shell->package_indices, 0, sizeof(shell->package_indices));
    if (count != 0u) {
        memcpy(shell->package_indices, indices, count);
    }
}

static void sync_return_screen(watchy_shell_t *shell) {
    if (shell->history_depth != 0u) {
        shell->return_screen = shell->history[shell->history_depth - 1u].screen;
    }
}

static void push_frame(watchy_shell_t *shell,
                       watchy_shell_screen_t screen,
                       uint8_t selection,
                       uint8_t view_start) {
    if (shell->history_depth == WATCHY_SHELL_HISTORY_DEPTH) {
        memmove(&shell->history[0], &shell->history[1],
                sizeof(shell->history[0]) * (WATCHY_SHELL_HISTORY_DEPTH - 1u));
        --shell->history_depth;
    }
    shell->history[shell->history_depth++] =
        (watchy_shell_navigation_frame_t){screen, selection, view_start};
    sync_return_screen(shell);
}

static void enter(watchy_shell_t *shell, watchy_shell_screen_t screen) {
    push_frame(shell, shell->screen, shell->selection, shell->view_start);
    shell->screen = screen;
    shell->selection = 0u;
    shell->view_start = 0u;
}

static bool restore_parent(watchy_shell_t *shell) {
    watchy_shell_navigation_frame_t frame;
    if (shell->history_depth == 0u) {
        shell->screen = shell->return_screen;
        shell->selection = 0u;
        shell->view_start = 0u;
        return false;
    }
    frame = shell->history[--shell->history_depth];
    shell->screen = frame.screen;
    shell->selection = frame.selection;
    shell->view_start = frame.view_start;
    sync_return_screen(shell);
    return true;
}

static void enter_watchface(watchy_shell_t *shell) {
    shell->screen = WATCHY_SHELL_WATCHFACE;
    shell->selection = 0u;
    shell->view_start = 0u;
    shell->history_depth = 0u;
    shell->return_screen = shell->safe_mode ? WATCHY_SHELL_SAFE_MODE
                                            : WATCHY_SHELL_LAUNCHER;
}

static void reveal_selection(watchy_shell_t *shell, uint8_t count) {
    if (count <= WATCHY_SHELL_VISIBLE_ROWS || shell->selection == 0u) {
        shell->view_start = 0u;
    } else if (shell->selection == count - 1u && shell->view_start == 0u) {
        shell->view_start = (uint8_t)(count - WATCHY_SHELL_VISIBLE_ROWS);
    } else if (shell->selection < shell->view_start) {
        shell->view_start = shell->selection;
    } else if (shell->selection >= shell->view_start + WATCHY_SHELL_VISIBLE_ROWS) {
        shell->view_start =
            (uint8_t)(shell->selection - WATCHY_SHELL_VISIBLE_ROWS + 1u);
    }
}

void watchy_shell_begin(watchy_shell_t *shell,
                        watchy_wake_cause_t wake_cause,
                        bool motion_wake_enabled,
                        bool safe_mode_requested,
                        bool package_watchface_failed) {
    if (shell == NULL) {
        return;
    }
    memset(shell, 0, sizeof(*shell));
    memset(s_catalog_package_refs, 0, sizeof(s_catalog_package_refs));
    s_catalog_owner = shell;
    shell->face_count = 1u;
    shell->safe_mode = safe_mode_requested;
    shell->package_warning = package_watchface_failed && !shell->safe_mode;
    shell->screen = shell->safe_mode ? WATCHY_SHELL_SAFE_MODE
                    : wake_cause == WATCHY_WAKE_BUTTON ? WATCHY_SHELL_LAUNCHER
                                                       : WATCHY_SHELL_WATCHFACE;
    shell->return_screen = shell->safe_mode ? WATCHY_SHELL_SAFE_MODE
                                            : WATCHY_SHELL_LAUNCHER;
    if (!shell->safe_mode && wake_cause == WATCHY_WAKE_BUTTON) {
        push_frame(shell, WATCHY_SHELL_WATCHFACE, 0u, 0u);
    }
    shell->sleep_requested = (wake_cause == WATCHY_WAKE_MOTION && !motion_wake_enabled) ||
                             (shell->safe_mode &&
                              (wake_cause == WATCHY_WAKE_RTC || wake_cause == WATCHY_WAKE_TIMER ||
                               wake_cause == WATCHY_WAKE_MOTION));
}

watchy_shell_sleep_route_t watchy_shell_sleep_route(
    watchy_shell_t *shell,
    bool input_pending,
    watchy_status_t preparation_status) {
    if (shell == NULL) {
        return WATCHY_SHELL_SLEEP_FAIL_CLOSED;
    }
    if (input_pending || preparation_status == WATCHY_STATUS_CANCELLED) {
        shell->sleep_requested = false;
        return WATCHY_SHELL_SLEEP_PROCESS_INPUT;
    }
    return preparation_status == WATCHY_STATUS_OK
               ? WATCHY_SHELL_SLEEP_ENTER
               : WATCHY_SHELL_SLEEP_FAIL_CLOSED;
}

void watchy_shell_require_manual_time(watchy_shell_t *shell, bool interactive) {
    if (shell == NULL) {
        return;
    }
    push_frame(shell, shell->safe_mode ? WATCHY_SHELL_SAFE_MODE
                                      : WATCHY_SHELL_SETTINGS,
               0u, 0u);
    shell->screen = WATCHY_SHELL_MANUAL_TIME;
    shell->selection = 0u;
    shell->view_start = 0u;
    shell->sleep_requested = !interactive;
}

void watchy_shell_set_package_count(watchy_shell_t *shell, size_t package_count) {
    if (shell != NULL) {
        const size_t bounded = package_count > WATCHY_PACKAGE_INSTALLED_MAX
                                   ? WATCHY_PACKAGE_INSTALLED_MAX : package_count;
        shell->app_count = (uint8_t)bounded;
        sync_package_compatibility_view(shell);
        if (shell->selection >= item_count(shell)) {
            shell->selection = 0u;
        }
        reveal_selection(shell, item_count(shell));
    }
}

void watchy_shell_set_package_catalog(watchy_shell_t *shell,
                                     const watchy_package_catalog_t *catalog,
                                     bool index_readable) {
    if (shell == NULL) {
        return;
    }
    shell->package_index_readable = catalog != NULL && index_readable;
    s_catalog_owner = shell;
    shell->app_count = 0u;
    shell->face_count = 1u;
    shell->recovery_count = 0u;
    shell->active_face_selection = 0u;
    memset(shell->app_indices, 0, sizeof(shell->app_indices));
    memset(shell->face_indices, 0, sizeof(shell->face_indices));
    memset(shell->recovery_indices, 0, sizeof(shell->recovery_indices));
    memset(shell->face_quarantined, 0, sizeof(shell->face_quarantined));
    memset(s_catalog_package_refs, 0, sizeof(s_catalog_package_refs));

    if (shell->package_index_readable) {
        const size_t catalog_count = catalog->count < WATCHY_PACKAGE_INSTALLED_MAX
                                         ? catalog->count : WATCHY_PACKAGE_INSTALLED_MAX;
        for (size_t index = 0u; index < catalog_count; ++index) {
            const watchy_package_info_t *package = &catalog->packages[index];
            const size_t ref_length = strnlen(package->package_ref,
                                              sizeof(package->package_ref));
            if (ref_length == 0u || ref_length >= sizeof(package->package_ref) ||
                (package->type != WATCHY_PACKAGE_TYPE_APP &&
                 package->type != WATCHY_PACKAGE_TYPE_WATCHFACE)) {
                continue;
            }
            memcpy(s_catalog_package_refs[index], package->package_ref,
                   ref_length + 1u);
            shell->recovery_indices[shell->recovery_count++] = (uint8_t)index;
            if (package->type == WATCHY_PACKAGE_TYPE_APP) {
                shell->app_indices[shell->app_count++] = (uint8_t)index;
            } else if (!shell->safe_mode) {
                const uint8_t face_position = (uint8_t)(shell->face_count - 1u);
                shell->face_indices[face_position] = (uint8_t)index;
                shell->face_quarantined[face_position] = package->quarantined;
                ++shell->face_count;
                if (shell->active_face_selection == 0u && package->active &&
                    !package->pending && !package->quarantined) {
                    shell->active_face_selection = shell->face_count - 1u;
                }
            }
        }
    }
    sync_package_compatibility_view(shell);
    if (shell->screen == WATCHY_SHELL_WATCHFACE_SELECTOR) {
        shell->selection = shell->active_face_selection;
    } else if (shell->screen == WATCHY_SHELL_PACKAGE_APPS &&
               shell->selection >= item_count(shell)) {
        shell->selection = 0u;
    }
    reveal_selection(shell, item_count(shell));
}

bool watchy_shell_selected_package(const watchy_shell_t *shell, size_t *out_catalog_index) {
    const uint8_t count = shell == NULL ? 0u
                            : shell->safe_mode ? shell->recovery_count : shell->app_count;
    const uint8_t *indices = shell == NULL ? NULL
                               : shell->safe_mode ? shell->recovery_indices : shell->app_indices;
    if (shell == NULL || out_catalog_index == NULL || count == 0u ||
        shell->selection >= count) {
        return false;
    }
    *out_catalog_index = indices[shell->selection];
    return true;
}

size_t watchy_shell_package_page_start(const watchy_shell_t *shell) {
    const uint8_t count = shell == NULL ? 0u
                            : shell->safe_mode ? shell->recovery_count : shell->app_count;
    if (shell == NULL || count == 0u) {
        return 0u;
    }
    return shell->view_start;
}

bool watchy_shell_selected_watchface(const watchy_shell_t *shell,
                                     size_t *out_catalog_index) {
    if (shell == NULL || out_catalog_index == NULL || shell->selection == 0u ||
        shell->selection >= shell->face_count) {
        return false;
    }
    *out_catalog_index = shell->face_indices[shell->selection - 1u];
    return true;
}

size_t watchy_shell_watchface_page_start(const watchy_shell_t *shell) {
    if (shell == NULL || shell->face_count == 0u) {
        return 0u;
    }
    return shell->view_start;
}

bool watchy_shell_format_package_label(const watchy_package_info_t *package,
                                       size_t position,
                                       size_t total,
                                       char *out_label,
                                       size_t out_size) {
    uint32_t hash = UINT32_C(2166136261);
    if (package == NULL || out_label == NULL || out_size == 0u || total == 0u ||
        position >= total) {
        return false;
    }
    for (size_t index = 0u; package->package_ref[index] != '\0'; ++index) {
        hash ^= (uint8_t)package->package_ref[index];
        hash *= UINT32_C(16777619);
    }
    const int length = snprintf(out_label, out_size, "%02u/%02u %.9s #%08lx",
                                (unsigned)(position + 1u), (unsigned)total,
                                package->package_ref, (unsigned long)hash);
    return length >= 0 && (size_t)length < out_size;
}

void watchy_shell_set_diagnostic_count(watchy_shell_t *shell, size_t count) {
    if (shell == NULL) {
        return;
    }
    shell->diagnostic_count = count > UINT8_MAX ? UINT8_MAX : (uint8_t)count;
    if (shell->screen == WATCHY_SHELL_DIAGNOSTICS && shell->selection >= item_count(shell)) {
        shell->selection = 0u;
    }
    reveal_selection(shell, item_count(shell));
}

size_t watchy_shell_diagnostic_page_start(const watchy_shell_t *shell) {
    if (shell == NULL || shell->diagnostic_count == 0u) {
        return 0u;
    }
    return shell->view_start;
}

size_t watchy_shell_visible_start(const watchy_shell_t *shell) {
    return shell == NULL ? 0u : shell->view_start;
}

void watchy_shell_set_home_timezone_index(watchy_shell_t *shell, size_t index) {
    const size_t count = watchy_settings_timezone_count();
    if (shell == NULL) {
        return;
    }
    shell->home_timezone_index =
        (uint8_t)(index < count ? index : 14u);
    if (shell->screen == WATCHY_SHELL_TIMEZONE) {
        shell->selection = shell->home_timezone_index;
        reveal_selection(shell, (uint8_t)count);
    }
}

bool watchy_shell_format_diagnostic_label(const watchy_diagnostic_entry_t *entry,
                                          char *out_label,
                                          size_t out_size) {
    const char *state;
    char service[13];
    size_t length = 0u;
    if (entry == NULL || entry->service == NULL || out_label == NULL || out_size == 0u) {
        return false;
    }
    switch (entry->state) {
    case WATCHY_DIAGNOSTIC_PASS:
        state = entry->scope == WATCHY_DIAGNOSTIC_ACTIVE_ACCEPTANCE ? "PASS" : "READY";
        break;
    case WATCHY_DIAGNOSTIC_FAIL: state = "FAIL"; break;
    case WATCHY_DIAGNOSTIC_UNAVAILABLE: state = "N/A"; break;
    case WATCHY_DIAGNOSTIC_STOPPED: state = "OFF"; break;
    default: return false;
    }
    while (entry->service[length] != '\0' && length + 1u < sizeof(service)) {
        const unsigned char value = (unsigned char)entry->service[length];
        if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '-' || value == '_')) {
            return false;
        }
        service[length] = value >= 'a' && value <= 'z' ? (char)(value - 'a' + 'A')
                                                       : (char)value;
        ++length;
    }
    service[length] = '\0';
    const int result = snprintf(out_label, out_size, "%-12s %s", service, state);
    return length != 0u && result >= 0 && (size_t)result < out_size;
}

void watchy_shell_fail(watchy_shell_t *shell, watchy_shell_error_t error) {
    if (shell == NULL || error == WATCHY_SHELL_ERROR_NONE) {
        return;
    }
    if (shell->screen != WATCHY_SHELL_ERROR) {
        enter(shell, WATCHY_SHELL_ERROR);
    }
    shell->selection = 0u;
    shell->view_start = 0u;
    shell->editing = false;
    clear_pending_action(shell);
    shell->error = error;
}

const char *watchy_shell_error_message(const watchy_shell_t *shell) {
    if (shell == NULL) return "OPERATION FAILED";
    switch (shell->error) {
    case WATCHY_SHELL_ERROR_SETTINGS_LOAD: return "SETTINGS LOAD FAILED";
    case WATCHY_SHELL_ERROR_SETTINGS_SAVE: return "SETTINGS SAVE FAILED";
    case WATCHY_SHELL_ERROR_MANUAL_TIME: return "TIME SAVE FAILED";
    case WATCHY_SHELL_ERROR_NTP: return "TIME SYNC FAILED";
    case WATCHY_SHELL_ERROR_PORTAL: return "PORTAL START FAILED";
    case WATCHY_SHELL_ERROR_DISPLAY: return "DISPLAY FAILED";
    case WATCHY_SHELL_ERROR_PACKAGE: return "PACKAGE ACTION FAILED";
    case WATCHY_SHELL_ERROR_INPUT: return "INPUT SERVICE FAILED";
    case WATCHY_SHELL_ERROR_NONE: break;
    }
    return "OPERATION FAILED";
}

static void select_launcher(watchy_shell_t *shell) {
    static const watchy_shell_screen_t screens[WATCHY_SHELL_LAUNCHER_ITEMS] = {
        WATCHY_SHELL_WATCHFACE_SELECTOR,
        WATCHY_SHELL_PACKAGE_APPS,
        WATCHY_SHELL_SETTINGS,
    };
    enter(shell, screens[shell->selection]);
    if (shell->screen == WATCHY_SHELL_WATCHFACE_SELECTOR) {
        shell->selection = shell->active_face_selection;
        reveal_selection(shell, item_count(shell));
    }
}

static void select_settings(watchy_shell_t *shell) {
    switch (shell->selection) {
    case 0u:
    case 2u:
    case 3u:
    case 8u:
        queue_action(shell, WATCHY_SHELL_ACTION_SAVE_SETTINGS);
        break;
    case 1u:
        enter(shell, WATCHY_SHELL_TIMEZONE);
        shell->selection = shell->home_timezone_index;
        reveal_selection(shell, (uint8_t)watchy_settings_timezone_count());
        break;
    case 4u:
        enter(shell, WATCHY_SHELL_MANUAL_TIME);
        break;
    case 5u:
        enter(shell, WATCHY_SHELL_NTP_SYNC);
        queue_action(shell, WATCHY_SHELL_ACTION_SYNC_NTP);
        break;
    case 6u:
        enter(shell, WATCHY_SHELL_CONNECTIVITY);
        break;
    case 7u:
        enter(shell, WATCHY_SHELL_PACKAGE_PORTAL);
        break;
    case 9u:
        enter(shell, WATCHY_SHELL_DIAGNOSTICS);
        break;
    case 10u:
        enter(shell, WATCHY_SHELL_ABOUT);
        break;
    default:
        break;
    }
}

watchy_transition_rect_t watchy_shell_settings_confirmation_rect(uint8_t selection,
                                                                 uint8_t view_start) {
    if (selection < view_start ||
        selection - view_start >= WATCHY_SHELL_VISIBLE_ROWS) {
        return (watchy_transition_rect_t){0};
    }
    const uint8_t slot = (uint8_t)(selection - view_start);
    return (watchy_transition_rect_t){0, (int16_t)(25 + slot * 55u), 187, 55};
}

watchy_transition_rect_t watchy_shell_sync_progress_rect(void) {
    return (watchy_transition_rect_t){4, 75, 192, 19};
}

bool watchy_shell_transition_for_change(const watchy_shell_transition_context_t *change,
                                        watchy_transition_request_v1_t *out_request) {
    if (change == NULL || out_request == NULL) {
        return false;
    }

    *out_request = (watchy_transition_request_v1_t){
        .size = sizeof(*out_request),
        .effect = WATCHY_TRANSITION_CUT,
        .direction = WATCHY_TRANSITION_DIRECTION_NONE,
    };
    if (change->safe_mode || change->from == WATCHY_SHELL_SAFE_MODE ||
        change->to == WATCHY_SHELL_SAFE_MODE) {
        return watchy_transition_validate(out_request) == WATCHY_STATUS_OK;
    }
    if (change->sync_progress || change->saved) {
        if (!change->has_rect) {
            return false;
        }
        out_request->flags = WATCHY_TRANSITION_HAS_RECT;
        out_request->rect = change->rect;
    }
    if (change->sync_progress) {
        out_request->effect = WATCHY_TRANSITION_FILL;
    } else if (change->saved) {
        out_request->effect = WATCHY_TRANSITION_FLASH;
    } else if (change->from != WATCHY_SHELL_WATCHFACE &&
               change->to == WATCHY_SHELL_WATCHFACE &&
               change->input == WATCHY_SHELL_INPUT_BACK) {
        out_request->effect = WATCHY_TRANSITION_PUSH;
        out_request->direction = WATCHY_TRANSITION_DIRECTION_LEFT;
        out_request->flags = WATCHY_TRANSITION_PREFER_FULL;
    } else if (change->sleep_requested && change->input == WATCHY_SHELL_INPUT_BACK) {
        out_request->effect = WATCHY_TRANSITION_SPLIT;
    } else if (change->from == WATCHY_SHELL_WATCHFACE &&
               change->to == WATCHY_SHELL_LAUNCHER) {
        /* A button wake must acknowledge Menu with the first physical write.
         * Multi-write effects make the accepted press look unresponsive. */
        out_request->effect = WATCHY_TRANSITION_CUT;
    } else if (change->from != change->to && change->input == WATCHY_SHELL_INPUT_MENU) {
        out_request->effect = WATCHY_TRANSITION_PUSH;
        out_request->direction = WATCHY_TRANSITION_DIRECTION_RIGHT;
    } else if (change->from != change->to && change->input == WATCHY_SHELL_INPUT_BACK) {
        out_request->effect = WATCHY_TRANSITION_PUSH;
        out_request->direction = WATCHY_TRANSITION_DIRECTION_LEFT;
    }
    return watchy_transition_validate(out_request) == WATCHY_STATUS_OK;
}

void watchy_shell_presentation_observe(watchy_shell_presentation_state_t *state,
                                       watchy_shell_t *shell,
                                       watchy_shell_presentation_outcome_t outcome,
                                       watchy_button_mask_t cancelled_buttons) {
    const bool target_presented = outcome == WATCHY_SHELL_PRESENT_TARGET;
    if (state != NULL) {
        state->sleep_deferred = !target_presented;
        if (!target_presented) {
            const watchy_button_mask_t allowed =
                WATCHY_BUTTON_MASK_MENU | WATCHY_BUTTON_MASK_BACK |
                WATCHY_BUTTON_MASK_DOWN | WATCHY_BUTTON_MASK_UP;
            state->pending_buttons |= cancelled_buttons & allowed;
        }
    }
    if (!target_presented && shell != NULL) {
        shell->sleep_requested = false;
    }
}

watchy_button_mask_t watchy_shell_presentation_take_cancelled_buttons(
    watchy_shell_presentation_state_t *state) {
    watchy_button_mask_t buttons;
    if (state == NULL) {
        return 0u;
    }
    buttons = state->pending_buttons;
    state->pending_buttons = 0u;
    return buttons;
}

bool watchy_shell_presentation_has_pending_input(
    const watchy_shell_presentation_state_t *state) {
    return state != NULL && state->pending_buttons != 0u;
}

bool watchy_shell_presentation_allows_sleep(
    const watchy_shell_presentation_state_t *state) {
    return state != NULL && !state->sleep_deferred && state->pending_buttons == 0u;
}

bool watchy_shell_presentation_needs_post_action(
    watchy_shell_presentation_outcome_t outcome) {
    return outcome == WATCHY_SHELL_PRESENT_TARGET;
}

void watchy_shell_input(watchy_shell_t *shell, watchy_shell_input_t input) {
    uint8_t count;
    if (shell == NULL) {
        return;
    }
    if (input == WATCHY_SHELL_INPUT_IDLE) {
        enter_watchface(shell);
        shell->sleep_requested = true;
        return;
    }
    if (shell->screen == WATCHY_SHELL_MANUAL_TIME && shell->editing &&
        (input == WATCHY_SHELL_INPUT_UP || input == WATCHY_SHELL_INPUT_DOWN)) {
        queue_action(shell, input == WATCHY_SHELL_INPUT_UP
                                ? WATCHY_SHELL_ACTION_MANUAL_INCREMENT
                                : WATCHY_SHELL_ACTION_MANUAL_DECREMENT);
        return;
    }
    count = item_count(shell);
    if (input == WATCHY_SHELL_INPUT_UP) {
        shell->selection = shell->selection == 0u ? (uint8_t)(count - 1u)
                                                  : (uint8_t)(shell->selection - 1u);
        reveal_selection(shell, count);
        return;
    }
    if (input == WATCHY_SHELL_INPUT_DOWN) {
        shell->selection = (uint8_t)((shell->selection + 1u) % count);
        reveal_selection(shell, count);
        return;
    }
    if (input == WATCHY_SHELL_INPUT_BACK) {
        if (shell->screen == WATCHY_SHELL_MANUAL_TIME && shell->editing) {
            shell->editing = false;
            return;
        }
        if (shell->screen == WATCHY_SHELL_MANUAL_TIME) {
            queue_action(shell, WATCHY_SHELL_ACTION_SAVE_MANUAL_TIME);
        }
        if (shell->screen == WATCHY_SHELL_WATCHFACE) {
            shell->sleep_requested = true;
        } else {
            const watchy_shell_screen_t prior = shell->screen;
            (void)restore_parent(shell);
            if (prior == WATCHY_SHELL_LAUNCHER &&
                shell->screen == WATCHY_SHELL_WATCHFACE) {
                shell->sleep_requested = true;
            }
        }
        return;
    }
    if (input != WATCHY_SHELL_INPUT_MENU) {
        return;
    }
    if (shell->screen == WATCHY_SHELL_WATCHFACE_SELECTOR) {
        if (shell->selection == 0u) {
            queue_action(shell, WATCHY_SHELL_ACTION_SELECT_BUILTIN);
            enter_watchface(shell);
            shell->sleep_requested = true;
        } else if (shell->selection < shell->face_count) {
            const uint8_t face_position = (uint8_t)(shell->selection - 1u);
            if (shell->face_quarantined[face_position]) {
                watchy_shell_fail(shell, WATCHY_SHELL_ERROR_PACKAGE);
            } else if (queue_package_action(shell, WATCHY_SHELL_ACTION_SELECT_WATCHFACE,
                                            shell->face_indices[face_position])) {
                enter_watchface(shell);
                shell->sleep_requested = true;
            } else {
                watchy_shell_fail(shell, WATCHY_SHELL_ERROR_PACKAGE);
            }
        }
        return;
    }
    switch (shell->screen) {
    case WATCHY_SHELL_WATCHFACE:
        enter(shell, shell->safe_mode ? WATCHY_SHELL_SAFE_MODE
                                     : WATCHY_SHELL_LAUNCHER);
        break;
    case WATCHY_SHELL_LAUNCHER:
        select_launcher(shell);
        break;
    case WATCHY_SHELL_SETTINGS:
        select_settings(shell);
        break;
    case WATCHY_SHELL_TIMEZONE: {
        int16_t offset;
        if (watchy_settings_timezone_offset_at(shell->selection, &offset)) {
            queue_numeric_action(shell, WATCHY_SHELL_ACTION_SAVE_TIMEZONE, offset);
            (void)restore_parent(shell);
        }
        break;
    }
    case WATCHY_SHELL_MANUAL_TIME:
        shell->editing = !shell->editing;
        break;
    case WATCHY_SHELL_PACKAGE_PORTAL:
        queue_action(shell, shell->selection == 0u
                                ? WATCHY_SHELL_ACTION_START_PORTAL_CLIENT
                                : WATCHY_SHELL_ACTION_START_PORTAL_AP);
        break;
    case WATCHY_SHELL_SAFE_MODE:
        if (shell->selection == 0u) {
            enter(shell, WATCHY_SHELL_DIAGNOSTICS);
        } else if (shell->selection == 1u) {
            if (shell->package_index_readable) {
                enter(shell, WATCHY_SHELL_PACKAGE_APPS);
            } else {
                queue_action(shell, WATCHY_SHELL_ACTION_PURGE_PACKAGES);
            }
        } else {
            queue_action(shell, WATCHY_SHELL_ACTION_NORMAL_REBOOT);
        }
        break;
    case WATCHY_SHELL_PACKAGE_APPS:
        if ((shell->safe_mode ? shell->recovery_count : shell->app_count) != 0u) {
            const uint8_t catalog_index = shell->safe_mode
                                              ? shell->recovery_indices[shell->selection]
                                              : shell->app_indices[shell->selection];
            if (!queue_package_action(shell,
                                      shell->safe_mode
                                          ? WATCHY_SHELL_ACTION_REMOVE_PACKAGE
                                          : WATCHY_SHELL_ACTION_RUN_PACKAGE,
                                      catalog_index)) {
                watchy_shell_fail(shell, WATCHY_SHELL_ERROR_PACKAGE);
            }
        }
        break;
    default:
        break;
    }
}

watchy_shell_action_t watchy_shell_take_action(watchy_shell_t *shell) {
    watchy_shell_action_request_t request;
    return watchy_shell_take_action_request(shell, &request)
               ? request.action : WATCHY_SHELL_ACTION_NONE;
}

bool watchy_shell_take_action_request(watchy_shell_t *shell,
                                      watchy_shell_action_request_t *out_request) {
    if (out_request != NULL) {
        memset(out_request, 0, sizeof(*out_request));
    }
    if (shell == NULL || out_request == NULL ||
        shell->pending_action == WATCHY_SHELL_ACTION_NONE) {
        return false;
    }
    out_request->action = shell->pending_action;
    out_request->has_package = shell->pending_action_has_package;
    out_request->catalog_index = shell->pending_action_catalog_index;
    out_request->numeric_value = shell->pending_action_numeric_value;
    memcpy(out_request->package_ref, shell->pending_action_package_ref,
           sizeof(out_request->package_ref));
    clear_pending_action(shell);
    return true;
}

bool watchy_shell_should_render_builtin(const watchy_shell_t *shell) {
    return shell != NULL && shell->screen == WATCHY_SHELL_WATCHFACE;
}
