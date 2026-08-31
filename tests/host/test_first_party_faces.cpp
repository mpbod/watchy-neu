#include <cassert>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#include "watchy_first_party/face.h"
#include "watchy_first_party/grid_render.hpp"
#include "watchy_first_party/package.hpp"

using namespace watchy_first_party;

#ifndef WATCHY_FACE_GOLDEN_DIR
#define WATCHY_FACE_GOLDEN_DIR "tests/golden/faces"
#endif

extern "C" watchy_status_t grid01_render(void *, watchy_canvas_t *, watchy_refresh_mode_t *);
extern "C" watchy_status_t grid02_render(void *, watchy_canvas_t *, watchy_refresh_mode_t *);
extern "C" watchy_status_t grid03_render(void *, watchy_canvas_t *, watchy_refresh_mode_t *);
extern "C" const watchy_package_descriptor_v1_t *grid01_package_entry(void);
extern "C" const watchy_package_descriptor_v1_t *grid02_package_entry(void);
extern "C" const watchy_package_descriptor_v1_t *grid03_package_entry(void);
extern "C" watchy_status_t term01_render(void *, watchy_canvas_t *, watchy_refresh_mode_t *);
extern "C" watchy_status_t term02_render(void *, watchy_canvas_t *, watchy_refresh_mode_t *);
extern "C" watchy_status_t term03_render(void *, watchy_canvas_t *, watchy_refresh_mode_t *);
extern "C" const watchy_package_descriptor_v1_t *term01_package_entry(void);
extern "C" const watchy_package_descriptor_v1_t *term02_package_entry(void);
extern "C" const watchy_package_descriptor_v1_t *term03_package_entry(void);
extern "C" watchy_status_t slab_render(void *, watchy_canvas_t *, watchy_refresh_mode_t *);
extern "C" watchy_status_t orbit_render(void *, watchy_canvas_t *, watchy_refresh_mode_t *);
extern "C" const watchy_package_descriptor_v1_t *slab_package_entry(void);
extern "C" const watchy_package_descriptor_v1_t *orbit_package_entry(void);

static watchy_status_t render(void *, watchy_canvas_t *, watchy_refresh_mode_t *) { return WATCHY_STATUS_OK; }
WATCHY_FIRST_PARTY_FACE_NAMED(test_package_entry, "watchy.test.face", "Test Face", 0x203u, render)

static bool black(const uint8_t *fb, int x, int y) {
    if (x < 0 || y < 0 || x >= 200 || y >= 200) return false;
    return (fb[static_cast<size_t>(y) * 25u + static_cast<size_t>(x) / 8u] &
            static_cast<uint8_t>(0x80u >> (x & 7))) == 0u;
}

static int ink(const uint8_t *fb, int left, int top, int right, int bottom) {
    int count = 0;
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) count += black(fb, x, y) ? 1 : 0;
    }
    return count;
}

static bool text_ink_fits(const watchy_text_style_t &style,
                          const char *text,
                          int pen_x,
                          int baseline,
                          int left,
                          int top,
                          int right,
                          int bottom) {
    for (size_t index = 0u; text != nullptr && text[index] != '\0'; ++index) {
        const watchy_font_glyph_t *glyph =
            watchy_font_find_glyph(style.font, static_cast<uint8_t>(text[index]));
        if (glyph == nullptr) return false;
        if (glyph->width != 0u && glyph->height != 0u) {
            const int glyph_left = pen_x + glyph->bearing_x;
            const int glyph_top = baseline - glyph->bearing_y;
            const int glyph_right = glyph_left + glyph->width - 1;
            const int glyph_bottom = glyph_top + glyph->height - 1;
            if (glyph_left < left || glyph_top < top || glyph_right > right ||
                glyph_bottom > bottom) {
                return false;
            }
        }
        pen_x += glyph->advance;
        if (text[index + 1u] != '\0') pen_x += style.tracking;
    }
    return true;
}

static bool right_text_ink_fits(const watchy_text_style_t &style,
                                const char *text,
                                int right_x,
                                int baseline,
                                int left,
                                int top,
                                int right,
                                int bottom) {
    const watchy_text_metrics_t metrics = watchy_ui_measure_text(&style, text);
    return text_ink_fits(style, text, right_x - metrics.width, baseline,
                         left, top, right, bottom);
}

