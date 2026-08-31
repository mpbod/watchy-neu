#include "watchy/display.h"
#include "watchy/shell_render.h"

#include <stdio.h>
#include <string.h>

#ifndef WATCHY_GOLDEN_DIR
#define WATCHY_GOLDEN_DIR "tests/golden/shell"
#endif

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static bool black(const uint8_t *fb, int x, int y) {
    if (x < 0 || y < 0 || x >= 200 || y >= 200) return false;
    return (fb[(size_t)y * WATCHY_DISPLAY_STRIDE + (size_t)x / 8u] &
            (uint8_t)(0x80u >> (x & 7))) == 0u;
}

static int region_ink(const uint8_t *fb, int left, int top, int right, int bottom) {
    int ink = 0;
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x)
            ink += black(fb, x, y) ? 1 : 0;
    return ink;
}

static watchy_canvas_t canvas(uint8_t *fb) {
    watchy_canvas_t c = {200u, 200u, WATCHY_DISPLAY_STRIDE, 0u, WATCHY_PIXEL_MONO, fb};
    memset(fb, 0xff, WATCHY_DISPLAY_FRAMEBUFFER_SIZE);
    return c;
}

static watchy_time_t now(void) { return (watchy_time_t){2026, 8u, 31u, 12u, 34u, 0u, 1u, 0}; }

static void draw(watchy_canvas_t *c, watchy_shell_t *s, watchy_settings_t *settings,
                 const watchy_time_t *t, const watchy_battery_state_t *battery,
                 const watchy_package_catalog_t *catalog,
                 const watchy_diagnostic_report_t *report, const char *detail) {
    watchy_shell_render(c, s, settings, t, battery, catalog, report, detail);
}

static int read_pbm(const char *path, const uint8_t *fb) {
    FILE *f = fopen(path, "rb");
    char line[32];
    uint8_t expected[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    CHECK(f != NULL);
    CHECK(fgets(line, sizeof(line), f) != NULL && strcmp(line, "P4\n") == 0);
    CHECK(fgets(line, sizeof(line), f) != NULL && strcmp(line, "200 200\n") == 0);
    CHECK(fread(expected, 1u, sizeof(expected), f) == sizeof(expected));
    CHECK(fgetc(f) == EOF);
    fclose(f);
    CHECK(memcmp(expected, fb, sizeof(expected)) == 0);
    return 0;
}

static void write_pbm(const char *path, const uint8_t *fb) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) return;
    (void)fputs("P4\n200 200\n", f);
    (void)fwrite(fb, 1u, WATCHY_DISPLAY_FRAMEBUFFER_SIZE, f);
    (void)fclose(f);
}

static void catalog_faces(watchy_package_catalog_t *catalog) {
    memset(catalog, 0, sizeof(*catalog));
    catalog->count = 4u;
    for (size_t i = 0u; i < catalog->count; ++i) {
        snprintf(catalog->packages[i].package_ref, sizeof(catalog->packages[i].package_ref),
                 "face-%zu@1.0.%zu", i, i);
        snprintf(catalog->packages[i].name, sizeof(catalog->packages[i].name), "Face %zu", i);
        snprintf(catalog->packages[i].version, sizeof(catalog->packages[i].version), "1.0.%zu", i);
        catalog->packages[i].type = WATCHY_PACKAGE_TYPE_WATCHFACE;
    }
    catalog->packages[0].active = true;
    catalog->packages[1].pending = true;
    catalog->packages[2].quarantined = true;
}

