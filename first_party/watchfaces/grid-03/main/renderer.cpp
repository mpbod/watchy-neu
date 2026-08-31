#include "watchy_first_party/grid_render.hpp"

namespace {
using namespace watchy_first_party;

static const char kTemperature[] = "--°";
static const char kNoData[] = "NO DATA";

static void draw_grid03(watchy_canvas_t *canvas, void *user_data,
                        const watchy_time_t &time) noexcept {
    watchy_ui_fill(canvas, false);
    watchy_text_style_t heading = grid_plex10();
    watchy_text_style_t body = grid_plex11();
    watchy_text_style_t large = grid_heros62();
    const fixed_text local = format_hhmm(time, false);
    watchy_time_t tokyo{};
    const bool tokyo_valid = offset_time_checked(time, 540, &tokyo);
    const fixed_text tokyo_clock = tokyo_valid ? format_hhmm(tokyo, false) : fixed_text("--:--");
    watchy_ui_draw_text_centered(canvas, 100, 73, local.c_str(), &large);
    watchy_ui_rule(canvas, 8, 87, 184, 1u, true);
    watchy_ui_rect(canvas, 70, 88, 1, 91, true);
    watchy_ui_rect(canvas, 135, 88, 1, 91, true);
    watchy_ui_rule(canvas, 8, 179, 184, 1u, true);
    watchy_ui_draw_text_font(canvas, 14, 104, "DATE", &heading);
    watchy_ui_draw_text_font(canvas, 77, 104, "WEATHER", &heading);
    watchy_ui_draw_text_font(canvas, 148, 104, "TYO", &heading);
    const fixed_text date = format_date(time);
    watchy_ui_draw_text_font(canvas, 14, 128, date.c_str(), &body);
    watchy_ui_draw_text_font(canvas, 77, 128, kTemperature, &body);
    watchy_ui_draw_text_font(canvas, 77, 148, kNoData, &body);
    watchy_ui_draw_text_font(canvas, 148, 128, tokyo_clock.c_str(), &body);
    watchy_ui_draw_text_font(canvas, 148, 148, "UTC+09", &body);
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