static int read_pbm(const char *path, const uint8_t *fb) {
    FILE *file = std::fopen(path, "rb");
    char header[32]{};
    if (file == nullptr || std::fgets(header, sizeof(header), file) == nullptr ||
        std::strcmp(header, "P4\n") != 0 || std::fgets(header, sizeof(header), file) == nullptr ||
        std::strcmp(header, "200 200\n") != 0) {
        if (file != nullptr) std::fclose(file);
        return 1;
    }
    uint8_t expected[5000]{};
    const bool read = std::fread(expected, 1u, sizeof(expected), file) == sizeof(expected) &&
                      std::fgetc(file) == EOF;
    std::fclose(file);
    if (!read) return 1;
    for (size_t i = 0; i < sizeof(expected); ++i) {
        if (expected[i] != static_cast<uint8_t>(~fb[i])) return 1;
    }
    return 0;
}

static void write_pbm(const char *path, const uint8_t *fb) {
    FILE *file = std::fopen(path, "wb");
    assert(file != nullptr);
    std::fputs("P4\n200 200\n", file);
    for (size_t i = 0; i < 5000u; ++i) {
        const uint8_t byte = static_cast<uint8_t>(~fb[i]);
        std::fwrite(&byte, 1u, 1u, file);
    }
    std::fclose(file);
}

struct host_fixture {
    watchy_time_t time{2026, 8, 31, 9, 41, 0, 1, 420};
    watchy_battery_state_t battery{3820, 68, false};
    bool bluetooth = false;
};

static watchy_status_t fixture_now(void *context, watchy_time_t *out) {
    if (context == nullptr || out == nullptr) return WATCHY_STATUS_INVALID_ARGUMENT;
    *out = static_cast<host_fixture *>(context)->time;
    return WATCHY_STATUS_OK;
}

static watchy_status_t fixture_battery(void *context, watchy_battery_state_t *out) {
    if (context == nullptr || out == nullptr) return WATCHY_STATUS_INVALID_ARGUMENT;
    *out = static_cast<host_fixture *>(context)->battery;
    return WATCHY_STATUS_OK;
}

static bool fixture_bt(void *context) {
    return context != nullptr && static_cast<host_fixture *>(context)->bluetooth;
}

static watchy_host_caps_v1_t fixture_caps(host_fixture *fixture) {
    static watchy_clock_api_v1_t clock{};
    static watchy_battery_api_v1_t battery{};
    static watchy_bluetooth_api_v1_t bluetooth{};
    clock = {fixture, fixture_now, nullptr};
    battery = {fixture, fixture_battery};
    bluetooth = {fixture, fixture_bt, nullptr, nullptr, nullptr};
    watchy_host_caps_v1_t caps{};
    caps.abi = {1u, 2u};
    caps.size = sizeof(caps);
    caps.clock = &clock;
    caps.battery = &battery;
    caps.bluetooth = &bluetooth;
    return caps;
}

