#include "watchy_first_party/term_render.hpp"

namespace {
using namespace watchy_first_party;

void draw_progress(watchy_canvas_t *canvas, int16_t y, const watchy_time_t &time) noexcept {
    const int filled = (static_cast<int>(time.hour) * 60 + time.minute) * 12 / 1440;
    for (int cell = 0; cell < 12; ++cell) {
        const int16_t x = static_cast<int16_t>(92 + cell * 8);
        watchy_ui_rect_outline(canvas, x, y, 7, 9, 1u, true);
        if (cell < filled) watchy_ui_rect(canvas, static_cast<int16_t>(x + 1),
                                          static_cast<int16_t>(y + 1), 5, 7, true);
    }
}

void draw_city(watchy_canvas_t *canvas, int16_t baseline, const char *city,
               const watchy_time_t &time) noexcept {
    const watchy_text_style_t city_style = term_9();
    const watchy_text_style_t clock_style = term_13_medium();
    watchy_ui_draw_text_font(canvas, 13, baseline, city, &city_style);
    watchy_ui_draw_text_font(canvas, 39, baseline, format_hhmm(time, false).c_str(), &clock_style);
    draw_progress(canvas, static_cast<int16_t>(baseline - 9), time);
}

void draw_term02(watchy_canvas_t *canvas, void *user_data, const watchy_time_t &time) noexcept {
    watchy_ui_fill(canvas, false);
    const watchy_text_style_t home = term_32();
    const watchy_text_style_t label = term_9(true, 1);
    watchy_ui_draw_text_font(canvas, 13, 43, format_hhmm(time, false).c_str(), &home);
    watchy_ui_draw_text_right(canvas, 187, 52, "HOME", &label);
    watchy_ui_rule(canvas, 13, 59, 174, 2u, true);
    watchy_time_t lon{};
    watchy_time_t nyc{};
    watchy_time_t tokyo{};
    const bool lon_ok = offset_time_checked(time, 0, &lon);
    const bool nyc_ok = offset_time_checked(time, -300, &nyc);
    const bool tokyo_ok = offset_time_checked(time, 540, &tokyo);
    draw_city(canvas, 74, "LON", lon_ok ? lon : time);
    draw_city(canvas, 101, "NYC", nyc_ok ? nyc : time);
    draw_city(canvas, 128, "TYO", tokyo_ok ? tokyo : time);
    watchy_ui_rule(canvas, 13, 145, 174, 1u, true);
    watchy_ui_draw_text_font(canvas, 13, 160, term_date(time).c_str(), &label);
    watchy_ui_draw_text_right(canvas, 187, 160, term_percent(user_data).c_str(), &label);
}
}

extern "C" watchy_status_t term02_render(void *user_data, watchy_canvas_t *canvas,
                                            watchy_refresh_mode_t *mode) noexcept {
    if (canvas == nullptr || mode == nullptr) return WATCHY_STATUS_INVALID_ARGUMENT;
    watchy_time_t time{};
    if (!watchy_first_party::term_time(user_data, &time)) {
        watchy_ui_fill(canvas, false);
        watchy_first_party::term_invalid_mode(user_data, mode);
        return WATCHY_STATUS_OK;
    }
    draw_term02(canvas, user_data, time);
    watchy_first_party::term_time_mode(user_data, time, mode);
    return WATCHY_STATUS_OK;
}
