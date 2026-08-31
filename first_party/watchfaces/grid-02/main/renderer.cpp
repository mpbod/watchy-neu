#include "watchy_first_party/grid_render.hpp"

namespace {
using namespace watchy_first_party;

static const char kAgenda[] = "NO EVENT";
static const char kAlarm[] = "--:--";

static void draw_grid02(watchy_canvas_t *canvas, const watchy_time_t &time) noexcept {
    watchy_ui_fill(canvas, false);
    watchy_text_style_t rail = grid_value_style();
    watchy_text_style_t heading = grid_heading_style();
    watchy_text_style_t body = grid_body_style();
    watchy_text_style_t large = grid_rail_clock_style();
    const fixed_text date = format_date(time);
    char date_chars[11]{};
    for (uint8_t i = 0u; i < 10u; ++i) date_chars[i] = date.value[i];
    for (uint8_t i = 0u; i < 10u; ++i) {
        char glyph[2] = {date_chars[i], '\0'};
        watchy_ui_draw_text_font(canvas, 5, static_cast<int16_t>(25 + i * 18), glyph, &rail);
    }
    watchy_ui_rect(canvas, 29, 8, 1, 184, true);
    watchy_ui_draw_text_font(canvas, 43, 29, "AGENDA", &heading);
    watchy_ui_rule(canvas, 43, 36, 145, 1u, true);
    watchy_ui_draw_text_font(canvas, 43, 62, kAgenda, &body);
    watchy_ui_draw_text_font(canvas, 43, 84, kAlarm, &body);
    watchy_ui_draw_text_font(canvas, 43, 106, "NEXT", &heading);
    watchy_ui_draw_text_centered(canvas, 119, 182, format_hhmm(time, false).c_str(), &large);
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