static int test_grid(const watchy_package_descriptor_v1_t *descriptor,
                     watchy_status_t (*renderer)(void *, watchy_canvas_t *, watchy_refresh_mode_t *),
                     const char *golden,
                     uint32_t capabilities,
                     int face) {
    assert(descriptor != nullptr);
    assert(descriptor->size == sizeof(*descriptor));
    assert(descriptor->metadata.abi.major == 1u && descriptor->metadata.abi.minor == 2u);
    assert(descriptor->metadata.version != nullptr && std::strcmp(descriptor->metadata.version, "1.0.0") == 0);
    assert(descriptor->metadata.flags == capabilities);
    host_fixture fixture;
    watchy_host_caps_v1_t caps = fixture_caps(&fixture);
    void *user = nullptr;
    assert(descriptor->callbacks.on_load(&caps, &user) == WATCHY_STATUS_OK);
    assert(descriptor->callbacks.on_start(user) == WATCHY_STATUS_OK);
    uint8_t framebuffer[5000];
    std::memset(framebuffer, 0x00, sizeof(framebuffer));
    watchy_canvas_t canvas{200u, 200u, 25u, 0u, WATCHY_PIXEL_MONO, framebuffer};
    watchy_refresh_mode_t mode = WATCHY_REFRESH_PARTIAL;
    assert(renderer(user, &canvas, &mode) == WATCHY_STATUS_OK);
    assert(mode == WATCHY_REFRESH_FULL);
    assert(ink(framebuffer, 0, 0, 199, 199) > 80);
    if (face == 1) {
        assert(ink(framebuffer, 14, 14, 185, 31) > 5);
        assert(ink(framebuffer, 14, 158, 185, 198) > 50);
    } else if (face == 2) {
        assert(ink(framebuffer, 0, 0, 25, 199) > 50);
        assert(ink(framebuffer, 31, 27, 185, 90) > 20);
    } else {
        assert(ink(framebuffer, 0, 0, 199, 117) > 80);
        assert(ink(framebuffer, 0, 119, 199, 199) > 40);
    }
    if (std::getenv("WATCHY_UPDATE_GOLDENS") != nullptr) {
        write_pbm(golden, framebuffer);
    } else {
        assert(read_pbm(golden, framebuffer) == 0);
    }
    std::memset(framebuffer, 0x00, sizeof(framebuffer));
    assert(renderer(user, &canvas, &mode) == WATCHY_STATUS_OK);
    assert(mode == WATCHY_REFRESH_PARTIAL);
    fixture.time.hour = 10u;
    assert(renderer(user, &canvas, &mode) == WATCHY_STATUS_OK);
    assert(mode == WATCHY_REFRESH_FULL);
    descriptor->callbacks.on_stop(user);
    descriptor->callbacks.on_unload(user);
    return 0;
}

static int filled_segments(const uint8_t *fb, int x, int y, int width, int gap) {
    int filled = 0;
    for (int index = 0; index < 10; ++index) {
        if (ink(fb, x + index * (width + gap) + 1, y + 1,
                x + index * (width + gap) + width - 2, y + 4) == 0) {
            ++filled;
        }
    }
    return filled;
}

static int test_term(const watchy_package_descriptor_v1_t *descriptor,
                     watchy_status_t (*renderer)(void *, watchy_canvas_t *, watchy_refresh_mode_t *),
                     const char *golden,
                     uint32_t capabilities,
                     int face) {
    assert(descriptor != nullptr);
    assert(descriptor->size == sizeof(*descriptor));
    assert(descriptor->metadata.abi.major == 1u && descriptor->metadata.abi.minor == 2u);
    assert(descriptor->metadata.version != nullptr && std::strcmp(descriptor->metadata.version, "1.0.0") == 0);
    assert(descriptor->metadata.flags == capabilities);
    host_fixture fixture;
    watchy_host_caps_v1_t caps = fixture_caps(&fixture);
    /* Term 01's 771 capability does not include Battery. Keeping this table
     * absent catches a renderer that silently treats fixture-only charge data
     * as a granted service. */
    if (face == 1) caps.battery = nullptr;
    void *user = nullptr;
    assert(descriptor->callbacks.on_load(&caps, &user) == WATCHY_STATUS_OK);
    assert(descriptor->callbacks.on_start(user) == WATCHY_STATUS_OK);
    uint8_t framebuffer[5000];
    std::memset(framebuffer, 0x00, sizeof(framebuffer));
    watchy_canvas_t canvas{200u, 200u, 25u, 0u, WATCHY_PIXEL_MONO, framebuffer};
    watchy_refresh_mode_t mode = WATCHY_REFRESH_PARTIAL;
    assert(renderer(user, &canvas, &mode) == WATCHY_STATUS_OK);
    assert(mode == WATCHY_REFRESH_FULL);
    if (face == 1) {
        /* A missing inverse status line, command content, or solid cursor is a layout bug. */
        assert(ink(framebuffer, 12, 12, 187, 25) > 400);
        assert(ink(framebuffer, 12, 34, 187, 150) > 240);
        assert(ink(framebuffer, 27, 174, 33, 186) == 91);
    } else if (face == 2) {
        /* Mutations to the twelve-cell bar geometry or the ruled footer must be visible. */
        assert(ink(framebuffer, 13, 59, 186, 60) > 250);
        assert(ink(framebuffer, 13, 145, 186, 145) > 120);
        for (int row = 0; row < 3; ++row) {
            int cells = 0;
            for (int cell = 0; cell < 12; ++cell) {
                const int x = 92 + cell * 8;
                assert(ink(framebuffer, x, 65 + row * 27, x + 6, 73 + row * 27) > 0);
                ++cells;
            }
            assert(cells == 12);
        }
    } else {
        /* Term 03 is an actual inverted panel, with only seven real battery fills at 68%. */
        assert(ink(framebuffer, 0, 0, 199, 199) > 36000);
        assert(ink(framebuffer, 14, 164, 180, 169) > 250);
        assert(filled_segments(framebuffer, 14, 164, 14, 3) == 7);
        assert(ink(framebuffer, 14, 174, 186, 192) < 3200);
    }
    if (std::getenv("WATCHY_UPDATE_GOLDENS") != nullptr) {
        write_pbm(golden, framebuffer);
        assert(read_pbm(golden, framebuffer) == 0);
    } else {
        assert(read_pbm(golden, framebuffer) == 0);
    }
    std::memset(framebuffer, 0x00, sizeof(framebuffer));
    assert(renderer(user, &canvas, &mode) == WATCHY_STATUS_OK);
    assert(mode == WATCHY_REFRESH_PARTIAL);
    fixture.time.hour = 10u;
    assert(renderer(user, &canvas, &mode) == WATCHY_STATUS_OK);
    assert(mode == WATCHY_REFRESH_FULL);
    descriptor->callbacks.on_stop(user);
    descriptor->callbacks.on_unload(user);
    return 0;
}

