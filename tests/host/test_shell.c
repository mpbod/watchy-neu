#include <stdio.h>
#include <string.h>

#include "watchy/settings.h"
#include "watchy/shell.h"
#include "watchy/ui.h"

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

static int test_button_wake_enters_launcher_and_navigation_is_deterministic(void) {
    watchy_shell_t shell;

    watchy_shell_begin(&shell, WATCHY_WAKE_BUTTON, true, false, false);
    CHECK(shell.screen == WATCHY_SHELL_LAUNCHER);
    CHECK(shell.selection == 0u);

    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    CHECK(shell.selection == 2u);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.screen == WATCHY_SHELL_SETTINGS);
    CHECK(shell.selection == 0u);

    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_BACK);
    CHECK(shell.screen == WATCHY_SHELL_LAUNCHER);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_UP);
    CHECK(shell.selection == WATCHY_SHELL_LAUNCHER_ITEMS - 1u);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_BACK);
    CHECK(shell.screen == WATCHY_SHELL_WATCHFACE);
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

    shell.selection = 5u;
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
    watchy_shell_begin(&shell, WATCHY_WAKE_BUTTON, true, false, false);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(shell.screen == WATCHY_SHELL_PACKAGE_APPS);
    watchy_shell_set_package_count(&shell, 2u);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_DOWN);
    watchy_shell_input(&shell, WATCHY_SHELL_INPUT_MENU);
    CHECK(watchy_shell_take_action(&shell) == WATCHY_SHELL_ACTION_RUN_PACKAGE);
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
    failures += test_wifi_settings_accept_only_open_or_valid_psk_credentials();
    failures += test_button_wake_enters_launcher_and_navigation_is_deterministic();
    failures += test_safe_mode_cold_boot_bypasses_normal_routes();
    failures += test_wake_and_idle_policy_return_to_builtin_watchface_before_sleep();
    failures += test_failed_package_render_keeps_builtin_watchface_with_warning();
    failures += test_manual_time_editor_and_portal_modes_require_explicit_selection();
    failures += test_package_list_selects_an_installed_app_for_execution();
    failures += test_compact_font_draws_and_clips_real_framebuffer_pixels();
    if (failures == 0) {
        puts("shell tests passed");
    }
    return failures == 0 ? 0 : 1;
}
