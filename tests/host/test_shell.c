#include <stdio.h>
#include <string.h>

#include "watchy/settings.h"
#include "watchy/shell.h"
#include "watchy/transition.h"
#include "watchy/ui.h"
#include "watchy/wifi_credentials.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static int test_invalid_persisted_settings_fall_back_field_by_field(void) {
    watchy_settings_t stored;
    watchy_settings_t settings;

    watchy_settings_defaults(&stored);
    memcpy(stored.timezone, "UTC0\nsecret", 12u);
    stored.partial_refresh_limit = 0u;
    memcpy(stored.active_watchface, "Bad/Face@1", 11u);
    memset(stored.wifi_ssid, 'x', sizeof(stored.wifi_ssid));
    memcpy(stored.ntp_server, "pool.ntp.org", 13u);

    watchy_settings_sanitize(&stored, &settings);

    CHECK(strcmp(settings.timezone, "UTC0") == 0);
    CHECK(settings.partial_refresh_limit == WATCHY_SETTINGS_DEFAULT_PARTIAL_LIMIT);
    CHECK(settings.active_watchface[0] == '\0');
    CHECK(settings.wifi_ssid[0] == '\0');
    CHECK(strcmp(settings.ntp_server, "pool.ntp.org") == 0);
    return 0;
}

static int test_display_motion_level_defaults_sanitizes_and_cycles(void) {
    watchy_settings_t stored;
    watchy_settings_t settings;

    watchy_settings_defaults(&settings);
    CHECK(settings.transition_level == WATCHY_TRANSITION_LEVEL_FULL);

    stored = settings;
    stored.transition_level = (watchy_transition_level_t)99;
    watchy_settings_sanitize(&stored, &settings);
    CHECK(settings.transition_level == WATCHY_TRANSITION_LEVEL_FULL);

    CHECK(watchy_settings_cycle_transition_level(&settings));
    CHECK(settings.transition_level == WATCHY_TRANSITION_LEVEL_REDUCED);
    CHECK(watchy_settings_cycle_transition_level(&settings));
    CHECK(settings.transition_level == WATCHY_TRANSITION_LEVEL_OFF);
    CHECK(watchy_settings_cycle_transition_level(&settings));
    CHECK(settings.transition_level == WATCHY_TRANSITION_LEVEL_FULL);
    return 0;
}

static int test_shell_transition_mapping_returns_complete_safe_requests(void) {
    static const struct {
        watchy_shell_transition_context_t change;
        watchy_transition_effect_t effect;
        watchy_transition_direction_t direction;
        uint32_t flags;
        watchy_transition_rect_t rect;
    } cases[] = {
        {
            .change = {.from = WATCHY_SHELL_WATCHFACE, .to = WATCHY_SHELL_LAUNCHER,
                       .input = WATCHY_SHELL_INPUT_MENU},
            .effect = WATCHY_TRANSITION_WIPE,
            .direction = WATCHY_TRANSITION_DIRECTION_NONE,
        },
        {
            .change = {.from = WATCHY_SHELL_LAUNCHER, .to = WATCHY_SHELL_SETTINGS,
                       .input = WATCHY_SHELL_INPUT_MENU},
            .effect = WATCHY_TRANSITION_PUSH,
            .direction = WATCHY_TRANSITION_DIRECTION_RIGHT,
        },
        {
            .change = {.from = WATCHY_SHELL_SETTINGS, .to = WATCHY_SHELL_LAUNCHER,
                       .input = WATCHY_SHELL_INPUT_BACK},
            .effect = WATCHY_TRANSITION_PUSH,
            .direction = WATCHY_TRANSITION_DIRECTION_LEFT,
        },
        {
            .change = {.from = WATCHY_SHELL_SETTINGS, .to = WATCHY_SHELL_SETTINGS,
                       .input = WATCHY_SHELL_INPUT_MENU, .saved = true,
                       .has_rect = true, .rect = {4, 30, 192, 15}},
            .effect = WATCHY_TRANSITION_FLASH,
            .direction = WATCHY_TRANSITION_DIRECTION_NONE,
            .flags = WATCHY_TRANSITION_HAS_RECT,
            .rect = {4, 30, 192, 15},
        },
        {
            .change = {.from = WATCHY_SHELL_SETTINGS, .to = WATCHY_SHELL_NTP_SYNC,
                       .input = WATCHY_SHELL_INPUT_MENU, .sync_progress = true,
                       .has_rect = true, .rect = {4, 75, 192, 19}},
            .effect = WATCHY_TRANSITION_FILL,
            .direction = WATCHY_TRANSITION_DIRECTION_NONE,
            .flags = WATCHY_TRANSITION_HAS_RECT,
            .rect = {4, 75, 192, 19},
        },
        {
            .change = {.from = WATCHY_SHELL_WATCHFACE, .to = WATCHY_SHELL_WATCHFACE,
                       .input = WATCHY_SHELL_INPUT_BACK, .sleep_requested = true},
            .effect = WATCHY_TRANSITION_SPLIT,
            .direction = WATCHY_TRANSITION_DIRECTION_NONE,
        },
        {
            .change = {.from = WATCHY_SHELL_SETTINGS, .to = WATCHY_SHELL_SETTINGS,
                       .input = WATCHY_SHELL_INPUT_DOWN},
            .effect = WATCHY_TRANSITION_CUT,
            .direction = WATCHY_TRANSITION_DIRECTION_NONE,
        },
        {
            .change = {.from = WATCHY_SHELL_SAFE_MODE, .to = WATCHY_SHELL_DIAGNOSTICS,
                       .input = WATCHY_SHELL_INPUT_MENU, .safe_mode = true},
            .effect = WATCHY_TRANSITION_CUT,
            .direction = WATCHY_TRANSITION_DIRECTION_NONE,
        },
    };

    for (size_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        watchy_transition_request_v1_t request = {
            .reserved = {UINT32_MAX, UINT32_MAX},
        };
        CHECK(watchy_shell_transition_for_change(&cases[index].change, &request));
        CHECK(request.size == sizeof(request));
        CHECK(request.effect == cases[index].effect);
        CHECK(request.direction == cases[index].direction);
        CHECK(request.flags == cases[index].flags);
        CHECK(memcmp(&request.rect, &cases[index].rect, sizeof(request.rect)) == 0);
        CHECK(request.reserved[0] == 0u && request.reserved[1] == 0u);
        CHECK(watchy_transition_validate(&request) == WATCHY_STATUS_OK);
    }

    watchy_shell_transition_context_t missing_rect = {
        .from = WATCHY_SHELL_SETTINGS,
        .to = WATCHY_SHELL_SETTINGS,
        .input = WATCHY_SHELL_INPUT_MENU,
        .saved = true,
    };
    watchy_transition_request_v1_t request;
    CHECK(!watchy_shell_transition_for_change(&missing_rect, &request));

    const watchy_transition_rect_t confirmation =
        watchy_shell_settings_confirmation_rect(7u);
    const watchy_transition_rect_t progress = watchy_shell_sync_progress_rect();
    CHECK(confirmation.x == 4 && confirmation.y == 177 &&
          confirmation.width == 192 && confirmation.height == 15);
    CHECK(progress.x == 4 && progress.y == 75 && progress.width == 192 &&
          progress.height == 19);
    return 0;
}

