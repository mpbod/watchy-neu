#include "watchy/shell_render.h"
#include "watchy/display.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static bool black(const uint8_t *fb, int x, int y) {
    return (fb[(size_t)y * WATCHY_DISPLAY_STRIDE + (size_t)x / 8u] & (uint8_t)(0x80u >> (x & 7))) == 0;
}

static watchy_canvas_t canvas(uint8_t *fb) {
    watchy_canvas_t c = {WATCHY_DISPLAY_WIDTH, WATCHY_DISPLAY_HEIGHT,
                         WATCHY_DISPLAY_STRIDE, 0, WATCHY_PIXEL_MONO, fb};
    memset(fb, 0xff, WATCHY_DISPLAY_FRAMEBUFFER_SIZE);
    return c;
}

static int test_menu_geometry(void) {
    uint8_t fb[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    watchy_canvas_t c = canvas(fb);
    watchy_shell_t shell = {0};
    watchy_settings_t settings = {0};
    watchy_time_t now = {2026, 8, 31, 12, 34, 0, 1, 0};
    shell.screen = WATCHY_SHELL_LAUNCHER;
    shell.selection = 1;
    settings.time_24h = true;
    watchy_shell_render(&c, &shell, &settings, &now, NULL, NULL, NULL, NULL);
    CHECK(black(fb, 0, 0) && black(fb, 199, 24));
    CHECK(!black(fb, 0, 25) && !black(fb, 186, 79));
    CHECK(!black(fb, 187, 25) && !black(fb, 199, 189));
    CHECK(black(fb, 10, 80) && black(fb, 180, 134));
    CHECK(!black(fb, 10, 79) && !black(fb, 10, 135));
    return 0;
}

static int test_hairline_battery_rule(void) {
    uint8_t fb[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    watchy_canvas_t c = canvas(fb);
    watchy_shell_t shell = {0};
    watchy_settings_t settings = {0};
    watchy_time_t now = {2026, 8, 31, 12, 34, 0, 1, 0};
    watchy_battery_state_t battery = {3800, 68, false};
    shell.screen = WATCHY_SHELL_WATCHFACE;
    settings.time_24h = true;
    watchy_shell_render(&c, &shell, &settings, &now, &battery, NULL, NULL, NULL);
    CHECK(black(fb, 0, 197) && black(fb, 135, 199));
    CHECK(!black(fb, 136, 197) && !black(fb, 199, 199));
    {
        bool found = false;
        for (int y = 175; y <= 185; ++y) for (int x = 20; x < 180; ++x) found |= black(fb, x, y);
        CHECK(found);
    }
    return 0;
}

static void dump_pbm(const char *path, const uint8_t *fb) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P4\n200 200\n");
    fwrite(fb, 1, WATCHY_DISPLAY_FRAMEBUFFER_SIZE, f);
    fclose(f);
}

static int compare_pbm(const char *path, const uint8_t *fb) {
    FILE *f = fopen(path, "rb");
    char header[16];
    uint8_t expected[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
    CHECK(f != NULL);
    CHECK(fgets(header, sizeof(header), f) != NULL && strcmp(header, "P4\n") == 0);
    CHECK(fgets(header, sizeof(header), f) != NULL && strcmp(header, "200 200\n") == 0);
    CHECK(fread(expected, 1, sizeof(expected), f) == sizeof(expected));
    fclose(f);
    CHECK(memcmp(expected, fb, sizeof(expected)) == 0);
    return 0;
}

int main(int argc, char **argv) {
    CHECK(test_menu_geometry() == 0);
    CHECK(test_hairline_battery_rule() == 0);
    if (argc > 1) {
        uint8_t fb[WATCHY_DISPLAY_FRAMEBUFFER_SIZE];
        watchy_canvas_t c = canvas(fb);
        watchy_shell_t shell = {0};
        watchy_settings_t settings = {0};
        watchy_time_t now = {2026, 8, 31, 12, 34, 0, 1, 0};
        watchy_battery_state_t battery = {3800, 68, false};
        watchy_package_catalog_t catalog = {0};
        shell.screen = WATCHY_SHELL_LAUNCHER;
        settings.time_24h = true;
        if (strcmp(argv[1], "selector") == 0) shell.screen = WATCHY_SHELL_WATCHFACE_SELECTOR;
        if (strcmp(argv[1], "selector-active") == 0) {
            shell.screen = WATCHY_SHELL_WATCHFACE_SELECTOR;
            shell.face_count = 2;
            shell.selection = 1;
            shell.face_indices[0] = 0;
            catalog.count = 1;
            strcpy(catalog.packages[0].name, "Grid 01");
            strcpy(catalog.packages[0].version, "1.0.0");
            catalog.packages[0].type = WATCHY_PACKAGE_TYPE_WATCHFACE;
            catalog.packages[0].active = true;
        }
        if (strcmp(argv[1], "hairline") == 0) shell.screen = WATCHY_SHELL_WATCHFACE;
        watchy_shell_render(&c, &shell, &settings, &now, &battery,
                            catalog.count ? &catalog : NULL, NULL, NULL);
        dump_pbm(argv[2], fb);
        if (argc > 3) CHECK(compare_pbm(argv[3], fb) == 0);
    }
    return 0;
}
