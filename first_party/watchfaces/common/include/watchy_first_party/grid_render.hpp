#ifndef WATCHY_FIRST_PARTY_GRID_RENDER_HPP
#define WATCHY_FIRST_PARTY_GRID_RENDER_HPP

#include "watchy_first_party/face.h"
#include "watchy_first_party/package.hpp"
#include "watchy/ui_draw.h"

namespace watchy_first_party {

inline const watchy_host_caps_v1_t *grid_host(void *user_data) noexcept {
    return host(user_data);
}

inline bool grid_time(void *user_data, watchy_time_t *out) noexcept {
    const watchy_host_caps_v1_t *caps = grid_host(user_data);
    if (out == nullptr) return false;
    *out = watchy_time_t{};
    if (caps == nullptr || caps->clock == nullptr || caps->clock->now == nullptr) return false;
    return caps->clock->now(caps->clock->context, out) == WATCHY_STATUS_OK && valid_time(*out);
}

inline bool grid_battery(void *user_data, watchy_battery_state_t *out) noexcept {
    const watchy_host_caps_v1_t *caps = grid_host(user_data);
    if (out == nullptr) return false;
    *out = watchy_battery_state_t{};
    if (caps == nullptr || caps->battery == nullptr || caps->battery->read == nullptr) return false;
    return caps->battery->read(caps->battery->context, out) == WATCHY_STATUS_OK && out->percent <= 100u;
}

inline bool grid_bluetooth(void *user_data) noexcept {
    const watchy_host_caps_v1_t *caps = grid_host(user_data);
    return caps != nullptr && caps->bluetooth != nullptr && caps->bluetooth->enabled != nullptr &&
           caps->bluetooth->enabled(caps->bluetooth->context);
}

inline watchy_text_style_t grid_plex10(bool black = true) noexcept {
    return {&watchy_font_plex_10_semibold, 1, black, false};
}

inline watchy_text_style_t grid_plex11(bool black = true) noexcept {
    return {&watchy_font_plex_11_regular, 0, black, false};
}

inline watchy_text_style_t grid_plex13(bool black = true) noexcept {
    return {&watchy_font_plex_13_medium, 0, black, false};
}

inline watchy_text_style_t grid_heros62(bool black = true) noexcept {
    return {&watchy_font_heros_62_bold, 0, black, false};
}

inline void grid_time_mode(void *user_data, const watchy_time_t &time,
                           watchy_refresh_mode_t *mode) noexcept {
    if (mode == nullptr) return;
    *mode = full_refresh_for_hour(user_data, time.hour) ? WATCHY_REFRESH_FULL : WATCHY_REFRESH_PARTIAL;
}

inline void grid_invalid_mode(void *user_data, watchy_refresh_mode_t *mode) noexcept {
    if (mode == nullptr) return;
    *mode = full_refresh_for_hour(user_data, 0u) ? WATCHY_REFRESH_FULL : WATCHY_REFRESH_PARTIAL;
}

inline void grid_footer_rule(watchy_canvas_t *canvas) noexcept {
    watchy_ui_rule(canvas, 8, 151, 184, 1u, true);
    watchy_ui_rect(canvas, 8, 151, 1, 46, true);
    watchy_ui_rect(canvas, 71, 151, 1, 46, true);
    watchy_ui_rect(canvas, 135, 151, 1, 46, true);
    watchy_ui_rect(canvas, 191, 151, 1, 46, true);
}

inline void grid_draw_battery(watchy_canvas_t *canvas, void *user_data,
                              int16_t x, int16_t baseline) noexcept {
    watchy_battery_state_t battery{};
    fixed_text value;
    if (grid_battery(user_data, &battery)) {
        value.value[0] = static_cast<char>('0' + battery.percent / 10u);
        value.value[1] = static_cast<char>('0' + battery.percent % 10u);
        value.value[2] = '%';
        value.value[3] = '\0';
    } else {
        value = fixed_text("--%");
    }
    watchy_text_style_t style = grid_plex13();
    watchy_ui_draw_text_font(canvas, x, baseline, value.c_str(), &style);
}

}  // namespace watchy_first_party

#endif
