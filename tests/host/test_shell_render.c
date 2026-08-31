#include "watchy/display.h"
#include "watchy/shell_render.h"
#include "watchy/ui_draw.h"

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

static int same_region(const uint8_t *first,
                       const uint8_t *second,
                       int left,
                       int top,
                       int right,
                       int bottom) {
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            if (black(first, x, y) != black(second, x, y)) return 0;
        }
    }
    return 1;
}

static int text_ink_fits(const watchy_font_t *font,
                         int8_t tracking,
                         const char *text,
                         int pen_x,
                         int baseline,
                         int left,
                         int top,
                         int right,
                         int bottom) {
    while (text != NULL && *text != '\0') {
        const watchy_font_glyph_t *glyph = watchy_font_find_glyph(font, (uint8_t)*text);
        if (glyph == NULL) return 0;
        if (glyph->width != 0u && glyph->height != 0u) {
            const int glyph_left = pen_x + glyph->bearing_x;
            const int glyph_top = baseline - glyph->bearing_y;
            const int glyph_right = glyph_left + glyph->width - 1;
            const int glyph_bottom = glyph_top + glyph->height - 1;
            if (glyph_left < left || glyph_top < top || glyph_right > right ||
                glyph_bottom > bottom) {
                return 0;
            }
        }
        pen_x += glyph->advance;
        if (text[1] != '\0') pen_x += tracking;
        ++text;
    }
    return 1;
}

static int centered_text_ink_fits(const watchy_text_style_t *style,
                                  const char *text,
                                  int center_x,
                                  int baseline,
                                  int left,
                                  int top,
                                  int right,
                                  int bottom) {
    const watchy_text_metrics_t metrics = watchy_ui_measure_text(style, text);
    return text_ink_fits(style->font, style->tracking, text,
                         center_x - metrics.width / 2, baseline,
                         left, top, right, bottom);
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
    for (size_t i = 0u; i < sizeof(expected); ++i) {
        CHECK(expected[i] == (uint8_t)~fb[i]);
    }
    return 0;
}

