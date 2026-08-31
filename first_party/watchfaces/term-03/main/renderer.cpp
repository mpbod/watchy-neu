#include "watchy_first_party/term_render.hpp"

namespace {
using namespace watchy_first_party;

void draw_segments(watchy_canvas_t *canvas, uint8_t percent) noexcept {
    /* Ceiling makes the visible ten-cell gauge honest at the handoff's
     * 68% fixture: seven cells are filled rather than visually understating it. */
    const uint8_t filled = static_cast<uint8_t>((percent * 10u + 99u) / 100u);
    for (uint8_t index = 0u; index < 10u; ++index) {
        const int16_t x = static_cast<int16_t>(14 + index * 17u);
        watchy_ui_rect_outline(canvas, x, 164, 14, 6, 1u, false);
        if (index < filled) watchy_ui_rect(canvas, static_cast<int16_t>(x + 1), 165, 12, 4, false);
    }
}

void draw_term03(watchy_canvas_t *canvas, void *user_data, const watchy_time_t &time) noexcept {
    watchy_ui_fill(canvas, true);
    const watchy_text_style_t header = term_9(false, 1);
    const watchy_text_style_t hours = term_44(false);
    const watchy_text_style_t minutes = term_44(false, true);
    const watchy_text_style_t footer = term_9(false);
    watchy_time_t tokyo{};
    const fixed_text tyo = offset_time_checked(time, 540, &tokyo) ? format_hhmm(tokyo, false)
                                                                    : fixed_text("--:--");
    char hour[3] = {static_cast<char>('0' + time.hour / 10u),
                    static_cast<char>('0' + time.hour % 10u), '\0'};
    char minute[3] = {static_cast<char>('0' + time.minute / 10u),
                      static_cast<char>('0' + time.minute % 10u), '\0'};
    char city[12] = "TYO ";
    uint8_t at = 4u;
    for (uint8_t i = 0u; tyo.value[i] != '\0' && at + 1u < sizeof(city); ++i) city[at++] = tyo.value[i];
    city[at] = '\0';
    watchy_ui_draw_text_font(canvas, 14, 23, term_day_date(time).c_str(), &header);
    watchy_ui_draw_text_centered(canvas, 100, 90, hour, &hours);
    watchy_ui_draw_text_centered(canvas, 100, 134, minute, &minutes);
    watchy_battery_state_t battery{};
    draw_segments(canvas, term_battery(user_data, &battery) ? battery.percent : 0u);
    watchy_ui_draw_text_font(canvas, 14, 188, "--°", &footer);
    watchy_ui_draw_text_right(canvas, 186, 188, city, &footer);
}
}

extern "C" watchy_status_t term03_render(void *user_data, watchy_canvas_t *canvas,
                                            watchy_refresh_mode_t *mode) noexcept {
    if (canvas == nullptr || mode == nullptr) return WATCHY_STATUS_INVALID_ARGUMENT;
    watchy_time_t time{};
    if (!watchy_first_party::term_time(user_data, &time)) {
        watchy_ui_fill(canvas, true);
        watchy_first_party::term_invalid_mode(user_data, mode);
        return WATCHY_STATUS_OK;
    }
    draw_term03(canvas, user_data, time);
    watchy_first_party::term_time_mode(user_data, time, mode);
    return WATCHY_STATUS_OK;
}
