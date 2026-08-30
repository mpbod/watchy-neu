#include "watchy/shell.h"

#include <stdio.h>
#include <string.h>

static uint8_t item_count(const watchy_shell_t *shell) {
    switch (shell->screen) {
    case WATCHY_SHELL_LAUNCHER:
        return WATCHY_SHELL_LAUNCHER_ITEMS;
    case WATCHY_SHELL_SETTINGS:
        return 8u;
    case WATCHY_SHELL_SAFE_MODE:
        return 3u;
    case WATCHY_SHELL_PACKAGE_APPS:
        return shell->package_count == 0u ? 1u : shell->package_count;
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

static void enter(watchy_shell_t *shell,
                  watchy_shell_screen_t screen,
                  watchy_shell_screen_t return_screen) {
    shell->screen = screen;
    shell->return_screen = return_screen;
    shell->selection = 0u;
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
    shell->safe_mode = safe_mode_requested;
    shell->package_warning = package_watchface_failed && !shell->safe_mode;
    shell->screen = shell->safe_mode ? WATCHY_SHELL_SAFE_MODE
                    : wake_cause == WATCHY_WAKE_BUTTON ? WATCHY_SHELL_LAUNCHER
                                                       : WATCHY_SHELL_WATCHFACE;
    shell->return_screen = shell->safe_mode ? WATCHY_SHELL_SAFE_MODE
                                            : WATCHY_SHELL_LAUNCHER;
    shell->sleep_requested = (wake_cause == WATCHY_WAKE_MOTION && !motion_wake_enabled) ||
                             (shell->safe_mode &&
                              (wake_cause == WATCHY_WAKE_RTC || wake_cause == WATCHY_WAKE_TIMER ||
                               wake_cause == WATCHY_WAKE_MOTION));
}

void watchy_shell_require_manual_time(watchy_shell_t *shell, bool interactive) {
    if (shell == NULL) {
        return;
    }
    enter(shell, WATCHY_SHELL_MANUAL_TIME,
          shell->safe_mode ? WATCHY_SHELL_SAFE_MODE : WATCHY_SHELL_SETTINGS);
    shell->sleep_requested = !interactive;
}

void watchy_shell_set_package_count(watchy_shell_t *shell, size_t package_count) {
    if (shell != NULL) {
        shell->package_count = package_count > UINT8_MAX ? UINT8_MAX : (uint8_t)package_count;
        if (shell->selection >= item_count(shell)) {
            shell->selection = 0u;
        }
    }
}

void watchy_shell_set_package_catalog(watchy_shell_t *shell,
                                     const watchy_package_catalog_t *catalog,
                                     bool index_readable) {
    size_t mapped = 0u;
    if (shell == NULL) {
        return;
    }
    shell->package_index_readable = index_readable;
    if (catalog != NULL && index_readable) {
        for (size_t index = 0u; index < catalog->count &&
                               mapped < WATCHY_PACKAGE_INSTALLED_MAX; ++index) {
            if (shell->safe_mode || catalog->packages[index].type == WATCHY_PACKAGE_TYPE_APP) {
                shell->package_indices[mapped++] = (uint8_t)index;
            }
        }
    }
    watchy_shell_set_package_count(shell, mapped);
}

bool watchy_shell_selected_package(const watchy_shell_t *shell, size_t *out_catalog_index) {
    if (shell == NULL || out_catalog_index == NULL || shell->package_count == 0u ||
        shell->selection >= shell->package_count) {
        return false;
    }
    *out_catalog_index = shell->package_indices[shell->selection];
    return true;
}

size_t watchy_shell_package_page_start(const watchy_shell_t *shell) {
    if (shell == NULL || shell->package_count == 0u) {
        return 0u;
    }
    return (shell->selection / WATCHY_SHELL_PACKAGE_PAGE_ITEMS) *
           WATCHY_SHELL_PACKAGE_PAGE_ITEMS;
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
}

size_t watchy_shell_diagnostic_page_start(const watchy_shell_t *shell) {
    if (shell == NULL || shell->diagnostic_count == 0u) {
        return 0u;
    }
    return (shell->selection / WATCHY_SHELL_PACKAGE_PAGE_ITEMS) *
           WATCHY_SHELL_PACKAGE_PAGE_ITEMS;
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
        shell->return_screen = shell->screen;
    }
    shell->screen = WATCHY_SHELL_ERROR;
    shell->selection = 0u;
    shell->editing = false;
    shell->pending_action = WATCHY_SHELL_ACTION_NONE;
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
    case WATCHY_SHELL_ERROR_NONE: break;
    }
    return "OPERATION FAILED";
}

static void select_launcher(watchy_shell_t *shell) {
    static const watchy_shell_screen_t screens[WATCHY_SHELL_LAUNCHER_ITEMS] = {
        WATCHY_SHELL_WATCHFACE,
        WATCHY_SHELL_PACKAGE_APPS,
        WATCHY_SHELL_SETTINGS,
        WATCHY_SHELL_CONNECTIVITY,
        WATCHY_SHELL_DIAGNOSTICS,
        WATCHY_SHELL_ABOUT,
    };
    enter(shell, screens[shell->selection], WATCHY_SHELL_LAUNCHER);
}

static void select_settings(watchy_shell_t *shell) {
    switch (shell->selection) {
    case 0u:
    case 1u:
    case 2u:
    case 7u:
        shell->pending_action = WATCHY_SHELL_ACTION_SAVE_SETTINGS;
        break;
    case 3u:
        enter(shell, WATCHY_SHELL_MANUAL_TIME, WATCHY_SHELL_SETTINGS);
        break;
    case 4u:
        enter(shell, WATCHY_SHELL_NTP_SYNC, WATCHY_SHELL_SETTINGS);
        shell->pending_action = WATCHY_SHELL_ACTION_SYNC_NTP;
        break;
    case 5u:
        enter(shell, WATCHY_SHELL_CONNECTIVITY, WATCHY_SHELL_SETTINGS);
        break;
    case 6u:
        enter(shell, WATCHY_SHELL_PACKAGE_PORTAL, WATCHY_SHELL_SETTINGS);
        break;
    default:
        break;
    }
}