static int test_slab_orbit(const watchy_package_descriptor_v1_t *descriptor,
                           watchy_status_t (*renderer)(void *, watchy_canvas_t *, watchy_refresh_mode_t *),
                           const char *golden,
                           uint32_t capabilities,
                           bool orbit) {
    assert(descriptor != nullptr);
    assert(descriptor->size == sizeof(*descriptor));
    assert(descriptor->metadata.abi.major == 1u && descriptor->metadata.abi.minor == 2u);
    assert(descriptor->metadata.version != nullptr && std::strcmp(descriptor->metadata.version, "1.0.0") == 0);
    assert(descriptor->metadata.flags == capabilities);
    host_fixture fixture;
    watchy_host_caps_v1_t caps = fixture_caps(&fixture);
    void *user = nullptr;
    assert(descriptor->callbacks.on_load(&caps, &user) == WATCHY_STATUS_OK);
    assert(descriptor->callbacks.on_start(user) == WATCHY_STATUS_OK);
    uint8_t framebuffer[5000];
    std::memset(framebuffer, 0x00, sizeof(framebuffer));
    watchy_canvas_t canvas{200u, 200u, 25u, 0u, WATCHY_PIXEL_MONO, framebuffer};
    watchy_refresh_mode_t mode = WATCHY_REFRESH_PARTIAL;
    assert(renderer(user, &canvas, &mode) == WATCHY_STATUS_OK);
    assert(mode == WATCHY_REFRESH_FULL);
    if (!orbit) {
        /* A swap, lost inversion, or incorrect 100px split changes these
         * independent top/bottom regions before the PBM comparison. */
        assert(ink(framebuffer, 0, 0, 199, 99) > 1000);
        assert(ink(framebuffer, 0, 100, 199, 199) > 15000);
        assert(ink(framebuffer, 50, 10, 150, 95) > 300);
        assert(ink(framebuffer, 50, 105, 150, 199) > 300);
        assert(ink(framebuffer, 130, 0, 190, 30) > 10);
        assert(ink(framebuffer, 8, 165, 85, 199) > 20);
        assert(ink(framebuffer, 145, 165, 191, 199) > 15);
    } else {
        /* The disc is exactly 62px (x/y 14..75) with a two-pixel outline;
         * the top-right status and 3px rule are independent layout anchors. */
        assert(ink(framebuffer, 14, 14, 75, 75) > 250);
        assert(ink(framebuffer, 42, 14, 47, 16) > 3);
        assert(ink(framebuffer, 14, 42, 16, 47) > 3);
        assert(ink(framebuffer, 89, 14, 185, 72) > 80);
        assert(ink(framebuffer, 14, 163, 185, 165) > 450);
        assert(ink(framebuffer, 14, 173, 185, 194) > 25);
    }
    if (std::getenv("WATCHY_UPDATE_GOLDENS") != nullptr) {
        write_pbm(golden, framebuffer);
        assert(read_pbm(golden, framebuffer) == 0);
    } else {
        assert(read_pbm(golden, framebuffer) == 0);
    }
    std::memset(framebuffer, 0x00, sizeof(framebuffer));
    assert(renderer(user, &canvas, &mode) == WATCHY_STATUS_OK);
    assert(mode == WATCHY_REFRESH_PARTIAL);
    fixture.time.hour = 10u;
    assert(renderer(user, &canvas, &mode) == WATCHY_STATUS_OK);
    assert(mode == WATCHY_REFRESH_FULL);
    descriptor->callbacks.on_stop(user);
    descriptor->callbacks.on_unload(user);
    return 0;
}