static int test_presentation_outcomes_gate_sleep_and_post_action_refresh(void) {
    watchy_shell_t shell;
    watchy_shell_presentation_state_t presentation = {0};

    watchy_shell_begin(&shell, WATCHY_WAKE_RTC, true, false, false);
    CHECK(watchy_shell_presentation_allows_sleep(&presentation));
    CHECK(watchy_shell_presentation_needs_post_action(
        WATCHY_SHELL_PRESENT_TARGET));

    shell.sleep_requested = true;
    watchy_shell_presentation_observe(&presentation, &shell,
                                      WATCHY_SHELL_PRESENT_TARGET, 0u);
    CHECK(shell.sleep_requested);
    CHECK(watchy_shell_presentation_allows_sleep(&presentation));

    watchy_shell_presentation_observe(&presentation, &shell,
                                      WATCHY_SHELL_PRESENT_RECOVERY, 0u);
    CHECK(!shell.sleep_requested);
    CHECK(!watchy_shell_presentation_allows_sleep(&presentation));
    CHECK(!watchy_shell_presentation_needs_post_action(
        WATCHY_SHELL_PRESENT_RECOVERY));

    watchy_shell_presentation_observe(&presentation, &shell,
                                      WATCHY_SHELL_PRESENT_TARGET, 0u);
    CHECK(watchy_shell_presentation_allows_sleep(&presentation));

    shell.sleep_requested = true;
    watchy_shell_presentation_observe(&presentation, &shell,
                                      WATCHY_SHELL_PRESENT_FAILED, 0u);
    CHECK(!shell.sleep_requested);
    CHECK(!watchy_shell_presentation_allows_sleep(&presentation));
    CHECK(!watchy_shell_presentation_needs_post_action(
        WATCHY_SHELL_PRESENT_FAILED));

    shell.sleep_requested = true;
    watchy_shell_presentation_observe(&presentation, &shell,
                                      WATCHY_SHELL_PRESENT_CANCELLED,
                                      WATCHY_BUTTON_MASK_DOWN);
    CHECK(!shell.sleep_requested);
    CHECK(watchy_shell_presentation_has_pending_input(&presentation));
    CHECK(!watchy_shell_presentation_allows_sleep(&presentation));
    CHECK(!watchy_shell_presentation_needs_post_action(
        WATCHY_SHELL_PRESENT_CANCELLED));
    CHECK(watchy_shell_presentation_take_cancelled_buttons(&presentation) ==
          WATCHY_BUTTON_MASK_DOWN);
    CHECK(!watchy_shell_presentation_has_pending_input(&presentation));
    CHECK(watchy_shell_presentation_take_cancelled_buttons(&presentation) == 0u);
    return 0;
}

static int test_wifi_settings_accept_only_open_or_valid_psk_credentials(void) {
    watchy_settings_t settings;
    watchy_settings_defaults(&settings);
    strcpy(settings.wifi_ssid, "home");
    strcpy(settings.wifi_password, "short");
    CHECK(!watchy_settings_valid(&settings));
    strcpy(settings.wifi_password, "eight888");
    CHECK(watchy_settings_valid(&settings));
    settings.wifi_ssid[0] = '\0';
    CHECK(!watchy_settings_valid(&settings));
    settings.wifi_password[0] = '\0';
    CHECK(watchy_settings_valid(&settings));
    return 0;
}

static int test_erased_settings_can_be_provisioned_and_reused_after_reload(void) {
    watchy_settings_t erased;
    watchy_settings_t persisted;
    watchy_settings_t reloaded;
    watchy_settings_defaults(&erased);
    CHECK(erased.wifi_ssid[0] == '\0' && erased.wifi_password[0] == '\0');
    CHECK(watchy_settings_set_wifi(&erased, "Lab WiFi", "correct horse battery"));
    persisted = erased;
    watchy_settings_sanitize(&persisted, &reloaded);
    CHECK(strcmp(reloaded.wifi_ssid, "Lab WiFi") == 0);
    CHECK(strcmp(reloaded.wifi_password, "correct horse battery") == 0);
    CHECK(strcmp(WATCHY_WIFI_CREDENTIAL_NAMESPACE, "watchy_cfg") == 0);
    CHECK(strcmp(WATCHY_WIFI_CREDENTIAL_SSID_KEY, "ssid") == 0);
    CHECK(strcmp(WATCHY_WIFI_CREDENTIAL_PASSWORD_KEY, "wifi_pass") == 0);
    CHECK(!watchy_settings_set_wifi(&reloaded, "Bad\nSSID", "correct horse battery"));
    CHECK(strcmp(reloaded.wifi_ssid, "Lab WiFi") == 0);
    return 0;
}

