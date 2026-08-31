#include "watchy_first_party/face.h"
#include "watchy_first_party/package.hpp"
#include "watchy/ui_draw.h"

namespace {
using namespace watchy_first_party;

/* Font pointers require dynamic relocation in a WPK, so the fixed styles are
 * deliberately writable package data rather than read-only literal pools. */
#if defined(__ELF__)
static watchy_text_style_t kLarge __attribute__((section(".data"))) =
    {&watchy_font_heros_82_bold, -6, true, false};
static watchy_text_style_t kUpper __attribute__((section(".data"))) =
    {&watchy_font_plex_9_semibold, 1, true, false};
static watchy_text_style_t kLower __attribute__((section(".data"))) =
    {&watchy_font_plex_9_semibold, 1, false, false};
#else
static watchy_text_style_t kLarge = {&watchy_font_heros_82_bold, -6, true, false};
static watchy_text_style_t kUpper = {&watchy_font_plex_9_semibold, 1, true, false};
static watchy_text_style_t kLower = {&watchy_font_plex_9_semibold, 1, false, false};
#endif

bool slab_time(void *user_data, watchy_time_t *out) noexcept {
    const watchy_host_caps_v1_t *caps = host(user_data);
    if (out == nullptr) return false;
    *out = watchy_time_t{};
    return caps != nullptr && caps->clock != nullptr && caps->clock->now != nullptr &&
           caps->clock->now(caps->clock->context, out) == WATCHY_STATUS_OK && valid_time(*out);
}

fixed_text slab_battery(void *user_data) noexcept {
    const watchy_host_caps_v1_t *caps = host(user_data);
    watchy_battery_state_t battery{};
    if (caps == nullptr || caps->battery == nullptr || caps->battery->read == nullptr ||
        caps->battery->read(caps->battery->context, &battery) != WATCHY_STATUS_OK ||
        battery.percent > 100u) {
        return fixed_text("--%");
    }
    return format_percent(battery.percent);
}

void draw_slab(watchy_canvas_t *canvas, void *user_data, const watchy_time_t &time) noexcept {
    const char hour[3] = {static_cast<char>('0' + time.hour / 10u),
                          static_cast<char>('0' + time.hour % 10u), '\0'};
    const char minute[3] = {static_cast<char>('0' + time.minute / 10u),
                            static_cast<char>('0' + time.minute % 10u), '\0'};

    watchy_ui_fill(canvas, false);
    /* The font's natural ascent/descent remains inside the white 100px slab;
     * no divider crop is used to manufacture the large-number treatment. */
    watchy_ui_draw_text_centered(canvas, 100, 82, hour, &kLarge);
    watchy_ui_rect(canvas, 0, 100, 200, 100, true);
    /* Mirrored lower-slab baseline preserves the same natural type boundary
     * without cutting minutes at the panel's lower edge. */
    watchy_text_style_t minute_style = kLarge;
    minute_style.black = false;
    watchy_ui_draw_text_centered(canvas, 100, 182, minute, &minute_style);

    const fixed_text weekday = format_weekday(time);
    const fixed_text date = format_day_month(time);
    const fixed_text battery = slab_battery(user_data);
    watchy_ui_draw_text_right(canvas, 191, 18, weekday.c_str(), &kUpper);
    watchy_ui_draw_text_font(canvas, 9, 191, date.c_str(), &kLower);
    watchy_ui_draw_text_right(canvas, 191, 191, battery.c_str(), &kLower);
}

void set_refresh(void *user_data, uint8_t hour, watchy_refresh_mode_t *mode) noexcept {
    (void)hour;
    set_routine_refresh(user_data, mode);
}
}  // namespace

extern "C" watchy_status_t slab_render(void *user_data, watchy_canvas_t *canvas,
                                        watchy_refresh_mode_t *mode) noexcept {
    if (canvas == nullptr || mode == nullptr) return WATCHY_STATUS_INVALID_ARGUMENT;
    watchy_time_t time{};
    if (!slab_time(user_data, &time)) {
        watchy_ui_fill(canvas, false);
        watchy_ui_rect(canvas, 0, 100, 200, 100, true);
        set_refresh(user_data, 0u, mode);
        return WATCHY_STATUS_OK;
    }
    draw_slab(canvas, user_data, time);
    set_refresh(user_data, time.hour, mode);
    return WATCHY_STATUS_OK;
}