static int test_menu(void) {
    uint8_t fb[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    watchy_canvas_t c = canvas(fb);
    watchy_shell_t s = {.screen = WATCHY_SHELL_LAUNCHER, .selection = 1u};
    watchy_settings_t settings = {.time_24h = true};
    watchy_time_t t = now();
    draw(&c, &s, &settings, &t, NULL, NULL, NULL, NULL);
    CHECK(black(fb, 0, 0) && black(fb, 186, 24));
    CHECK(!black(fb, 188, 24) && black(fb, 187, 50) && !black(fb, 188, 50));
    CHECK(black(fb, 2, 80) && black(fb, 180, 134));
    CHECK(!black(fb, 2, 79) && !black(fb, 2, 135));
    CHECK(region_ink(fb, 44, 25, 180, 79) > 20);
    CHECK(region_ink(fb, 44, 80, 180, 134) > 20);
    CHECK(region_ink(fb, 44, 135, 180, 189) > 20);
    CHECK(read_pbm(WATCHY_GOLDEN_DIR "/menu.pbm", fb) == 0);
    return 0;
}

static int test_menu_selections(void) {
    uint8_t frames[3][WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    watchy_settings_t settings = {.time_24h = true};
    watchy_time_t t = now();
    for (unsigned selection = 0u; selection < 3u; ++selection) {
        watchy_canvas_t c = canvas(frames[selection]);
        watchy_shell_t s = {.screen = WATCHY_SHELL_LAUNCHER, .selection = (uint8_t)selection};
        draw(&c, &s, &settings, &t, NULL, NULL, NULL, NULL);
        CHECK(black(frames[selection], 2, (int)(25u + selection * 55u)));
        CHECK(!black(frames[selection], 2, (int)(25u + ((selection + 1u) % 3u) * 55u)));
    }
    CHECK(memcmp(frames[0], frames[1], sizeof(frames[0])) != 0);
    CHECK(memcmp(frames[1], frames[2], sizeof(frames[1])) != 0);
    return 0;
}

static int test_selector(void) {
    uint8_t fb[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    watchy_canvas_t c = canvas(fb);
    watchy_shell_t s = {.screen = WATCHY_SHELL_WATCHFACE_SELECTOR, .face_count = 5u};
    watchy_settings_t settings = {.time_24h = true};
    watchy_time_t t = now();
    watchy_package_catalog_t catalog;
    catalog_faces(&catalog);
    for (size_t i = 0u; i < 4u; ++i) s.face_indices[i] = (uint8_t)i;
    uint8_t status_frames[5][WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    static const char *const status_contract[] = {
        "BUILT-IN", "ACTIVE", "PENDING", "QUARANTINED", "1.0.3",
    };
    for (unsigned selection = 0u; selection < 5u; ++selection) {
        s.selection = (uint8_t)selection;
        draw(&c, &s, &settings, &t, NULL, &catalog, NULL, NULL);
        memcpy(status_frames[selection], fb, sizeof(fb));
        CHECK(region_ink(fb, 44, 25, 180, 189) > 25);
        CHECK(region_ink(fb, 190, 20, 196, 179) > 15);
    }
    (void)status_contract;
    CHECK(memcmp(status_frames[1], status_frames[2], sizeof(status_frames[1])) != 0);
    CHECK(memcmp(status_frames[2], status_frames[3], sizeof(status_frames[2])) != 0);
    CHECK(memcmp(status_frames[3], status_frames[4], sizeof(status_frames[3])) != 0);
    s.selection = 0u;
    draw(&c, &s, &settings, &t, NULL, &catalog, NULL, NULL);
    CHECK(read_pbm(WATCHY_GOLDEN_DIR "/selector-hairline.pbm", fb) == 0);
    s.selection = 1u;
    draw(&c, &s, &settings, &t, NULL, &catalog, NULL, NULL);
    CHECK(read_pbm(WATCHY_GOLDEN_DIR "/selector-active-wpk.pbm", fb) == 0);
    return 0;
}

static int test_settings(void) {
    uint8_t frames[10][WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    watchy_settings_t settings = {.time_24h = true, .motion_wake = true,
                                  .transition_level = WATCHY_TRANSITION_LEVEL_REDUCED,
                                  .partial_refresh_limit = 20u};
    watchy_time_t t = now();
    for (unsigned selection = 0u; selection < 10u; ++selection) {
        watchy_canvas_t c = canvas(frames[selection]);
        watchy_shell_t s = {.screen = WATCHY_SHELL_SETTINGS, .selection = (uint8_t)selection};
        draw(&c, &s, &settings, &t, NULL, NULL, NULL, NULL);
        CHECK(region_ink(frames[selection], 44, 25, 180, 189) > 20);
    }
    CHECK(memcmp(frames[0], frames[3], sizeof(frames[0])) != 0);
    CHECK(memcmp(frames[3], frames[6], sizeof(frames[3])) != 0);
    CHECK(memcmp(frames[6], frames[9], sizeof(frames[6])) != 0);
    return 0;
}

static int test_hairline(void) {
    static const unsigned percentages[] = {0u, 68u, 100u, 255u};
    uint8_t fb[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    watchy_settings_t settings = {.time_24h = true};
    watchy_time_t t = now();
    for (size_t i = 0u; i < sizeof(percentages) / sizeof(percentages[0]); ++i) {
        watchy_canvas_t c = canvas(fb);
        watchy_shell_t s = {.screen = WATCHY_SHELL_WATCHFACE};
        watchy_battery_state_t b = {3800u, (uint8_t)percentages[i], false};
        draw(&c, &s, &settings, &t, &b, NULL, NULL, NULL);
        const unsigned width = percentages[i] <= 100u ? (percentages[i] * 200u + 50u) / 100u : 0u;
        for (unsigned x = 0u; x < 200u; ++x) {
            CHECK(black(fb, (int)x, 197) == (x < width));
            CHECK(black(fb, (int)x, 199) == (x < width));
        }
        CHECK(region_ink(fb, 50, 45, 150, 100) > 25);
        CHECK(region_ink(fb, 60, 155, 140, 180) > 4);
    }
    return 0;
}

static int test_legacy_and_guards(void) {
    static const watchy_shell_screen_t screens[] = {
        WATCHY_SHELL_PACKAGE_APPS, WATCHY_SHELL_MANUAL_TIME, WATCHY_SHELL_NTP_SYNC,
        WATCHY_SHELL_CONNECTIVITY, WATCHY_SHELL_PACKAGE_PORTAL, WATCHY_SHELL_DIAGNOSTICS,
        WATCHY_SHELL_ABOUT, WATCHY_SHELL_ERROR, WATCHY_SHELL_SAFE_MODE,
    };
    uint8_t storage[WATCHY_DISPLAY_FRAMEBUFFER_SIZE + 2u];
    watchy_settings_t settings = {.time_24h = true};
    watchy_time_t t = now();
    watchy_package_catalog_t catalog;
    watchy_diagnostic_report_t report = {0};
    catalog_faces(&catalog);
    report.count = 2u;
    report.entries[0] = (watchy_diagnostic_entry_t){"wifi", WATCHY_DIAGNOSTIC_PASS,
                                                     WATCHY_DIAGNOSTIC_PASSIVE, 0, "connected"};
    report.entries[1] = (watchy_diagnostic_entry_t){"clock", WATCHY_DIAGNOSTIC_FAIL,
                                                     WATCHY_DIAGNOSTIC_ACTIVE_ACCEPTANCE, -1, "bad"};
    for (size_t i = 0u; i < sizeof(screens) / sizeof(screens[0]); ++i) {
        watchy_canvas_t c = {200u, 200u, WATCHY_DISPLAY_STRIDE, 0u, WATCHY_PIXEL_MONO, storage + 1u};
        watchy_shell_t s = {.screen = screens[i], .app_count = 2u, .recovery_count = 2u,
                            .package_index_readable = true, .error = WATCHY_SHELL_ERROR_PACKAGE};
        s.app_indices[0] = 0u; s.app_indices[1] = 1u;
        s.recovery_indices[0] = 0u; s.recovery_indices[1] = 1u;
        memset(storage, 0x5a, sizeof(storage));
        memset(storage + 1u, 0xff, WATCHY_DISPLAY_FRAMEBUFFER_SIZE);
        draw(&c, &s, &settings, &t, NULL, &catalog, &report, "STATUS DETAIL\nSECOND LINE");
        CHECK(storage[0] == 0x5a && storage[sizeof(storage) - 1u] == 0x5a);
        CHECK(region_ink(storage + 1u, 0, 0, 199, 199) > 20);
    }
    return 0;
}

static int test_apps_filter_and_page(void) {
    uint8_t first[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    uint8_t second[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    watchy_canvas_t c;
    watchy_settings_t settings = {.time_24h = true};
    watchy_time_t t = now();
    watchy_package_catalog_t catalog;
    watchy_shell_t shell = {.screen = WATCHY_SHELL_PACKAGE_APPS, .app_count = 4u};
    catalog_faces(&catalog);
    shell.app_indices[0] = 3u;
    shell.app_indices[1] = 0u;
    shell.app_indices[2] = 2u;
    shell.app_indices[3] = 1u;
    c = canvas(first);
    draw(&c, &shell, &settings, &t, NULL, &catalog, NULL, NULL);
    shell.selection = 3u;
    c = canvas(second);
    draw(&c, &shell, &settings, &t, NULL, &catalog, NULL, NULL);
    CHECK(memcmp(first, second, sizeof(first)) != 0);
    CHECK(region_ink(first, 9, 31, 185, 55) > 4);
    CHECK(region_ink(second, 9, 31, 185, 55) > 4);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--update-goldens") == 0) {
        uint8_t fb[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
        watchy_canvas_t c = canvas(fb);
        watchy_settings_t settings = {.time_24h = true};
        watchy_time_t t = now();
        watchy_shell_t shell = {.screen = WATCHY_SHELL_LAUNCHER, .selection = 1u};
        draw(&c, &shell, &settings, &t, NULL, NULL, NULL, NULL);
        write_pbm("tests/golden/shell/menu.pbm", fb);
        shell.screen = WATCHY_SHELL_WATCHFACE;
        draw(&c, &shell, &settings, &t, NULL, NULL, NULL, NULL);
        write_pbm("tests/golden/shell/hairline.pbm", fb);
        watchy_package_catalog_t catalog;
        catalog_faces(&catalog);
        shell.screen = WATCHY_SHELL_WATCHFACE_SELECTOR;
        shell.face_count = 5u;
        for (size_t i = 0u; i < 4u; ++i) shell.face_indices[i] = (uint8_t)i;
        shell.selection = 0u;
        draw(&c, &shell, &settings, &t, NULL, &catalog, NULL, NULL);
        write_pbm("tests/golden/shell/selector-hairline.pbm", fb);
        shell.selection = 1u;
        draw(&c, &shell, &settings, &t, NULL, &catalog, NULL, NULL);
        write_pbm("tests/golden/shell/selector-active-wpk.pbm", fb);
        return 0;
    }
    CHECK(test_menu() == 0);
    CHECK(test_menu_selections() == 0);
    CHECK(test_selector() == 0);
    CHECK(test_settings() == 0);
    CHECK(test_hairline() == 0);
    CHECK(test_legacy_and_guards() == 0);
    CHECK(test_apps_filter_and_page() == 0);
    if (argc == 3 && strcmp(argv[1], "--compare-menu") == 0) {
        uint8_t fb[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
        watchy_canvas_t c = canvas(fb);
        watchy_settings_t settings = {.time_24h = true};
        watchy_time_t t = now();
        watchy_shell_t shell = {.screen = WATCHY_SHELL_LAUNCHER, .selection = 1u};
        draw(&c, &shell, &settings, &t, NULL, NULL, NULL, NULL);
        return read_pbm(argv[2], fb);
    }
    puts("shell render tests passed");
    return 0;
}