static int test_persisted_timezone_hostname_and_wpa_strings_are_syntactically_strict(void) {
    static const char *const valid_timezones[] = {
        "UTC0", "EST5EDT,M3.2.0,M11.1.0", "<+07>-7",
    };
    static const char *const invalid_timezones[] = {
        "UTC", "UTC+", "UTC0/evil", "UTC0DST", "UTC0DST,M3.2.0",
        "UTC0DST,M13.2.0,M11.1.0", "UTC0DST,M3.0.0,M11.1.0",
        "UTC0DST,M3.2.7,M11.1.0", "UTC25", "UT0",
    };
    static const char *const invalid_hosts[] = {
        "-pool.ntp.org", "pool-.ntp.org", "pool..ntp.org", ".pool.ntp.org",
        "pool.ntp.org.", "pool_ntp.org",
    };
    watchy_settings_t settings;
    for (size_t index = 0u; index < sizeof(valid_timezones) / sizeof(valid_timezones[0]); ++index) {
        watchy_settings_defaults(&settings);
        strcpy(settings.timezone, valid_timezones[index]);
        CHECK(watchy_settings_valid(&settings));
    }
    for (size_t index = 0u; index < sizeof(invalid_timezones) / sizeof(invalid_timezones[0]); ++index) {
        watchy_settings_defaults(&settings);
        strcpy(settings.timezone, invalid_timezones[index]);
        CHECK(!watchy_settings_valid(&settings));
    }
    for (size_t index = 0u; index < sizeof(invalid_hosts) / sizeof(invalid_hosts[0]); ++index) {
        watchy_settings_defaults(&settings);
        strcpy(settings.ntp_server, invalid_hosts[index]);
        CHECK(!watchy_settings_valid(&settings));
    }
    watchy_settings_defaults(&settings);
    memset(settings.ntp_server, 'a', sizeof(settings.ntp_server));
    settings.ntp_server[63] = '\0';
    CHECK(watchy_settings_valid(&settings));
    settings.ntp_server[0] = '-';
    CHECK(!watchy_settings_valid(&settings));

    watchy_settings_defaults(&settings);
    strcpy(settings.wifi_ssid, "home");
    strcpy(settings.wifi_password, "printable password");
    CHECK(watchy_settings_valid(&settings));
    settings.wifi_password[3] = '\n';
    CHECK(!watchy_settings_valid(&settings));
    memset(settings.wifi_password, 'a', 64u);
    settings.wifi_password[64] = '\0';
    CHECK(watchy_settings_valid(&settings));
    settings.wifi_password[63] = 'g';
    CHECK(!watchy_settings_valid(&settings));
    settings.wifi_ssid[0] = '\0';
    strcpy(settings.wifi_password, "must-be-empty");
    CHECK(!watchy_settings_valid(&settings));
    return 0;
}

static int test_button_wake_enters_launcher_and_navigation_is_deterministic(void) {
    watchy_shell_t shell;

    watchy_shell_begin(&shell, WATCHY_WAKE_BUTTON, true, false, false);
    CHECK(WATCHY_SHELL_LAUNCHER_ITEMS == 3u);
    CHECK(WATCHY_SHELL_VISIBLE_ROWS == 3u);
    CHECK(shell.screen == WATCHY_SHELL_LAUNCHER);
    CHECK(shell.selection == 0u);

    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.screen == WATCHY_SHELL_WATCHFACE_SELECTOR);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_BACK);
    CHECK(shell.screen == WATCHY_SHELL_LAUNCHER);

    shell.selection = 1u;
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.screen == WATCHY_SHELL_PACKAGE_APPS);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_BACK);

    shell.selection = 2u;
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.screen == WATCHY_SHELL_SETTINGS);
    CHECK(shell.selection == 0u);

    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_BACK);
    CHECK(shell.screen == WATCHY_SHELL_LAUNCHER);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_UP);
    CHECK(shell.selection == 2u);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_BACK);
    CHECK(shell.screen == WATCHY_SHELL_WATCHFACE);
    CHECK(shell.sleep_requested);
    return 0;
}

static int test_settings_have_exact_order_routes_and_wrap(void) {
    watchy_shell_t shell;

    watchy_shell_begin(&shell, WATCHY_WAKE_BUTTON, true, false, false);
    shell.screen = WATCHY_SHELL_SETTINGS;
    shell.return_screen = WATCHY_SHELL_LAUNCHER;

    shell.selection = 0u;
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_UP);
    CHECK(shell.selection == 9u);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    CHECK(shell.selection == 0u);

    for (uint8_t selection = 0u; selection < 3u; ++selection) {
        shell.screen = WATCHY_SHELL_SETTINGS;
        shell.selection = selection;
        watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
        CHECK(watchy_shell_take_action(&shell) == WATCHY_SHELL_ACTION_SAVE_SETTINGS);
    }

    shell.screen = WATCHY_SHELL_SETTINGS;
    shell.selection = 3u;
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.screen == WATCHY_SHELL_MANUAL_TIME);
    CHECK(shell.return_screen == WATCHY_SHELL_SETTINGS);

    shell.screen = WATCHY_SHELL_SETTINGS;
    shell.selection = 4u;
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.screen == WATCHY_SHELL_NTP_SYNC);
    CHECK(shell.return_screen == WATCHY_SHELL_SETTINGS);
    CHECK(watchy_shell_take_action(&shell) == WATCHY_SHELL_ACTION_SYNC_NTP);

    shell.screen = WATCHY_SHELL_SETTINGS;
    shell.selection = 5u;
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.screen == WATCHY_SHELL_CONNECTIVITY);
    CHECK(shell.return_screen == WATCHY_SHELL_SETTINGS);

    shell.screen = WATCHY_SHELL_SETTINGS;
    shell.selection = 6u;
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.screen == WATCHY_SHELL_PACKAGE_PORTAL);
    CHECK(shell.return_screen == WATCHY_SHELL_SETTINGS);

    shell.screen = WATCHY_SHELL_SETTINGS;
    shell.selection = 7u;
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(watchy_shell_take_action(&shell) == WATCHY_SHELL_ACTION_SAVE_SETTINGS);

    shell.screen = WATCHY_SHELL_SETTINGS;
    shell.selection = 8u;
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.screen == WATCHY_SHELL_DIAGNOSTICS);
    CHECK(shell.return_screen == WATCHY_SHELL_SETTINGS);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_BACK);
    CHECK(shell.screen == WATCHY_SHELL_SETTINGS);

    shell.screen = WATCHY_SHELL_SETTINGS;
    shell.selection = 9u;
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.screen == WATCHY_SHELL_ABOUT);
    CHECK(shell.return_screen == WATCHY_SHELL_SETTINGS);
    return 0;
}