static void assert_orbit_phase(watchy_time_t phase_time,
                               int expected_octant,
                               bool left_black,
                               bool right_black,
                               const char *artifact_name) {
    assert(moon_octant(phase_time) == expected_octant);
    host_fixture fixture;
    fixture.time = phase_time;
    watchy_host_caps_v1_t caps = fixture_caps(&fixture);
    void *user = nullptr;
    assert(orbit_package_entry()->callbacks.on_load(&caps, &user) == WATCHY_STATUS_OK);
    assert(orbit_package_entry()->callbacks.on_start(user) == WATCHY_STATUS_OK);
    uint8_t framebuffer[5000]{};
    watchy_canvas_t canvas{200u, 200u, 25u, 0u, WATCHY_PIXEL_MONO, framebuffer};
    watchy_refresh_mode_t mode = WATCHY_REFRESH_PARTIAL;
    assert(orbit_render(user, &canvas, &mode) == WATCHY_STATUS_OK);
    assert(mode == WATCHY_REFRESH_FULL);
    /* Interior samples are deliberately away from the 2px outline and phase
     * terminator: a wrong phase direction or a filled/empty disc fails. */
    assert(black(framebuffer, 30, 45) == left_black);
    assert(black(framebuffer, 60, 45) == right_black);
    assert(ink(framebuffer, 14, 14, 75, 75) > 250);
    const char *artifact_dir = std::getenv("WATCHY_PHASE_ARTIFACT_DIR");
    if (artifact_dir != nullptr && artifact_name != nullptr) {
        char path[192]{};
        const int written = std::snprintf(path, sizeof(path), "%s/%s.pbm", artifact_dir, artifact_name);
        assert(written > 0 && static_cast<size_t>(written) < sizeof(path));
        write_pbm(path, framebuffer);
    }
    orbit_package_entry()->callbacks.on_stop(user);
    orbit_package_entry()->callbacks.on_unload(user);
}