static void write_pbm(const char *path, const uint8_t *fb) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) return;
    (void)fputs("P4\n200 200\n", f);
    for (size_t i = 0u; i < WATCHY_DISPLAY_FRAMEBUFFER_SIZE; ++i) {
        const uint8_t standard_bits = (uint8_t)~fb[i];
        (void)fwrite(&standard_bits, 1u, 1u, f);
    }
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
    CHECK(black(fb, 2, 79) && black(fb, 2, 134) && !black(fb, 2, 135));
    CHECK(black(fb, 188, 19) && black(fb, 198, 19));
    CHECK(black(fb, 188, 180) && black(fb, 198, 180));
    CHECK(black(fb, 193, 8) && black(fb, 192, 9) && black(fb, 194, 9));
    CHECK(black(fb, 190, 11) && black(fb, 196, 11));
    CHECK(black(fb, 190, 189) && black(fb, 196, 189));
    /* The face icon has a vertical stem only; its dial center stays clear. */
    CHECK(black(fb, 21, 42));
    CHECK(black(fb, 12, 52));
    CHECK(black(fb, 31, 52));
    CHECK(black(fb, 21, 61) && black(fb, 20, 45) && black(fb, 21, 51));
    CHECK(!black(fb, 19, 48) && !black(fb, 23, 48) && !black(fb, 17, 53));
    /* Settings is a 14px square at 45 degrees: a 20px outline bbox whose center is white. */
    CHECK(black(fb, 21, 152) && black(fb, 12, 162) && black(fb, 31, 162));
    CHECK(black(fb, 21, 171) && !black(fb, 21, 161) && !black(fb, 22, 162));
    CHECK(!black(fb, 11, 162) && !black(fb, 32, 162));
    CHECK(region_ink(fb, 44, 25, 180, 79) > 20);
    CHECK(region_ink(fb, 44, 80, 180, 134) > 20);
    CHECK(region_ink(fb, 44, 135, 180, 189) > 20);
    CHECK(read_pbm(WATCHY_GOLDEN_DIR "/menu.pbm", fb) == 0);
    write_pbm("/tmp/watchy-shell-polarity.pbm", fb);
    FILE *pbm = fopen("/tmp/watchy-shell-polarity.pbm", "rb");
    CHECK(pbm != NULL);
    CHECK(fseek(pbm, 11L, SEEK_SET) == 0);
    int first_byte = fgetc(pbm);
    CHECK((first_byte & 0x80) != 0); /* P4 bit 1 is black at (0,0). */
    CHECK(fseek(pbm, 11L + 24L * 25L + 23L, SEEK_SET) == 0);
    int rail_byte = fgetc(pbm);
    CHECK((rail_byte & 0x08) == 0); /* P4 bit 0 is white at (188,24). */
    fclose(pbm);
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
        const unsigned thumb_top = 20u + (selection * 109u + 1u) / 2u;
        CHECK(black(frames[selection], 194, (int)thumb_top));
        CHECK(black(frames[selection], 190, (int)thumb_top) &&
              black(frames[selection], 197, (int)thumb_top));
        CHECK(!black(frames[selection], 189, (int)thumb_top) &&
              !black(frames[selection], 198, (int)thumb_top));
        CHECK(black(frames[selection], 194, (int)(thumb_top + 50u)));
        if (thumb_top + 51u < 180u) {
            CHECK(!black(frames[selection], 194, (int)(thumb_top + 51u)));
        }
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
        const unsigned selector_thumb_top = 20u + (selection * 109u + 2u) / 4u;
        CHECK(black(fb, 194, (int)selector_thumb_top));
        CHECK(black(fb, 194, (int)(selector_thumb_top + 50u)));
        CHECK(black(fb, 190, (int)selector_thumb_top) &&
              black(fb, 197, (int)selector_thumb_top));
        CHECK(!black(fb, 189, (int)selector_thumb_top) &&
              !black(fb, 198, (int)selector_thumb_top));
    }
    (void)status_contract;
    CHECK(memcmp(status_frames[1], status_frames[2], sizeof(status_frames[1])) != 0);
    CHECK(memcmp(status_frames[2], status_frames[3], sizeof(status_frames[2])) != 0);
    CHECK(memcmp(status_frames[3], status_frames[4], sizeof(status_frames[3])) != 0);
    for (unsigned position = 1u; position < 5u; ++position) {
        s.selection = position < 3u ? 0u : position == 3u ? 4u : 3u;
        draw(&c, &s, &settings, &t, NULL, &catalog, NULL, NULL);
        const unsigned page_start = (position / 3u) * 3u;
        const unsigned slot = position - page_start;
        uint8_t expected[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
        watchy_canvas_t expected_canvas = canvas(expected);
        watchy_ui_draw_text_font(&expected_canvas, 44,
                                 (int16_t)(25 + slot * 55u + 49u),
                                 status_contract[position],
                                 &(watchy_text_style_t){&watchy_font_plex_10_semibold,
                                                        1, true, false});
        CHECK(same_region(fb, expected, 44, 25 + (int)slot * 55 + 34,
                          186, 25 + (int)slot * 55 + 52));
    }
    s.selection = 0u;
    draw(&c, &s, &settings, &t, NULL, &catalog, NULL, NULL);
    CHECK(read_pbm(WATCHY_GOLDEN_DIR "/selector-hairline.pbm", fb) == 0);
    s.selection = 1u;
    draw(&c, &s, &settings, &t, NULL, &catalog, NULL, NULL);
    CHECK(read_pbm(WATCHY_GOLDEN_DIR "/selector-active-wpk.pbm", fb) == 0);
    return 0;
}