static int test_safe_mode_cold_boot_bypasses_normal_routes(void) {
    watchy_shell_t shell;

    watchy_shell_begin(&shell, WATCHY_WAKE_COLD, true, true, true);
    CHECK(shell.safe_mode);
    CHECK(shell.screen == WATCHY_SHELL_SAFE_MODE);
    CHECK(!shell.package_warning);

    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(watchy_shell_take_action(&shell) == WATCHY_SHELL_ACTION_PURGE_PACKAGES);
    CHECK(shell.screen == WATCHY_SHELL_SAFE_MODE);

    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(watchy_shell_take_action(&shell) == WATCHY_SHELL_ACTION_NORMAL_REBOOT);

    watchy_shell_begin(&shell, WATCHY_WAKE_RTC, true, true, false);
    CHECK(shell.safe_mode);
    CHECK(shell.screen == WATCHY_SHELL_SAFE_MODE);
    CHECK(shell.sleep_requested);
    return 0;
}

static int test_invalid_rtc_routes_to_manual_recovery_and_timer_safe_mode_is_low_duty(void) {
    watchy_shell_t shell;
    watchy_shell_begin(&shell, WATCHY_WAKE_BUTTON, true, false, false);
    watchy_shell_require_manual_time(&shell, true);
    CHECK(shell.screen == WATCHY_SHELL_MANUAL_TIME);
    CHECK(!shell.sleep_requested);

    watchy_shell_begin(&shell, WATCHY_WAKE_TIMER, true, false, false);
    watchy_shell_require_manual_time(&shell, false);
    CHECK(shell.screen == WATCHY_SHELL_MANUAL_TIME);
    CHECK(shell.sleep_requested);

    watchy_shell_begin(&shell, WATCHY_WAKE_TIMER, true, true, false);
    CHECK(shell.screen == WATCHY_SHELL_SAFE_MODE);
    CHECK(shell.sleep_requested);
    return 0;
}

static int test_wake_and_idle_policy_return_to_builtin_watchface_before_sleep(void) {
    watchy_shell_t shell;

    watchy_shell_begin(&shell, WATCHY_WAKE_RTC, true, false, false);
    CHECK(shell.screen == WATCHY_SHELL_WATCHFACE);
    CHECK(!shell.sleep_requested);

    watchy_shell_begin(&shell, WATCHY_WAKE_MOTION, false, false, false);
    CHECK(shell.screen == WATCHY_SHELL_WATCHFACE);
    CHECK(shell.sleep_requested);

    watchy_shell_begin(&shell, WATCHY_WAKE_BUTTON, true, false, false);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_IDLE);
    CHECK(shell.screen == WATCHY_SHELL_WATCHFACE);
    CHECK(shell.sleep_requested);
    return 0;
}

static int test_failed_package_render_keeps_builtin_watchface_with_warning(void) {
    watchy_shell_t shell;

    watchy_shell_begin(&shell, WATCHY_WAKE_RTC, true, false, true);
    CHECK(shell.screen == WATCHY_SHELL_WATCHFACE);
    CHECK(shell.package_warning);
    CHECK(watchy_shell_should_render_builtin(&shell));
    return 0;
}

static int test_manual_time_editor_and_portal_modes_require_explicit_selection(void) {
    watchy_shell_t shell;

    watchy_shell_begin(&shell, WATCHY_WAKE_BUTTON, true, false, false);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.screen == WATCHY_SHELL_MANUAL_TIME);
    CHECK(!shell.editing);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.editing);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_UP);
    CHECK(watchy_shell_take_action(&shell) == WATCHY_SHELL_ACTION_MANUAL_INCREMENT);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_BACK);
    CHECK(watchy_shell_take_action(&shell) == WATCHY_SHELL_ACTION_SAVE_MANUAL_TIME);
    CHECK(shell.screen == WATCHY_SHELL_SETTINGS);

    shell.selection = 6u;
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.screen == WATCHY_SHELL_PACKAGE_PORTAL);
    CHECK(watchy_shell_take_action(&shell) == WATCHY_SHELL_ACTION_NONE);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(watchy_shell_take_action(&shell) == WATCHY_SHELL_ACTION_START_PORTAL_CLIENT);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(watchy_shell_take_action(&shell) == WATCHY_SHELL_ACTION_START_PORTAL_AP);
    return 0;
}

