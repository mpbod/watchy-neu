#include "watchy_first_party/term_render.hpp"

namespace {
using namespace watchy_first_party;

void draw_term01(watchy_canvas_t *canvas, void *user_data, const watchy_time_t &time) noexcept {
    watchy_ui_fill(canvas, false);
    const watchy_text_style_t header = term_9(false, 1);
    const watchy_text_style_t command = term_11();
    const watchy_text_style_t date = term_13();
    const watchy_text_style_t clock = term_30();
    const fixed_text battery = term_percent(user_data);
    char status[16]{};
    const char *link = term_bluetooth(user_data) ? "BT·ON " : "BT·-- ";
    uint8_t at = 0u;
    for (uint8_t i = 0u; link[i] != '\0' && at + 1u < sizeof(status); ++i) status[at++] = link[i];
    for (uint8_t i = 0u; battery.value[i] != '\0' && at + 1u < sizeof(status); ++i) status[at++] = battery.value[i];
    status[at] = '\0';
    watchy_ui_rect(canvas, 12, 12, 176, 14, true);
    watchy_ui_draw_text_font(canvas, 17, 23, "WATCH.LOCAL", &header);
    watchy_ui_draw_text_right(canvas, 183, 23, status, &header);
    watchy_ui_draw_text_font(canvas, 12, 45, "$ date", &command);
    watchy_ui_draw_text_font(canvas, 12, 60, term_date(time).c_str(), &date);
    watchy_ui_draw_text_font(canvas, 12, 79, "$ time", &command);
    watchy_ui_draw_text_font(canvas, 12, 109, format_hhmm(time, false).c_str(), &clock);
    watchy_ui_draw_text_font(canvas, 12, 128, "$ wx", &command);
    watchy_ui_draw_text_font(canvas, 12, 145, "NO DATA --°", &date);
    watchy_ui_draw_text_font(canvas, 12, 186, "$", &command);
    watchy_ui_rect(canvas, 27, 174, 7, 13, true);
}
}

extern "C" watchy_status_t term01_render(void *user_data, watchy_canvas_t *canvas,
                                            watchy_refresh_mode_t *mode) noexcept {
    if (canvas == nullptr || mode == nullptr) return WATCHY_STATUS_INVALID_ARGUMENT;
    watchy_time_t time{};
    if (!watchy_first_party::term_time(user_data, &time)) {
        watchy_ui_fill(canvas, false);
        const watchy_text_style_t style = watchy_first_party::term_9();
        watchy_ui_draw_text_font(canvas, 12, 25, "WATCH.LOCAL", &style);
        watchy_first_party::term_invalid_mode(user_data, mode);
        return WATCHY_STATUS_OK;
    }
    draw_term01(canvas, user_data, time);
    watchy_first_party::term_time_mode(user_data, time, mode);
    return WATCHY_STATUS_OK;
}
