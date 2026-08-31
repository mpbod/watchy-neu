#include "watchy_first_party/grid_render.hpp"

namespace {
using namespace watchy_first_party;

static const char kTemperature[] = "--°";
static const char kNoData[] = "NO DATA";

static void draw_grid03(watchy_canvas_t *canvas, void *user_data,
                        const watchy_time_t &time) noexcept {
    watchy_ui_fill(canvas, false);
    watchy_text_style_t small = grid_small_style();
    watchy_text_style_t value = grid_value_style();
    watchy_text_style_t day_style = grid_day_style();
    watchy_text_style_t large = grid_modular_clock_style();
    const fixed_text local = format_hhmm(time, false);
    watchy_time_t tokyo{};
    const bool tokyo_valid = offset_time_checked(time, 540, &tokyo);
    const fixed_text tokyo_clock = tokyo_valid ? format_hhmm(tokyo, false) : fixed_text("--:--");
    grid_draw_text_fit_centered(canvas, 100, 94, local.c_str(), large, 190u);
    watchy_ui_rule(canvas, 0, 118, 200, 1u, true);
    watchy_ui_rect(canvas, 66, 119, 1, 81, true);
    watchy_ui_rect(canvas, 133, 119, 1, 81, true);
    const fixed_text date = format_day_month(time);
    char day[3] = {date.value[0], date.value[1], '\0'};
    char month[4] = {date.value[3], date.value[4], date.value[5], '\0'};
    watchy_ui_draw_text_font(canvas, 9, 138, format_weekday(time).c_str(), &small);
    watchy_ui_draw_text_font(canvas, 9, 164, day, &day_style);
    watchy_ui_draw_text_font(canvas, 9, 184, month, &small);
    watchy_ui_draw_text_font(canvas, 75, 138, "OUT", &small);
    watchy_ui_draw_text_font(canvas, 75, 162, kTemperature, &value);
    watchy_ui_draw_text_font(canvas, 75, 183, kNoData, &small);
    watchy_ui_draw_text_font(canvas, 142, 138, "TYO", &small);
    watchy_ui_draw_text_font(canvas, 142, 162, tokyo_clock.c_str(), &value);
    (void)user_data;
}
}

extern "C" watchy_status_t grid03_render(void *user_data, watchy_canvas_t *canvas,
                                          watchy_refresh_mode_t *mode) noexcept {
    if (canvas == nullptr || mode == nullptr) return WATCHY_STATUS_INVALID_ARGUMENT;
    watchy_time_t time{};
    if (!watchy_first_party::grid_time(user_data, &time)) {
        watchy_ui_fill(canvas, false);
        watchy_first_party::grid_invalid_mode(user_data, mode);
        return WATCHY_STATUS_OK;
    }
    draw_grid03(canvas, user_data, time);
    watchy_first_party::grid_time_mode(user_data, time, mode);
    return WATCHY_STATUS_OK;
}