static int test_package_list_selects_an_installed_app_for_execution(void) {
    watchy_shell_t shell;
    watchy_package_catalog_t catalog = {0};
    catalog.count = 3u;
    strcpy(catalog.packages[0].package_ref, "face.clock@1");
    catalog.packages[0].type = WATCHY_PACKAGE_TYPE_WATCHFACE;
    strcpy(catalog.packages[1].package_ref, "app.timer@1");
    catalog.packages[1].type = WATCHY_PACKAGE_TYPE_APP;
    strcpy(catalog.packages[2].package_ref, "app.weather@2");
    catalog.packages[2].type = WATCHY_PACKAGE_TYPE_APP;
    watchy_shell_begin(&shell, WATCHY_WAKE_BUTTON, true, false, false);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.screen == WATCHY_SHELL_PACKAGE_APPS);
    watchy_shell_set_package_catalog(&shell, &catalog, true);
    CHECK(shell.app_count == 2u);
    CHECK(shell.face_count == 2u);
    CHECK(shell.app_indices[0] == 1u && shell.app_indices[1] == 2u);
    CHECK(shell.face_indices[0] == 0u);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    size_t catalog_index = 0u;
    CHECK(watchy_shell_selected_package(&shell, &catalog_index));
    CHECK(catalog_index == 2u);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(watchy_shell_take_action(&shell) == WATCHY_SHELL_ACTION_RUN_PACKAGE);
    return 0;
}

static void populate_apps(watchy_package_catalog_t *catalog, size_t count) {
    memset(catalog, 0, sizeof(*catalog));
    catalog->count = count;
    for (size_t index = 0u; index < count; ++index) {
        snprintf(catalog->packages[index].package_ref,
                 sizeof(catalog->packages[index].package_ref), "app.%02u@1", (unsigned)index);
        catalog->packages[index].type = WATCHY_PACKAGE_TYPE_APP;
    }
}

static int test_app_launcher_pages_and_maps_zero_three_four_and_sixteen_apps(void) {
    static const size_t counts[] = {0u, 3u, 4u, 16u};
    watchy_shell_t shell;
    watchy_package_catalog_t catalog;
    for (size_t case_index = 0u; case_index < sizeof(counts) / sizeof(counts[0]); ++case_index) {
        populate_apps(&catalog, counts[case_index]);
        watchy_shell_begin(&shell, WATCHY_WAKE_BUTTON, true, false, false);
        watchy_shell_set_package_catalog(&shell, &catalog, true);
        shell.screen = WATCHY_SHELL_PACKAGE_APPS;
        CHECK(shell.app_count == counts[case_index]);
        CHECK(watchy_shell_package_page_start(&shell) == 0u);
        if (counts[case_index] == 0u) {
            CHECK(!watchy_shell_selected_package(&shell, NULL));
            watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
            CHECK(watchy_shell_take_action(&shell) == WATCHY_SHELL_ACTION_NONE);
            continue;
        }
        for (size_t index = 0u; index < counts[case_index] - 1u; ++index) {
            watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
        }
        size_t catalog_index = SIZE_MAX;
        CHECK(watchy_shell_selected_package(&shell, &catalog_index));
        CHECK(catalog_index == counts[case_index] - 1u);
        CHECK(watchy_shell_package_page_start(&shell) ==
              ((counts[case_index] - 1u) / WATCHY_SHELL_VISIBLE_ROWS) *
                  WATCHY_SHELL_VISIBLE_ROWS);
    }
    return 0;
}

static void populate_faces(watchy_package_catalog_t *catalog, size_t count) {
    memset(catalog, 0, sizeof(*catalog));
    catalog->count = count;
    const size_t bounded = count < WATCHY_PACKAGE_INSTALLED_MAX
                               ? count : WATCHY_PACKAGE_INSTALLED_MAX;
    for (size_t index = 0u; index < bounded; ++index) {
        snprintf(catalog->packages[index].package_ref,
                 sizeof(catalog->packages[index].package_ref),
                 "face.%02u@1", (unsigned)index);
        snprintf(catalog->packages[index].name,
                 sizeof(catalog->packages[index].name),
                 "Face %02u", (unsigned)index);
        strcpy(catalog->packages[index].version, "1.0.0");
        catalog->packages[index].type = WATCHY_PACKAGE_TYPE_WATCHFACE;
    }
}

static int test_catalog_builds_independent_bounded_maps_and_clears_stale_rows(void) {
    watchy_shell_t shell;
    watchy_package_catalog_t catalog = {0};

    catalog.count = 6u;
    for (size_t index = 0u; index < catalog.count; ++index) {
        snprintf(catalog.packages[index].package_ref,
                 sizeof(catalog.packages[index].package_ref),
                 "pkg.%02u@1", (unsigned)index);
        catalog.packages[index].type = (index & 1u) == 0u
                                           ? WATCHY_PACKAGE_TYPE_WATCHFACE
                                           : WATCHY_PACKAGE_TYPE_APP;
    }
    watchy_shell_begin(&shell, WATCHY_WAKE_BUTTON, true, false, false);
    watchy_shell_set_package_catalog(&shell, &catalog, true);
    CHECK(shell.app_count == 3u && shell.face_count == 4u);
    CHECK(shell.app_indices[0] == 1u && shell.app_indices[1] == 3u &&
          shell.app_indices[2] == 5u);
    CHECK(shell.face_indices[0] == 0u && shell.face_indices[1] == 2u &&
          shell.face_indices[2] == 4u);

    for (size_t index = 0u; index < WATCHY_PACKAGE_INSTALLED_MAX; ++index) {
        snprintf(catalog.packages[index].package_ref,
                 sizeof(catalog.packages[index].package_ref),
                 "overflow.%02u@1", (unsigned)index);
        catalog.packages[index].type = (index & 1u) == 0u
                                           ? WATCHY_PACKAGE_TYPE_WATCHFACE
                                           : WATCHY_PACKAGE_TYPE_APP;
    }
    catalog.count = SIZE_MAX;
    watchy_shell_set_package_catalog(&shell, &catalog, true);
    CHECK(shell.app_count == WATCHY_PACKAGE_INSTALLED_MAX / 2u);
    CHECK(shell.face_count == WATCHY_PACKAGE_INSTALLED_MAX / 2u + 1u);
    CHECK(shell.recovery_count == WATCHY_PACKAGE_INSTALLED_MAX);
    CHECK(shell.app_indices[shell.app_count - 1u] == WATCHY_PACKAGE_INSTALLED_MAX - 1u);
    CHECK(shell.face_indices[shell.face_count - 2u] == WATCHY_PACKAGE_INSTALLED_MAX - 2u);

    watchy_shell_set_package_catalog(&shell, NULL, false);
    CHECK(!shell.package_index_readable);
    CHECK(shell.app_count == 0u && shell.face_count == 1u && shell.recovery_count == 0u);
    CHECK(!watchy_shell_selected_package(&shell, NULL));

    watchy_shell_set_package_catalog(&shell, &catalog, false);
    CHECK(shell.app_count == 0u && shell.face_count == 1u && shell.recovery_count == 0u);
    return 0;
}

