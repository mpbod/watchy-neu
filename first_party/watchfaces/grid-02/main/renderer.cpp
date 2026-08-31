#include "watchy_first_party/grid_render.hpp"

namespace {
using namespace watchy_first_party;

static const char kAgenda[] = "NO EVENT";
static const char kAlarm[] = "--:--";

static void draw_grid02(watchy_canvas_t *canvas, const watchy_time_t &time) noexcept {
    watchy_ui_fill(canvas, false);
    watchy_text_style_t rail = grid_small_style();
    watchy_text_style_t body = grid_body_style();
    watchy_text_style_t large = grid_rail_clock_style();
    const fixed_text date = format_day_month_year(time);
    char date_chars[16]{};
    for (uint8_t i = 0u; i < 15u; ++i) date_chars[i] = date.value[i];
    for (uint8_t i = 0u; i < 15u; ++i) {
        char glyph[2] = {date_chars[i], '\0'};
        watchy_ui_draw_text_font(canvas, 9, static_cast<int16_t>(12 + i * 12), glyph, &rail);
    }
    watchy_ui_rect(canvas, 25, 0, 1, 200, true);
    watchy_ui_draw_text_font(canvas, 40, 28, "NEXT", &body);
    watchy_ui_draw_text_font(canvas, 40, 54, kAgenda, &body);
    watchy_ui_draw_text_font(canvas, 40, 76, kAlarm, &body);
    watchy_ui_draw_text_centered(canvas, 111, 184, format_hhmm(time, false).c_str(), &large);
}
}

extern "C" watchy_status_t grid02_render(void *user_data, watchy_canvas_t *canvas,
                                          watchy_refresh_mode_t *mode) noexcept {
    if (canvas == nullptr || mode == nullptr) return WATCHY_STATUS_INVALID_ARGUMENT;
    watchy_time_t time{};
    if (!watchy_first_party::grid_time(user_data, &time)) {
        watchy_ui_fill(canvas, false);
        watchy_first_party::grid_invalid_mode(user_data, mode);
        return WATCHY_STATUS_OK;
    }
    draw_grid02(canvas, time);
    watchy_first_party::grid_time_mode(user_data, time, mode);
    return WATCHY_STATUS_OK;
}