watchy_transition_rect_t watchy_shell_settings_confirmation_rect(uint8_t selection) {
    if (selection >= 8u) {
        return (watchy_transition_rect_t){0};
    }
    return (watchy_transition_rect_t){4, (int16_t)(30 + selection * 21), 192, 15};
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
    } else if (change->sleep_requested && change->input == WATCHY_SHELL_INPUT_BACK) {
        out_request->effect = WATCHY_TRANSITION_SPLIT;
    } else if (change->from == WATCHY_SHELL_WATCHFACE &&
               change->to == WATCHY_SHELL_LAUNCHER) {
        out_request->effect = WATCHY_TRANSITION_WIPE;
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
        if (outcome == WATCHY_SHELL_PRESENT_CANCELLED) {
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
        shell->screen = WATCHY_SHELL_WATCHFACE;
        shell->selection = 0u;
        shell->sleep_requested = true;
        return;
    }
    if (shell->screen == WATCHY_SHELL_MANUAL_TIME && shell->editing &&
        (input == WATCHY_SHELL_INPUT_UP || input == WATCHY_SHELL_INPUT_DOWN)) {
        shell->pending_action = input == WATCHY_SHELL_INPUT_UP
                                    ? WATCHY_SHELL_ACTION_MANUAL_INCREMENT
                                    : WATCHY_SHELL_ACTION_MANUAL_DECREMENT;
        return;
    }
    count = item_count(shell);
    if (input == WATCHY_SHELL_INPUT_UP) {
        shell->selection = shell->selection == 0u ? (uint8_t)(count - 1u)
                                                  : (uint8_t)(shell->selection - 1u);
        return;
    }
    if (input == WATCHY_SHELL_INPUT_DOWN) {
        shell->selection = (uint8_t)((shell->selection + 1u) % count);
        return;
    }
    if (input == WATCHY_SHELL_INPUT_BACK) {
        if (shell->screen == WATCHY_SHELL_MANUAL_TIME && shell->editing) {
            shell->editing = false;
            return;
        }
        if (shell->screen == WATCHY_SHELL_MANUAL_TIME) {
            shell->pending_action = WATCHY_SHELL_ACTION_SAVE_MANUAL_TIME;
        }
        if (shell->screen == WATCHY_SHELL_WATCHFACE) {
            shell->sleep_requested = true;
        } else if (shell->screen == WATCHY_SHELL_LAUNCHER) {
            enter(shell, WATCHY_SHELL_WATCHFACE, WATCHY_SHELL_LAUNCHER);
        } else {
            enter(shell, shell->return_screen,
                  shell->safe_mode ? WATCHY_SHELL_SAFE_MODE : WATCHY_SHELL_LAUNCHER);
        }
        return;
    }
    if (input != WATCHY_SHELL_INPUT_MENU) {
        return;
    }
    switch (shell->screen) {
    case WATCHY_SHELL_WATCHFACE:
        enter(shell, shell->safe_mode ? WATCHY_SHELL_SAFE_MODE : WATCHY_SHELL_LAUNCHER,
              WATCHY_SHELL_WATCHFACE);
        break;
    case WATCHY_SHELL_LAUNCHER:
        select_launcher(shell);
        break;
    case WATCHY_SHELL_SETTINGS:
        select_settings(shell);
        break;
    case WATCHY_SHELL_MANUAL_TIME:
        shell->editing = !shell->editing;
        break;
    case WATCHY_SHELL_PACKAGE_PORTAL:
        shell->pending_action = shell->selection == 0u
                                    ? WATCHY_SHELL_ACTION_START_PORTAL_CLIENT
                                    : WATCHY_SHELL_ACTION_START_PORTAL_AP;
        break;
    case WATCHY_SHELL_SAFE_MODE:
        if (shell->selection == 0u) {
            enter(shell, WATCHY_SHELL_DIAGNOSTICS, WATCHY_SHELL_SAFE_MODE);
        } else if (shell->selection == 1u) {
            if (shell->package_index_readable) {
                enter(shell, WATCHY_SHELL_PACKAGE_APPS, WATCHY_SHELL_SAFE_MODE);
            } else {
                shell->pending_action = WATCHY_SHELL_ACTION_PURGE_PACKAGES;
            }
        } else {
            shell->pending_action = WATCHY_SHELL_ACTION_NORMAL_REBOOT;
        }
        break;
    case WATCHY_SHELL_PACKAGE_APPS:
        if (shell->package_count != 0u) {
            shell->pending_action = shell->safe_mode ? WATCHY_SHELL_ACTION_REMOVE_PACKAGE
                                                     : WATCHY_SHELL_ACTION_RUN_PACKAGE;
        }
        break;
    default:
        break;
    }
}

watchy_shell_action_t watchy_shell_take_action(watchy_shell_t *shell) {
    watchy_shell_action_t action;
    if (shell == NULL) {
        return WATCHY_SHELL_ACTION_NONE;
    }
    action = shell->pending_action;
    shell->pending_action = WATCHY_SHELL_ACTION_NONE;
    return action;
}

bool watchy_shell_should_render_builtin(const watchy_shell_t *shell) {
    return shell != NULL && shell->screen == WATCHY_SHELL_WATCHFACE;
}