static int test_selector_handles_sizes_pages_wrap_and_healthy_active_cursor(void) {
    static const size_t wpk_counts[] = {0u, 1u, 3u, 4u, WATCHY_PACKAGE_INSTALLED_MAX};
    watchy_shell_t shell;
    watchy_package_catalog_t catalog;

    for (size_t case_index = 0u;
         case_index < sizeof(wpk_counts) / sizeof(wpk_counts[0]); ++case_index) {
        populate_faces(&catalog, wpk_counts[case_index]);
        watchy_shell_begin(&shell, WATCHY_WAKE_BUTTON, true, false, false);
        watchy_shell_set_package_catalog(&shell, &catalog, true);
        watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
        CHECK(shell.screen == WATCHY_SHELL_WATCHFACE_SELECTOR);
        CHECK(shell.face_count == wpk_counts[case_index] + 1u);
        CHECK(shell.selection == 0u);
        watchy_shell_input(&shell, WATCHY_SHELL_INPUT_UP);
        CHECK(shell.selection == shell.face_count - 1u);
        CHECK(watchy_shell_watchface_page_start(&shell) ==
              ((shell.face_count - 1u) / WATCHY_SHELL_VISIBLE_ROWS) *
                  WATCHY_SHELL_VISIBLE_ROWS);
        watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
        CHECK(shell.selection == 0u);
    }

    populate_faces(&catalog, 4u);
    catalog.packages[2].active = true;
    catalog.packages[3].pending = true;
    watchy_shell_begin(&shell, WATCHY_WAKE_BUTTON, true, false, false);
    watchy_shell_set_package_catalog(&shell, &catalog, true);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.selection == 3u);
    size_t catalog_index = SIZE_MAX;
    CHECK(watchy_shell_selected_watchface(&shell, &catalog_index));
    CHECK(catalog_index == 2u);

    catalog.packages[2].quarantined = true;
    watchy_shell_set_package_catalog(&shell, &catalog, true);
    CHECK(shell.face_count == 5u);
    CHECK(shell.selection == 0u);
    catalog.packages[2].active = false;
    catalog.packages[3].pending = true;
    watchy_shell_set_package_catalog(&shell, &catalog, true);
    CHECK(shell.selection == 0u);
    return 0;
}

static int test_selector_never_treats_a_pending_face_as_active(void) {
    watchy_shell_t shell;
    watchy_package_catalog_t catalog;

    populate_faces(&catalog, 1u);
    catalog.packages[0].active = true;
    catalog.packages[0].pending = true;
    watchy_shell_begin(&shell, WATCHY_WAKE_BUTTON, true, false, false);
    watchy_shell_set_package_catalog(&shell, &catalog, true);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);

    CHECK(shell.screen == WATCHY_SHELL_WATCHFACE_SELECTOR);
    CHECK(shell.selection == 0u);
    return 0;
}

static int test_selector_actions_are_copied_one_shot_and_back_cancels(void) {
    watchy_shell_t shell;
    watchy_package_catalog_t catalog;
    watchy_shell_action_request_t request;

    populate_faces(&catalog, 2u);
    watchy_shell_begin(&shell, WATCHY_WAKE_BUTTON, true, false, false);
    watchy_shell_set_package_catalog(&shell, &catalog, true);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_BACK);
    CHECK(shell.screen == WATCHY_SHELL_LAUNCHER);
    CHECK(!watchy_shell_take_action_request(&shell, &request));

    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.screen == WATCHY_SHELL_WATCHFACE);
    CHECK(shell.sleep_requested);
    CHECK(watchy_shell_take_action_request(&shell, &request));
    CHECK(request.action == WATCHY_SHELL_ACTION_SELECT_BUILTIN);
    CHECK(!request.has_package);
    CHECK(!watchy_shell_take_action_request(&shell, &request));
    CHECK(request.action == WATCHY_SHELL_ACTION_NONE);

    watchy_shell_begin(&shell, WATCHY_WAKE_BUTTON, true, false, false);
    watchy_shell_set_package_catalog(&shell, &catalog, true);
    strcpy(catalog.packages[1].package_ref, "caller.mutated@9");
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    shell.selection = 2u;
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.screen == WATCHY_SHELL_WATCHFACE);
    CHECK(shell.sleep_requested);
    watchy_shell_set_package_catalog(&shell, NULL, false);
    CHECK(watchy_shell_take_action_request(&shell, &request));
    CHECK(request.action == WATCHY_SHELL_ACTION_SELECT_WATCHFACE);
    CHECK(request.has_package);
    CHECK(request.catalog_index == 1u);
    CHECK(strcmp(request.package_ref, "face.01@1") == 0);
    CHECK(watchy_shell_take_action(&shell) == WATCHY_SHELL_ACTION_NONE);
    return 0;
}

