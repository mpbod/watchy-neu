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

inline watchy_text_style_t grid_heading_style(bool black = true) noexcept {
    return {&watchy_font_plex_11_semibold, 1, black, false};
}

inline watchy_text_style_t grid_header_style(bool black = true) noexcept {
    return {&watchy_font_plex_10_semibold, 1, black, false};
}

inline watchy_text_style_t grid_footer_style(bool black = true) noexcept {
    return {&watchy_font_plex_8_semibold, 1, black, false};
}

inline watchy_text_style_t grid_small_style(bool black = true) noexcept {
    return {&watchy_font_plex_8_semibold, 0, black, false};
}

inline watchy_text_style_t grid_rail_style(bool black = true) noexcept {
    return {&watchy_font_plex_9_semibold, 2, black, false};
}

inline watchy_text_style_t grid_body_style(bool black = true) noexcept {
    return {&watchy_font_plex_13_regular, 0, black, false};
}

inline watchy_text_style_t grid_value_style(bool black = true) noexcept {
    return {&watchy_font_plex_15_medium, 0, black, false};
}

inline watchy_text_style_t grid_day_style(bool black = true) noexcept {
    return {&watchy_font_plex_22_bold, 0, black, false};
}

inline watchy_text_style_t grid_large_clock_style(bool black = true) noexcept {
    return {&watchy_font_heros_62_bold, 0, black, false};
}

inline watchy_text_style_t grid_rail_clock_style(bool black = true) noexcept {
    return {&watchy_font_heros_62_regular, 0, black, false};
}

inline watchy_text_style_t grid_modular_clock_style(bool black = true) noexcept {
    return {&watchy_font_heros_74_regular, 0, black, false};
}

/* These bounded raster helpers keep the generated strikes intact while
 * fitting the wide Heros numerals into the handoff's measured regions. They
 * intentionally write through the clipped canvas API, so package renderers
 * cannot leak pixels at an edge. */
inline const watchy_font_glyph_t *grid_find_glyph(const watchy_font_t *font,
                                                  uint8_t codepoint) noexcept {
    const watchy_font_glyph_t *glyph = watchy_font_find_glyph(font, codepoint);
    if (glyph == nullptr && codepoint != static_cast<uint8_t>('?')) {
        glyph = watchy_font_find_glyph(font, static_cast<uint8_t>('?'));
    }
    return glyph;
}

inline int32_t grid_text_raw_width(const watchy_text_style_t &style,
                                   const char *text) noexcept {
    int32_t width = 0;
    bool has_glyph = false;
    if (style.font == nullptr || text == nullptr) return 0;
    for (const uint8_t *cursor = reinterpret_cast<const uint8_t *>(text);
         *cursor != 0u; ++cursor) {
        const watchy_font_glyph_t *glyph = grid_find_glyph(style.font, *cursor);
        if (glyph == nullptr) continue;
        if (has_glyph) width += style.tracking;
        width += glyph->advance;
        has_glyph = true;
    }
    return width > 0 ? width : 0;
}

inline uint16_t grid_fit_width(const watchy_text_style_t &style,
                               const char *text,
                               uint16_t maximum) noexcept {
    const int32_t raw = grid_text_raw_width(style, text);
    return static_cast<uint16_t>(raw < static_cast<int32_t>(maximum) ? raw : maximum);
}

inline bool grid_glyph_pixel(const watchy_font_t *font,
                             const watchy_font_glyph_t *glyph,
                             uint16_t x,
                             uint16_t y) noexcept {
    if (font == nullptr || glyph == nullptr || font->bitmap == nullptr ||
        x >= glyph->width || y >= glyph->height || glyph->stride == 0u) {
        return false;
    }
    const size_t offset = static_cast<size_t>(glyph->bitmap_offset) +
                          static_cast<size_t>(y) * glyph->stride + x / 8u;
    return (font->bitmap[offset] & static_cast<uint8_t>(0x80u >> (x & 7u))) != 0u;
}

