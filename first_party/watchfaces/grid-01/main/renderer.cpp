#include "watchy_first_party/grid_render.hpp"

namespace {
using namespace watchy_first_party;

static const char kBluetoothPlaceholder[] = "BT·--";
static const char kTemperaturePlaceholder[] = "--°";

static void draw_grid01(watchy_canvas_t *canvas, void *user_data,
                        const watchy_time_t &time) noexcept {
    watchy_ui_fill(canvas, false);
    watchy_text_style_t header = grid_heading_style();
    watchy_text_style_t value = grid_value_style();
    watchy_text_style_t large = grid_large_clock_style();
    const fixed_text weekday = format_weekday(time);
    const fixed_text date = format_day_month(time);
    const fixed_text clock = format_hhmm(time, false);
    watchy_ui_draw_text_font(canvas, 14, 23, weekday.c_str(), &header);
    watchy_ui_draw_text_right(canvas, 186, 23, date.c_str(), &header);
    watchy_ui_rule(canvas, 14, 32, 172, 1u, true);
    watchy_ui_draw_text_centered(canvas, 100, 111, clock.c_str(), &large);
    grid_footer_rule(canvas);
    watchy_ui_draw_text_font(canvas, 14, 174, "TEMP", &header);
    watchy_ui_draw_text_font(canvas, 76, 174, "BATT", &header);
    watchy_ui_draw_text_font(canvas, 140, 174, "LINK", &header);
    watchy_ui_draw_text_font(canvas, 14, 196, kTemperaturePlaceholder, &value);
    grid_draw_battery(canvas, user_data, 76, 196);
    watchy_ui_draw_text_right(canvas, 184, 196,
                              grid_bluetooth(user_data) ? "BT·ON" : kBluetoothPlaceholder, &value);
}
}

extern "C" watchy_status_t grid01_render(void *user_data, watchy_canvas_t *canvas,
                                          watchy_refresh_mode_t *mode) noexcept {
    if (canvas == nullptr || mode == nullptr) return WATCHY_STATUS_INVALID_ARGUMENT;
    watchy_time_t time{};
    if (!watchy_first_party::grid_time(user_data, &time)) {
        watchy_ui_fill(canvas, false);
        watchy_text_style_t style = watchy_first_party::grid_body_style();
        watchy_ui_draw_text_font(canvas, 8, 15, "--/--/----", &style);
        watchy_ui_draw_text_right(canvas, 192, 15, "BT·--", &style);
        watchy_first_party::grid_invalid_mode(user_data, mode);
        return WATCHY_STATUS_OK;
    }
    draw_grid01(canvas, user_data, time);
    watchy_first_party::grid_time_mode(user_data, time, mode);
    return WATCHY_STATUS_OK;
}