static int test_quarantined_faces_stay_visible_but_cannot_activate(void) {
    watchy_shell_t shell;
    watchy_package_catalog_t catalog;
    watchy_shell_action_request_t request;
    size_t catalog_index = SIZE_MAX;

    populate_faces(&catalog, 1u);
    catalog.packages[0].quarantined = true;
    watchy_shell_begin(&shell, WATCHY_WAKE_BUTTON, true, false, false);
    watchy_shell_set_package_catalog(&shell, &catalog, true);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.face_count == 2u);
    shell.selection = 1u;
    CHECK(watchy_shell_selected_watchface(&shell, &catalog_index));
    CHECK(catalog_index == 0u);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.screen == WATCHY_SHELL_ERROR);
    CHECK(shell.error == WATCHY_SHELL_ERROR_PACKAGE);
    CHECK(!watchy_shell_take_action_request(&shell, &request));
    return 0;
}

static int test_safe_mode_selector_exposes_only_hairline_and_never_selects_wpk(void) {
    watchy_shell_t shell;
    watchy_package_catalog_t catalog;
    watchy_shell_action_request_t request;

    populate_faces(&catalog, 4u);
    watchy_shell_begin(&shell, WATCHY_WAKE_COLD, true, true, false);
    watchy_shell_set_package_catalog(&shell, &catalog, true);
    CHECK(shell.app_count == 0u);
    CHECK(shell.face_count == 1u);
    CHECK(shell.recovery_count == 4u);
    shell.screen = WATCHY_SHELL_WATCHFACE_SELECTOR;
    shell.return_screen = WATCHY_SHELL_SAFE_MODE;
    shell.selection = 0u;
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    CHECK(shell.selection == 0u);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(watchy_shell_take_action_request(&shell, &request));
    CHECK(request.action == WATCHY_SHELL_ACTION_SELECT_BUILTIN);
    CHECK(request.action != WATCHY_SHELL_ACTION_SELECT_WATCHFACE);
    return 0;
}

static int test_package_labels_are_bounded_and_disambiguated(void) {
    watchy_package_info_t first = {0};
    watchy_package_info_t second = {0};
    char first_label[WATCHY_SHELL_PACKAGE_LABEL_SIZE];
    char second_label[WATCHY_SHELL_PACKAGE_LABEL_SIZE];
    strcpy(first.package_ref, "app.really.long.shared.prefix.alpha@123456789");
    strcpy(second.package_ref, "app.really.long.shared.prefix.bravo@123456789");
    CHECK(watchy_shell_format_package_label(&first, 7u, 16u, first_label,
                                            sizeof(first_label)));
    CHECK(watchy_shell_format_package_label(&second, 8u, 16u, second_label,
                                            sizeof(second_label)));
    CHECK(strnlen(first_label, sizeof(first_label)) < sizeof(first_label));
    CHECK(strnlen(second_label, sizeof(second_label)) < sizeof(second_label));
    CHECK(strcmp(first_label, second_label) != 0);
    CHECK(strstr(first_label, "08/16") != NULL);
    CHECK(strstr(second_label, "09/16") != NULL);
    return 0;
}

static int test_safe_mode_uses_readable_metadata_for_individual_removal(void) {
    watchy_shell_t shell;
    watchy_package_catalog_t catalog;
    populate_apps(&catalog, 2u);
    strcpy(catalog.packages[0].package_ref, "face.clock@1");
    catalog.packages[0].type = WATCHY_PACKAGE_TYPE_WATCHFACE;
    watchy_shell_begin(&shell, WATCHY_WAKE_COLD, true, true, false);
    watchy_shell_set_package_catalog(&shell, &catalog, true);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.screen == WATCHY_SHELL_PACKAGE_APPS);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(watchy_shell_take_action(&shell) == WATCHY_SHELL_ACTION_REMOVE_PACKAGE);
    size_t catalog_index = SIZE_MAX;
    CHECK(watchy_shell_selected_package(&shell, &catalog_index));
    CHECK(catalog_index == 1u);

    watchy_shell_begin(&shell, WATCHY_WAKE_COLD, true, true, false);
    watchy_shell_set_package_catalog(&shell, NULL, false);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(watchy_shell_take_action(&shell) == WATCHY_SHELL_ACTION_PURGE_PACKAGES);
    return 0;
}

static int test_all_operation_failures_enter_a_bounded_safe_error_screen(void) {
    static const watchy_shell_error_t failures[] = {
        WATCHY_SHELL_ERROR_SETTINGS_LOAD,
        WATCHY_SHELL_ERROR_SETTINGS_SAVE,
        WATCHY_SHELL_ERROR_MANUAL_TIME,
        WATCHY_SHELL_ERROR_NTP,
        WATCHY_SHELL_ERROR_PORTAL,
        WATCHY_SHELL_ERROR_DISPLAY,
        WATCHY_SHELL_ERROR_PACKAGE,
    };
    watchy_shell_t shell;
    for (size_t index = 0u; index < sizeof(failures) / sizeof(failures[0]); ++index) {
        watchy_shell_begin(&shell, WATCHY_WAKE_BUTTON, true, false, false);
        shell.screen = WATCHY_SHELL_SETTINGS;
        shell.pending_action = WATCHY_SHELL_ACTION_SAVE_SETTINGS;
        watchy_shell_fail(&shell, failures[index]);
        const char *message = watchy_shell_error_message(&shell);
        CHECK(shell.screen == WATCHY_SHELL_ERROR);
        CHECK(shell.pending_action == WATCHY_SHELL_ACTION_NONE);
        CHECK(message != NULL && message[0] != '\0');
        CHECK(strlen(message) <= WATCHY_SHELL_ERROR_MESSAGE_MAX);
        CHECK(strchr(message, '/') == NULL);
        CHECK(strchr(message, '\\') == NULL);
        watchy_shell_input(&shell, WATCHY_SHELL_INPUT_BACK);
        CHECK(shell.screen == WATCHY_SHELL_SETTINGS);
    }
    return 0;
}