inline void grid_draw_text_fit(watchy_canvas_t *canvas,
                               int16_t x,
                               int16_t baseline,
                               const char *text,
                               const watchy_text_style_t &style,
                               uint16_t maximum) noexcept {
    const int32_t raw = grid_text_raw_width(style, text);
    if (canvas == nullptr || raw <= 0 || text == nullptr) return;
    const int32_t target = raw < static_cast<int32_t>(maximum) ? raw : maximum;
    int32_t pen = 0;
    bool has_glyph = false;
    for (const uint8_t *cursor = reinterpret_cast<const uint8_t *>(text);
         *cursor != 0u; ++cursor) {
        const watchy_font_glyph_t *glyph = grid_find_glyph(style.font, *cursor);
        if (glyph == nullptr) continue;
        if (has_glyph) pen += style.tracking;
        const int32_t scaled_pen = pen * target / raw;
        const int32_t scaled_bearing = static_cast<int32_t>(glyph->bearing_x) * target / raw;
        const int32_t origin_x = static_cast<int32_t>(x) + scaled_pen + scaled_bearing;
        const int32_t origin_y = static_cast<int32_t>(baseline) - glyph->bearing_y;
        for (uint16_t row = 0u; row < glyph->height; ++row) {
            for (uint16_t column = 0u; column < glyph->width; ++column) {
                if (!grid_glyph_pixel(style.font, glyph, column, row)) continue;
                const int32_t left = static_cast<int32_t>(column) * target / raw;
                const int32_t right =
                    (static_cast<int32_t>(column) + 1) * target / raw - 1;
                for (int32_t destination = left; destination <= right; ++destination) {
                    watchy_ui_pixel(canvas, static_cast<int16_t>(origin_x + destination),
                                    static_cast<int16_t>(origin_y + row), style.black);
                }
            }
        }
        pen += glyph->advance;
        has_glyph = true;
    }
}

inline void grid_draw_text_fit_centered(watchy_canvas_t *canvas,
                                        int16_t center_x,
                                        int16_t baseline,
                                        const char *text,
                                        const watchy_text_style_t &style,
                                        uint16_t maximum) noexcept {
    const uint16_t width = grid_fit_width(style, text, maximum);
    grid_draw_text_fit(canvas, static_cast<int16_t>(center_x - width / 2u), baseline,
                       text, style, maximum);
}

inline void grid_draw_rotated_text(watchy_canvas_t *canvas,
                                   int16_t rail_x,
                                   int16_t start_y,
                                   const char *text,
                                   const watchy_text_style_t &style) noexcept {
    if (canvas == nullptr || style.font == nullptr || text == nullptr) return;
    int32_t pen_y = 0;
    bool has_glyph = false;
    for (const uint8_t *cursor = reinterpret_cast<const uint8_t *>(text);
         *cursor != 0u; ++cursor) {
        const watchy_font_glyph_t *glyph = grid_find_glyph(style.font, *cursor);
        if (glyph == nullptr) continue;
        if (has_glyph) pen_y += style.tracking;
        for (uint16_t row = 0u; row < glyph->height; ++row) {
            for (uint16_t column = 0u; column < glyph->width; ++column) {
                if (!grid_glyph_pixel(style.font, glyph, column, row)) continue;
                watchy_ui_pixel(canvas,
                                static_cast<int16_t>(rail_x + glyph->height - 1u - row),
                                static_cast<int16_t>(start_y + pen_y + column + glyph->bearing_x),
                                style.black);
            }
        }
        pen_y += glyph->advance;
        has_glyph = true;
    }
}

inline void grid_time_mode(void *user_data, const watchy_time_t &time,
                           watchy_refresh_mode_t *mode) noexcept {
    (void)time;
    set_routine_refresh(user_data, mode);
}

inline void grid_invalid_mode(void *user_data, watchy_refresh_mode_t *mode) noexcept {
    set_routine_refresh(user_data, mode);
}

inline void grid_footer_rule(watchy_canvas_t *canvas) noexcept {
    watchy_ui_rule(canvas, 14, 158, 172, 1u, true);
    watchy_ui_rect(canvas, 14, 158, 1, 41, true);
    watchy_ui_rect(canvas, 71, 158, 1, 41, true);
    watchy_ui_rect(canvas, 128, 158, 1, 41, true);
    watchy_ui_rect(canvas, 185, 158, 1, 41, true);
}

inline void grid_draw_battery(watchy_canvas_t *canvas, void *user_data,
                              int16_t x, int16_t baseline) noexcept {
    watchy_battery_state_t battery{};
    fixed_text value;
    if (grid_battery(user_data, &battery)) {
        value = format_percent(battery.percent);
    } else {
        value = fixed_text("--%");
    }
    watchy_text_style_t style = grid_value_style();
    watchy_ui_draw_text_font(canvas, x, baseline, value.c_str(), &style);
}

}  // namespace watchy_first_party

#endif
