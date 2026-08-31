#ifndef WATCHY_FIRST_PARTY_TERM_RENDER_HPP
#define WATCHY_FIRST_PARTY_TERM_RENDER_HPP

#include "watchy_first_party/face.h"
#include "watchy_first_party/package.hpp"
#include "watchy/ui_draw.h"

namespace watchy_first_party {

inline bool term_time(void *user_data, watchy_time_t *out) noexcept {
    const watchy_host_caps_v1_t *caps = host(user_data);
    if (out == nullptr) return false;
    *out = watchy_time_t{};
    return caps != nullptr && caps->clock != nullptr && caps->clock->now != nullptr &&
           caps->clock->now(caps->clock->context, out) == WATCHY_STATUS_OK && valid_time(*out);
}

inline bool term_battery(void *user_data, watchy_battery_state_t *out) noexcept {
    const watchy_host_caps_v1_t *caps = host(user_data);
    if (out == nullptr) return false;
    *out = watchy_battery_state_t{};
    return caps != nullptr && caps->battery != nullptr && caps->battery->read != nullptr &&
           caps->battery->read(caps->battery->context, out) == WATCHY_STATUS_OK && out->percent <= 100u;
}

inline bool term_bluetooth(void *user_data) noexcept {
    const watchy_host_caps_v1_t *caps = host(user_data);
    return caps != nullptr && caps->bluetooth != nullptr && caps->bluetooth->enabled != nullptr &&
           caps->bluetooth->enabled(caps->bluetooth->context);
}

inline watchy_text_style_t term_9(bool black = true, int8_t tracking = 0) noexcept {
    return {&watchy_font_plex_9_semibold, tracking, black, false};
}
inline watchy_text_style_t term_11(bool black = true) noexcept {
    return {&watchy_font_plex_11_semibold, 0, black, false};
}
inline watchy_text_style_t term_13(bool black = true) noexcept {
    return {&watchy_font_plex_13_bold, 0, black, false};
}
inline watchy_text_style_t term_13_medium(bool black = true) noexcept {
    return {&watchy_font_plex_13_medium, 0, black, false};
}
inline watchy_text_style_t term_30(bool black = true) noexcept {
    return {&watchy_font_plex_30_bold, 0, black, false};
}
inline watchy_text_style_t term_32(bool black = true) noexcept {
    return {&watchy_font_plex_32_bold, 0, black, false};
}
inline watchy_text_style_t term_44(bool black = true, bool outlined = false) noexcept {
    return {&watchy_font_plex_44_bold, 0, black, outlined};
}

inline fixed_text term_date(const watchy_time_t &time) noexcept {
    fixed_text result;
    const fixed_text weekday = format_weekday(time);
    const fixed_text date = format_day_month_year(time);
    uint8_t at = 0u;
    for (uint8_t i = 0u; weekday.value[i] != '\0' && at + 1u < sizeof(result.value); ++i) result.value[at++] = weekday.value[i];
    if (at + 1u < sizeof(result.value)) result.value[at++] = ' ';
    for (uint8_t i = 0u; date.value[i] != '\0' && at + 1u < sizeof(result.value); ++i) result.value[at++] = date.value[i];
    result.value[at] = '\0';
    return result;
}

inline fixed_text term_day_date(const watchy_time_t &time) noexcept {
    fixed_text result;
    const fixed_text weekday = format_weekday(time);
    const fixed_text date = format_day_month(time);
    uint8_t at = 0u;
    for (uint8_t i = 0u; weekday.value[i] != '\0' && at + 1u < sizeof(result.value); ++i) result.value[at++] = weekday.value[i];
    if (at + 1u < sizeof(result.value)) result.value[at++] = ' ';
    for (uint8_t i = 0u; date.value[i] != '\0' && at + 1u < sizeof(result.value); ++i) result.value[at++] = date.value[i];
    result.value[at] = '\0';
    return result;
}

inline fixed_text term_percent(void *user_data) noexcept {
    watchy_battery_state_t battery{};
    if (!term_battery(user_data, &battery)) return fixed_text("--%");
    return format_percent(battery.percent);
}

inline void term_time_mode(void *user_data, const watchy_time_t &time,
                           watchy_refresh_mode_t *mode) noexcept {
    (void)time;
    set_routine_refresh(user_data, mode);
}

inline void term_invalid_mode(void *user_data, watchy_refresh_mode_t *mode) noexcept {
    set_routine_refresh(user_data, mode);
}

}  // namespace watchy_first_party

#endif