static int test_diagnostics_distinguish_passive_status_from_active_acceptance(void) {
    static const watchy_diagnostic_state_t states[] = {
        WATCHY_DIAGNOSTIC_PASS,
        WATCHY_DIAGNOSTIC_FAIL,
        WATCHY_DIAGNOSTIC_UNAVAILABLE,
        WATCHY_DIAGNOSTIC_STOPPED,
    };
    static const char *const expected[] = {"READY", "FAIL", "N/A", "OFF"};
    watchy_shell_t shell;
    char label[WATCHY_SHELL_DIAGNOSTIC_LABEL_SIZE];
    watchy_shell_begin(&shell, WATCHY_WAKE_BUTTON, true, false, false);
    shell.screen = WATCHY_SHELL_DIAGNOSTICS;
    watchy_shell_set_diagnostic_count(&shell, WATCHY_DIAGNOSTIC_ENTRY_COUNT);
    for (size_t index = 0u; index < sizeof(states) / sizeof(states[0]); ++index) {
        const watchy_diagnostic_entry_t entry = {
            .service = "storage", .state = states[index], .status_code = -123,
            .scope = WATCHY_DIAGNOSTIC_PASSIVE,
            .detail = "must not be displayed",
        };
        CHECK(watchy_shell_format_diagnostic_label(&entry, label, sizeof(label)));
        CHECK(strstr(label, "STORAGE") != NULL);
        CHECK(strstr(label, expected[index]) != NULL);
        CHECK(strstr(label, "must") == NULL);
    }
    const watchy_diagnostic_entry_t active = {
        .service = "buttons", .state = WATCHY_DIAGNOSTIC_PASS,
        .scope = WATCHY_DIAGNOSTIC_ACTIVE_ACCEPTANCE, .status_code = 0,
        .detail = "bounded interactive exercise",
    };
    CHECK(watchy_shell_format_diagnostic_label(&active, label, sizeof(label)));
    CHECK(strstr(label, "PASS") != NULL);
    CHECK(strstr(label, "READY") == NULL);
    for (size_t index = 0u; index < WATCHY_SHELL_PACKAGE_PAGE_ITEMS; ++index) {
        watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    }
    CHECK(shell.selection == WATCHY_SHELL_PACKAGE_PAGE_ITEMS);
    CHECK(watchy_shell_diagnostic_page_start(&shell) == WATCHY_SHELL_PACKAGE_PAGE_ITEMS);
    return 0;
}

static bool pixel_black(const uint8_t *framebuffer, int x, int y) {
    const size_t offset = (size_t)y * 25u + (size_t)x / 8u;
    return (framebuffer[offset] & (uint8_t)(0x80u >> ((unsigned)x & 7u))) == 0u;
}

static int test_compact_font_draws_and_clips_real_framebuffer_pixels(void) {
    uint8_t framebuffer[5000];
    watchy_canvas_t canvas = {
        .width = 200u, .height = 200u, .stride = 25u,
        .format = WATCHY_PIXEL_MONO, .pixels = framebuffer,
    };

    memset(framebuffer, 0xff, sizeof(framebuffer));
    watchy_ui_draw_text(&canvas, 0, 0, "A", 1u, true);
    CHECK(pixel_black(framebuffer, 1, 0));
    CHECK(pixel_black(framebuffer, 0, 1));
    CHECK(pixel_black(framebuffer, 4, 1));
    CHECK(pixel_black(framebuffer, 0, 3));
    CHECK(pixel_black(framebuffer, 4, 3));
    CHECK(!pixel_black(framebuffer, 2, 1));

    watchy_ui_draw_text(&canvas, 198, 198, "W", 2u, true);
    CHECK(pixel_black(framebuffer, 198, 198));
    return 0;
}

int main(void) {
    int failures = 0;
    failures += test_invalid_persisted_settings_fall_back_field_by_field();
    failures += test_display_motion_level_defaults_sanitizes_and_cycles();
    failures += test_shell_transition_mapping_returns_complete_safe_requests();
    failures += test_presentation_outcomes_gate_sleep_and_post_action_refresh();
    failures += test_wifi_settings_accept_only_open_or_valid_psk_credentials();
    failures += test_erased_settings_can_be_provisioned_and_reused_after_reload();
    failures += test_persisted_timezone_hostname_and_wpa_strings_are_syntactically_strict();
    failures += test_button_wake_enters_launcher_and_navigation_is_deterministic();
    failures += test_settings_have_exact_order_routes_and_wrap();
    failures += test_safe_mode_cold_boot_bypasses_normal_routes();
    failures += test_invalid_rtc_routes_to_manual_recovery_and_timer_safe_mode_is_low_duty();
    failures += test_wake_and_idle_policy_return_to_builtin_watchface_before_sleep();
    failures += test_failed_package_render_keeps_builtin_watchface_with_warning();
    failures += test_manual_time_editor_and_portal_modes_require_explicit_selection();
    failures += test_package_list_selects_an_installed_app_for_execution();
    failures += test_app_launcher_pages_and_maps_zero_three_four_and_sixteen_apps();
    failures += test_catalog_builds_independent_bounded_maps_and_clears_stale_rows();
    failures += test_selector_handles_sizes_pages_wrap_and_healthy_active_cursor();
    failures += test_selector_never_treats_a_pending_face_as_active();
    failures += test_selector_actions_are_copied_one_shot_and_back_cancels();
    failures += test_quarantined_faces_stay_visible_but_cannot_activate();
    failures += test_safe_mode_selector_exposes_only_hairline_and_never_selects_wpk();
    failures += test_package_labels_are_bounded_and_disambiguated();
    failures += test_safe_mode_uses_readable_metadata_for_individual_removal();
    failures += test_all_operation_failures_enter_a_bounded_safe_error_screen();
    failures += test_diagnostics_distinguish_passive_status_from_active_acceptance();
    failures += test_compact_font_draws_and_clips_real_framebuffer_pixels();
    if (failures == 0) {
        puts("shell tests passed");
    }
    return failures == 0 ? 0 : 1;
}