static int test_settings(void) {
    static const char *const expected_labels[] = {
        "Clock", "Motion Wake", "Display Motion", "Set Time", "NTP Sync",
        "Wi-Fi", "Portal", "Refresh", "Diagnostics", "About",
    };
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
        const unsigned settings_thumb_top = 20u + (selection * 109u + 4u) / 9u;
        CHECK(black(frames[selection], 194, (int)settings_thumb_top));
        CHECK(black(frames[selection], 194, (int)(settings_thumb_top + 50u)));
        CHECK(black(frames[selection], 190, (int)settings_thumb_top) &&
              black(frames[selection], 197, (int)settings_thumb_top));
        CHECK(!black(frames[selection], 189, (int)settings_thumb_top) &&
              !black(frames[selection], 198, (int)settings_thumb_top));
        CHECK(strcmp(watchy_shell_render_settings_label(selection), expected_labels[selection]) == 0);
        uint8_t expected[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
        watchy_canvas_t expected_canvas = canvas(expected);
        const unsigned slot = selection % WATCHY_SHELL_VISIBLE_ROWS;
        const int top = 25 + (int)slot * 55;
        watchy_ui_rect(&expected_canvas, 0, (int16_t)top, 187, 55, true);
        watchy_ui_draw_text_font(&expected_canvas, 44, (int16_t)(top + 32),
                                 expected_labels[selection],
                                 &(watchy_text_style_t){&watchy_font_heros_20_bold, 0,
                                                        false, false});
        CHECK(same_region(frames[selection], expected, 44, top, 186, top + 33));
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

static int test_larger_typography_fits_shell_regions(void) {
    const watchy_text_style_t header = {&watchy_font_plex_10_semibold, 1, true, false};
    const watchy_text_style_t primary = {&watchy_font_heros_20_bold, 0, true, false};
    const watchy_text_style_t secondary = {&watchy_font_plex_10_semibold, 1, true, false};
    const watchy_text_style_t clock = {&watchy_font_heros_46_regular, 0, true, false};
    const watchy_text_style_t date = {&watchy_font_plex_10_semibold, 1, true, false};
    const watchy_text_style_t compact = {&watchy_font_heros_15_bold, 0, true, false};

    CHECK(header.font->px == 10u && primary.font->px == 20u &&
          clock.font->px == 46u && compact.font->px == 15u);
    CHECK(text_ink_fits(header.font, header.tracking, "DIAGNOSTICS", 7, 18,
                        0, 0, 186, 24));
    CHECK(text_ink_fits(primary.font, primary.tracking, "Display Motion", 44, 57,
                        0, 25, 186, 79));
    CHECK(text_ink_fits(secondary.font, secondary.tracking, "QUARANTINED", 44, 74,
                        0, 25, 186, 79));
    CHECK(centered_text_ink_fits(&clock, "12:34", 100, 94,
                                 0, 25, 199, 150));
    CHECK(centered_text_ink_fits(&date, "31 AUG", 100, 176,
                                 0, 151, 199, 196));
    CHECK(text_ink_fits(compact.font, compact.tracking, "REMOVE PACKAGE", 9, 157,
                        0, 139, 199, 166));
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

static int test_dynamic_text_is_fitted_before_visual_boundaries(void) {
    uint8_t fb[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    watchy_canvas_t c = canvas(fb);
    watchy_settings_t settings = {.time_24h = true};
    watchy_time_t t = now();
    watchy_package_catalog_t catalog;
    watchy_shell_t shell = {
        .screen = WATCHY_SHELL_WATCHFACE_SELECTOR,
        .selection = 0u,
        .face_count = 2u,
    };
    catalog_faces(&catalog);
    snprintf(catalog.packages[0].name, sizeof(catalog.packages[0].name),
             "%s", "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    shell.face_indices[0] = 0u;
    draw(&c, &shell, &settings, &t, NULL, &catalog, NULL, NULL);
    uint8_t expected[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    watchy_canvas_t expected_canvas = canvas(expected);
    watchy_ui_draw_text_font(&expected_canvas, 44, 112, "ABCDEFGHIJK",
                             &(watchy_text_style_t){&watchy_font_heros_20_bold,
                                                    0, true, false});
    CHECK(same_region(fb, expected, 44, 81, 186, 120));

    c = canvas(fb);
    shell = (watchy_shell_t){.screen = WATCHY_SHELL_MANUAL_TIME};
    draw(&c, &shell, &settings, &t, NULL, NULL, NULL,
         "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJK");
    CHECK(region_ink(fb, 192, 50, 199, 78) == 0);
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
    CHECK(test_larger_typography_fits_shell_regions() == 0);
    CHECK(test_legacy_and_guards() == 0);
    CHECK(test_apps_filter_and_page() == 0);
    CHECK(test_dynamic_text_is_fitted_before_visual_boundaries() == 0);
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
