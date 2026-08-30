#ifndef WATCHY_SAMPLE_DRAWING_HPP
#define WATCHY_SAMPLE_DRAWING_HPP

#include "watchy/sdk.hpp"

namespace sample {

inline void pixel(watchy_canvas_t *canvas, int x, int y, bool black = true) noexcept {
    if (canvas == nullptr || canvas->pixels == nullptr || canvas->format != WATCHY_PIXEL_MONO ||
        x < 0 || y < 0 || x >= canvas->width || y >= canvas->height) return;
    const unsigned offset = static_cast<unsigned>(y) * canvas->stride + static_cast<unsigned>(x) / 8u;
    const unsigned char bit = static_cast<unsigned char>(0x80u >> (static_cast<unsigned>(x) & 7u));
    if (black) canvas->pixels[offset] &= static_cast<unsigned char>(~bit);
    else canvas->pixels[offset] |= bit;
}

inline void fill(watchy_canvas_t *canvas, bool black = false) noexcept {
    if (canvas == nullptr || canvas->pixels == nullptr) return;
    const unsigned size = static_cast<unsigned>(canvas->stride) * canvas->height;
    for (unsigned i = 0; i < size; ++i) canvas->pixels[i] = black ? 0x00u : 0xffu;
}

inline void rect(watchy_canvas_t *canvas, int x, int y, int width, int height,
                 bool black = true) noexcept {
    for (int row = 0; row < height; ++row)
        for (int column = 0; column < width; ++column)
            pixel(canvas, x + column, y + row, black);
}

inline void segment(watchy_canvas_t *canvas, int x, int y, int width, int height,
                    int segment_index, bool enabled) noexcept {
    if (!enabled) return;
    const int thickness = 4;
    switch (segment_index) {
    case 0: rect(canvas, x + thickness, y, width - 2 * thickness, thickness); break;
    case 1: rect(canvas, x + width - thickness, y + thickness, thickness, height / 2 - thickness); break;
    case 2: rect(canvas, x + width - thickness, y + height / 2, thickness, height / 2 - thickness); break;
    case 3: rect(canvas, x + thickness, y + height - thickness, width - 2 * thickness, thickness); break;
    case 4: rect(canvas, x, y + height / 2, thickness, height / 2 - thickness); break;
    case 5: rect(canvas, x, y + thickness, thickness, height / 2 - thickness); break;
    case 6: rect(canvas, x + thickness, y + height / 2 - thickness / 2,
                 width - 2 * thickness, thickness); break;
    }
}

inline void digit(watchy_canvas_t *canvas, int x, int y, unsigned value,
                  int width = 30, int height = 54) noexcept {
    static constexpr unsigned char masks[10] = {
        0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f,
    };
    if (value > 9) value = 0;
    for (int index = 0; index < 7; ++index)
        segment(canvas, x, y, width, height, index, (masks[value] & (1u << index)) != 0u);
}

inline void number(watchy_canvas_t *canvas, int x, int y, unsigned value,
                   unsigned digits_count, int scale = 1) noexcept {
    unsigned divisor = 1;
    for (unsigned i = 1; i < digits_count; ++i) divisor *= 10;
    for (unsigned i = 0; i < digits_count; ++i) {
        digit(canvas, x + static_cast<int>(i) * 20 * scale, y, (value / divisor) % 10,
              16 * scale, 28 * scale);
        if (divisor > 1) divisor /= 10;
    }
}

inline void status_box(watchy_canvas_t *canvas, int x, int y, bool ok) noexcept {
    rect(canvas, x, y, 12, 12, true);
    rect(canvas, x + 3, y + 3, 6, 6, ok ? false : true);
}

}  // namespace sample

#endif