int main() {
    const watchy_text_style_t grid_heading = grid_heading_style();
    const watchy_text_style_t grid_header = grid_header_style();
    const watchy_text_style_t grid_footer = grid_footer_style();
    const watchy_text_style_t grid_body = grid_body_style();
    const watchy_text_style_t grid_value = grid_value_style();
    const watchy_text_style_t grid_rail = grid_rail_style();
    const watchy_text_style_t grid_clock = grid_large_clock_style();
    const watchy_text_style_t grid_rail_clock = grid_rail_clock_style();
    const watchy_text_style_t grid_modular_clock = grid_modular_clock_style();
    assert(grid_heading.font == &watchy_font_plex_11_semibold);
    assert(grid_header.font == &watchy_font_plex_10_semibold);
    assert(grid_footer.font == &watchy_font_plex_8_semibold);
    assert(grid_header.tracking == 1 && grid_footer.tracking == 1);
    assert(grid_body.font == &watchy_font_plex_13_regular);
    assert(grid_value.font == &watchy_font_plex_15_medium);
    assert(grid_rail.font == &watchy_font_plex_9_semibold);
    assert(grid_rail.tracking == 2);
    assert(grid_clock.font == &watchy_font_heros_62_bold);
    assert(grid_clock.tracking == 0);
    assert(grid_rail_clock.font == &watchy_font_heros_62_regular);
    assert(grid_rail_clock.tracking == 0);
    assert(grid_modular_clock.font == &watchy_font_heros_74_regular);
    assert(grid_modular_clock.tracking == 0);
    assert(watchy_ui_measure_text(&grid_clock, "09:41").width == 157);
    assert(text_ink_fits(grid_header, "MON", 14, 23, 14, 14, 70, 31));
    assert(right_text_ink_fits(grid_header, "31 AUG", 186, 23, 120, 14, 185, 31));
    assert(grid_fit_width(grid_clock, "09:41", 170u) == 157u);
    assert(grid_fit_width(grid_rail_clock, "09:41", 150u) == 150u);
    assert(grid_text_raw_width(grid_modular_clock, "09:41") == 185);
    assert(grid_fit_width(grid_modular_clock, "09:41", 190u) == 185u);
    assert(text_ink_fits(grid_body, "NO EVENT", 40, 58,
                         31, 30, 185, 90));
    assert(text_ink_fits(grid_body, "--:--", 40, 76,
                         31, 30, 185, 90));
    assert(text_ink_fits(grid_value, "--", 75, 160,
                         67, 119, 132, 199));
    assert(right_text_ink_fits(grid_value, "BT--", 184, 196,
                               128, 158, 184, 198));
    watchy_time_t value{2026, 8, 31, 9, 41, 0, 1, 420};
    assert(format_hhmm(value, true) == fixed_text("09:41"));
    assert(format_hhmm(value, false) == fixed_text("09:41"));
    assert(format_hhmm(watchy_time_t{2026, 8, 31, 0, 0, 0, 1, 420}, true) == fixed_text("12:00"));
    assert(format_hhmm(watchy_time_t{2026, 8, 31, 12, 0, 0, 1, 420}, true) == fixed_text("12:00"));
    assert(offset_time(value, 540).hour == 11);
    assert(moon_octant(value) < 8u);
    assert(moon_octant(watchy_time_t{2000, 1, 6, 18, 14, 0, 4, 0}) == 0u);
    assert(moon_octant(watchy_time_t{2000, 1, 6, 18, 13, 0, 4, 0}) == 7u);
    assert(moon_octant(watchy_time_t{2000, 1, 7, 0, 0, 0, 5, 0}) == 0u);
    assert(moon_octant(watchy_time_t{2000, 1, 7, 0, 0, 0, 5, 60}) == 0u);
    assert(format_weekday(value) == fixed_text("MON"));
    assert(format_day_month(value) == fixed_text("31 AUG"));
    assert(format_day_month_year(value) == fixed_text("31 AUG 2026"));
    assert(format_weekday(watchy_time_t{2000, 1, 1, 0, 0, 0, 6, 0}) == fixed_text("SAT"));
    assert(format_weekday(watchy_time_t{2000, 2, 29, 0, 0, 0, 2, 0}) == fixed_text("TUE"));
    watchy_time_t rollover = offset_time(value, -720);
    assert(rollover.day == 30 && rollover.month == 8 && rollover.hour == 14);
    assert(!valid_time(watchy_time_t{2025, 2, 29, 0, 0, 0, 0, 0}));
    assert(valid_time(watchy_time_t{2024, 2, 29, 23, 59, 59, 4, 840}));
    assert(!valid_time(watchy_time_t{1900, 2, 29, 0, 0, 0, 0, 0}));
    assert(!valid_time(watchy_time_t{2026, 1, 1, 0, 0, 0, 0, 841}));
    fixed_text unchanged("KEEP");
    assert(!format_hhmm_checked(watchy_time_t{2025, 2, 29, 0, 0, 0, 0, 0}, true, &unchanged));
    assert(unchanged == fixed_text("KEEP"));
    watchy_time_t unchanged_time{2026, 8, 31, 9, 41, 0, 1, 420};
    watchy_time_t invalid{2025, 2, 29, 0, 0, 0, 0, 0};
    assert(!offset_time_checked(invalid, 0, &unchanged_time));
    assert(unchanged_time.day == 31 && unchanged_time.hour == 9);
    assert(offset_time(watchy_time_t{2026, 1, 1, 0, 15, 0, 4, 0}, -720).year == 2025);
    assert(offset_time(watchy_time_t{2025, 12, 31, 23, 45, 0, 3, 0}, 840).year == 2026);
    const watchy_package_descriptor_v1_t *descriptor = test_package_entry();
    assert(descriptor->metadata.abi.major == 1 && descriptor->metadata.abi.minor == 2);
    watchy_host_caps_v1_t caps{}; void *user = nullptr;
    assert(descriptor->callbacks.on_load(&caps, &user) == WATCHY_STATUS_OK && host(user) == &caps);
    assert(descriptor->callbacks.on_start(user) == WATCHY_STATUS_OK);
    assert(descriptor->callbacks.on_render(user, nullptr, nullptr) == WATCHY_STATUS_OK);
    descriptor->callbacks.on_stop(user); descriptor->callbacks.on_unload(user);
    assert(host(user) == nullptr);
    void *second_user = nullptr;
    assert(descriptor->callbacks.on_load(&caps, &second_user) == WATCHY_STATUS_OK);
    descriptor->callbacks.on_unload(second_user);
    const watchy_face_data_t data{};
    assert(data.time.year == 0);
    assert(std::strcmp(grid01_package_entry()->metadata.identifier, "watchy.firstparty.grid01") == 0);
    assert(std::strcmp(grid01_package_entry()->metadata.name, "Grid 01") == 0);
    assert(std::strcmp(grid02_package_entry()->metadata.identifier, "watchy.firstparty.grid02") == 0);
    assert(std::strcmp(grid03_package_entry()->metadata.identifier, "watchy.firstparty.grid03") == 0);
    assert(test_grid(grid01_package_entry(), grid01_render,
                     WATCHY_FACE_GOLDEN_DIR "/grid-01.pbm", 787u, 1) == 0);
    assert(test_grid(grid02_package_entry(), grid02_render,
                     WATCHY_FACE_GOLDEN_DIR "/grid-02.pbm", 515u, 2) == 0);
    assert(test_grid(grid03_package_entry(), grid03_render,
                     WATCHY_FACE_GOLDEN_DIR "/grid-03.pbm", 515u, 3) == 0);
    assert(std::strcmp(term01_package_entry()->metadata.identifier, "watchy.firstparty.term01") == 0);
    assert(std::strcmp(term01_package_entry()->metadata.name, "Term 01") == 0);
    assert(std::strcmp(term02_package_entry()->metadata.identifier, "watchy.firstparty.term02") == 0);
    assert(std::strcmp(term03_package_entry()->metadata.identifier, "watchy.firstparty.term03") == 0);
    assert(test_term(term01_package_entry(), term01_render,
                     WATCHY_FACE_GOLDEN_DIR "/term-01.pbm", 771u, 1) == 0);
    assert(test_term(term02_package_entry(), term02_render,
                     WATCHY_FACE_GOLDEN_DIR "/term-02.pbm", 531u, 2) == 0);
    assert(test_term(term03_package_entry(), term03_render,
                     WATCHY_FACE_GOLDEN_DIR "/term-03.pbm", 531u, 3) == 0);
    assert(std::strcmp(slab_package_entry()->metadata.identifier, "watchy.firstparty.slab") == 0);
    assert(std::strcmp(slab_package_entry()->metadata.name, "Slab") == 0);
    assert(std::strcmp(orbit_package_entry()->metadata.identifier, "watchy.firstparty.orbit") == 0);
    assert(std::strcmp(orbit_package_entry()->metadata.name, "Orbit") == 0);
    assert(test_slab_orbit(slab_package_entry(), slab_render,
                           WATCHY_FACE_GOLDEN_DIR "/slab.pbm", 531u, false) == 0);
    assert(test_slab_orbit(orbit_package_entry(), orbit_render,
                           WATCHY_FACE_GOLDEN_DIR "/orbit.pbm", 531u, true) == 0);
    assert_orbit_phase(watchy_time_t{2000, 1, 6, 18, 14, 0, 4, 0}, 0, true, true, "orbit-new");
    assert_orbit_phase(watchy_time_t{2000, 1, 14, 3, 26, 0, 5, 0}, 2, true, false, "orbit-first-quarter");
    assert_orbit_phase(watchy_time_t{2000, 1, 21, 12, 37, 0, 5, 0}, 4, false, false, "orbit-full");
    assert_orbit_phase(watchy_time_t{2000, 1, 28, 21, 48, 0, 5, 0}, 6, false, true, "orbit-last-quarter");
    return 0;
}
