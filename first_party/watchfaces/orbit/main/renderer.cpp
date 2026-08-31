#include "watchy_first_party/face.h"
#include "watchy_first_party/package.hpp"
#include "watchy/ui_draw.h"

namespace {
using namespace watchy_first_party;

/* The package loader applies relocations only to writable data. These styles
 * contain font pointers, so keep their two relocations out of .rodata. */
#if defined(__ELF__)
static watchy_text_style_t kCompact __attribute__((section(".data"))) =
    {&watchy_font_plex_9_semibold, 0, true, false};
static watchy_text_style_t kClock __attribute__((section(".data"))) =
    {&watchy_font_heros_60_bold, 0, true, false};
#else
static watchy_text_style_t kCompact = {&watchy_font_plex_9_semibold, 0, true, false};
static watchy_text_style_t kClock = {&watchy_font_heros_60_bold, 0, true, false};
#endif

bool orbit_time(void *user_data, watchy_time_t *out) noexcept {
    const watchy_host_caps_v1_t *caps = host(user_data);
    if (out == nullptr) return false;
    *out = watchy_time_t{};
    return caps != nullptr && caps->clock != nullptr && caps->clock->now != nullptr &&
           caps->clock->now(caps->clock->context, out) == WATCHY_STATUS_OK && valid_time(*out);
}

fixed_text orbit_percent(void *user_data) noexcept {
    const watchy_host_caps_v1_t *caps = host(user_data);
    watchy_battery_state_t battery{};
    if (caps == nullptr || caps->battery == nullptr || caps->battery->read == nullptr ||
        caps->battery->read(caps->battery->context, &battery) != WATCHY_STATUS_OK ||
        battery.percent > 100u) return fixed_text("--%");
    fixed_text value;
    value.value[0] = static_cast<char>('0' + battery.percent / 10u);
    value.value[1] = static_cast<char>('0' + battery.percent % 10u);
    value.value[2] = '%';
    value.value[3] = '\0';
    return value;
}

bool phase_black(uint8_t octant, int dx2) noexcept {
    /* Keep control flow explicit: an Xtensa switch jump table would place
     * text relocations in .rodata, which the package loader rejects. */
    const uint8_t phase = static_cast<uint8_t>(octant & 7u);
    if (phase == 0u) return true;                      // new
    if (phase == 1u) return dx2 < 24;                  // waxing crescent
    if (phase == 2u) return dx2 < 0;                   // first quarter
    if (phase == 3u) return dx2 < -24;                 // waxing gibbous
    if (phase == 4u) return false;                     // full
    if (phase == 5u) return dx2 > 24;                  // waning gibbous
    if (phase == 6u) return dx2 >= 0;                  // last quarter
    return dx2 > -24;                                  // waning crescent
}

void draw_phase_disc(watchy_canvas_t *canvas, uint8_t octant) noexcept {
    /* A 62 x 62 fixed-point circle avoids floats and trig. Coordinates are
     * doubled about (44.5, 44.5), so its exact bounds are 14..75. */
    constexpr int outer_squared = 61 * 61;
    constexpr int inner_squared = 57 * 57;
    for (int y = 14; y <= 75; ++y) {
        const int dy2 = y * 2 - 89;
        for (int x = 14; x <= 75; ++x) {
            const int dx2 = x * 2 - 89;
            const int distance_squared = dx2 * dx2 + dy2 * dy2;
            if (distance_squared > outer_squared) continue;
            const bool border = distance_squared > inner_squared;
            watchy_ui_pixel(canvas, static_cast<int16_t>(x), static_cast<int16_t>(y),
                            border || phase_black(octant, dx2));
        }
    }
}

fixed_text orbit_day(const watchy_time_t &time) noexcept {
    fixed_text out;
    const fixed_text weekday = format_weekday(time);
    out.value[0] = weekday.value[0];
    out.value[1] = weekday.value[1];
    out.value[2] = weekday.value[2];
    out.value[3] = ' ';
    out.value[4] = static_cast<char>('0' + time.day / 10u);
    out.value[5] = static_cast<char>('0' + time.day % 10u);
    out.value[6] = '\0';
    return out;
}

fixed_text orbit_tyo(const watchy_time_t &time) noexcept {
    watchy_time_t tokyo{};
    const fixed_text clock = offset_time_checked(time, 540, &tokyo) ? format_hhmm(tokyo, false)
                                                                      : fixed_text("--:--");
    fixed_text out("TYO ");
    uint8_t at = 4u;
    for (uint8_t i = 0u; clock.value[i] != '\0' && at + 1u < sizeof(out.value); ++i) {
        out.value[at++] = clock.value[i];
    }
    out.value[at] = '\0';
    return out;
}

fixed_text orbit_status(void *user_data) noexcept {
    const fixed_text percent = orbit_percent(user_data);
    fixed_text out("--° · ");
    uint8_t at = 7u;
    for (uint8_t i = 0u; percent.value[i] != '\0' && at + 1u < sizeof(out.value); ++i) {
        out.value[at++] = percent.value[i];
    }
    out.value[at] = '\0';
    return out;
}

void draw_glyphs(watchy_canvas_t *canvas) noexcept {
    watchy_ui_rect_outline(canvas, 92, 35, 10, 10, 1u, true);
    watchy_ui_circle(canvas, 111, 39, 4, false, true);
    for (int row = 0; row < 10; ++row) {
        const int width = row <= 4 ? row * 2 + 1 : (9 - row) * 2 + 1;
        watchy_ui_rule(canvas, static_cast<int16_t>(132 - width / 2),
                       static_cast<int16_t>(35 + row), static_cast<int16_t>(width), 1u, true);
    }
}

void draw_orbit(watchy_canvas_t *canvas, void *user_data, const watchy_time_t &time) noexcept {
    const fixed_text clock = format_hhmm(time, false);
    watchy_ui_fill(canvas, false);
    draw_phase_disc(canvas, moon_octant(time));
    watchy_ui_draw_text_font(canvas, 92, 23, "PHASE", &kCompact);
    draw_glyphs(canvas);
    watchy_ui_draw_text_font(canvas, 92, 64, orbit_status(user_data).c_str(), &kCompact);
    watchy_ui_draw_text_centered(canvas, 100, 150, clock.c_str(), &kClock);
    watchy_ui_rule(canvas, 14, 163, 172, 3u, true);
    watchy_ui_draw_text_font(canvas, 14, 186, orbit_day(time).c_str(), &kCompact);
    watchy_ui_draw_text_right(canvas, 186, 186, orbit_tyo(time).c_str(), &kCompact);
}

void set_refresh(void *user_data, uint8_t hour, watchy_refresh_mode_t *mode) noexcept {
    if (mode != nullptr) {
        *mode = full_refresh_for_hour(user_data, hour) ? WATCHY_REFRESH_FULL
                                                       : WATCHY_REFRESH_PARTIAL;
    }
}
}  // namespace

extern "C" watchy_status_t orbit_render(void *user_data, watchy_canvas_t *canvas,
                                         watchy_refresh_mode_t *mode) noexcept {
    if (canvas == nullptr || mode == nullptr) return WATCHY_STATUS_INVALID_ARGUMENT;
    watchy_time_t time{};
    if (!orbit_time(user_data, &time)) {
        watchy_ui_fill(canvas, false);
        set_refresh(user_data, 0u, mode);
        return WATCHY_STATUS_OK;
    }
    draw_orbit(canvas, user_data, time);
    set_refresh(user_data, time.hour, mode);
    return WATCHY_STATUS_OK;
}
