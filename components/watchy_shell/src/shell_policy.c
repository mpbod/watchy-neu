#include "watchy/shell.h"

#include <string.h>

static uint8_t item_count(const watchy_shell_t *shell) {
    switch (shell->screen) {
    case WATCHY_SHELL_LAUNCHER:
        return WATCHY_SHELL_LAUNCHER_ITEMS;
    case WATCHY_SHELL_SETTINGS:
        return 7u;
    case WATCHY_SHELL_SAFE_MODE:
        return 3u;
    case WATCHY_SHELL_PACKAGE_APPS:
        return shell->package_count == 0u ? 1u : shell->package_count;
    case WATCHY_SHELL_MANUAL_TIME:
        return 5u;
    case WATCHY_SHELL_PACKAGE_PORTAL:
        return 2u;
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
    shell->sleep_requested = wake_cause == WATCHY_WAKE_MOTION && !motion_wake_enabled;
}

void watchy_shell_set_package_count(watchy_shell_t *shell, size_t package_count) {
    if (shell != NULL) {
        shell->package_count = package_count > UINT8_MAX ? UINT8_MAX : (uint8_t)package_count;
        if (shell->selection >= item_count(shell)) {
            shell->selection = 0u;
        }
    }
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
    case 6u:
        shell->pending_action = WATCHY_SHELL_ACTION_SAVE_SETTINGS;
        break;
    case 2u:
        enter(shell, WATCHY_SHELL_MANUAL_TIME, WATCHY_SHELL_SETTINGS);
        break;
    case 3u:
        enter(shell, WATCHY_SHELL_NTP_SYNC, WATCHY_SHELL_SETTINGS);
        shell->pending_action = WATCHY_SHELL_ACTION_SYNC_NTP;
        break;
    case 4u:
        enter(shell, WATCHY_SHELL_CONNECTIVITY, WATCHY_SHELL_SETTINGS);
        break;
    case 5u:
        enter(shell, WATCHY_SHELL_PACKAGE_PORTAL, WATCHY_SHELL_SETTINGS);
        break;
    default:
        break;
    }
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
            shell->pending_action = WATCHY_SHELL_ACTION_PURGE_PACKAGES;
        } else {
            shell->pending_action = WATCHY_SHELL_ACTION_NORMAL_REBOOT;
        }
        break;
    case WATCHY_SHELL_PACKAGE_APPS:
        if (shell->package_count != 0u) {
            shell->pending_action = WATCHY_SHELL_ACTION_RUN_PACKAGE;
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
