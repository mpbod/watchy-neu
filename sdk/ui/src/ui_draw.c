#include "watchy/ui_draw.h"

#include <limits.h>
#include <stddef.h>

enum {
    WATCHY_UI_PANEL_WIDTH = 200,
    WATCHY_UI_PANEL_HEIGHT = 200,
};

typedef struct {
    uint32_t codepoint;
    uint8_t length;
} decoded_codepoint_t;

static bool canvas_bounds(const watchy_canvas_t *canvas, int32_t *width, int32_t *height) {
    int32_t bounded_width;
    int32_t bounded_height;
    int32_t stride_width;
    if (canvas == NULL || canvas->pixels == NULL || canvas->format != WATCHY_PIXEL_MONO ||
        canvas->stride == 0u) {
        return false;
    }
    bounded_width = canvas->width < WATCHY_UI_PANEL_WIDTH
        ? (int32_t)canvas->width : WATCHY_UI_PANEL_WIDTH;
    bounded_height = canvas->height < WATCHY_UI_PANEL_HEIGHT
        ? (int32_t)canvas->height : WATCHY_UI_PANEL_HEIGHT;
    stride_width = (int32_t)canvas->stride * 8;
    if (bounded_width > stride_width) {
        bounded_width = stride_width;
    }
    if (bounded_width <= 0 || bounded_height <= 0) {
        return false;
    }
    *width = bounded_width;
    *height = bounded_height;
    return true;
}

static void put_pixel(watchy_canvas_t *canvas, int32_t x, int32_t y, bool black) {
    int32_t width;
    int32_t height;
    size_t offset;
    uint8_t mask;
    if (!canvas_bounds(canvas, &width, &height) || x < 0 || y < 0 || x >= width || y >= height) {
        return;
    }
    offset = (size_t)y * canvas->stride + (size_t)x / 8u;
    mask = (uint8_t)(0x80u >> ((uint32_t)x & 7u));
    if (black) {
        canvas->pixels[offset] &= (uint8_t)~mask;
    } else {
        canvas->pixels[offset] |= mask;
    }
}

static void fill_rect(watchy_canvas_t *canvas,
                      int32_t x,
                      int32_t y,
                      int32_t width,
                      int32_t height,
                      bool black) {
    int32_t canvas_width;
    int32_t canvas_height;
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
    if (width <= 0 || height <= 0 || !canvas_bounds(canvas, &canvas_width, &canvas_height)) {
        return;
    }
    left = x > 0 ? x : 0;
    top = y > 0 ? y : 0;
    right = x + width;
    bottom = y + height;
    if (right > canvas_width) {
        right = canvas_width;
    }
    if (bottom > canvas_height) {
        bottom = canvas_height;
    }
    if (left >= right || top >= bottom) {
        return;
    }
    for (int32_t row = top; row < bottom; ++row) {
        for (int32_t column = left; column < right; ++column) {
            put_pixel(canvas, column, row, black);
        }
    }
}

void watchy_ui_pixel(watchy_canvas_t *canvas, int16_t x, int16_t y, bool black) {
    put_pixel(canvas, x, y, black);
}

void watchy_ui_fill(watchy_canvas_t *canvas, bool black) {
    int32_t width;
    int32_t height;
    if (canvas_bounds(canvas, &width, &height)) {
        fill_rect(canvas, 0, 0, width, height, black);
    }
}

void watchy_ui_rect(watchy_canvas_t *canvas,
                    int16_t x,
                    int16_t y,
                    int16_t width,
                    int16_t height,
                    bool black) {
    fill_rect(canvas, x, y, width, height, black);
}

void watchy_ui_rect_outline(watchy_canvas_t *canvas,
                            int16_t x,
                            int16_t y,
                            int16_t width,
                            int16_t height,
                            uint8_t thickness,
                            bool black) {
    const int32_t stroke = thickness;
    if (width <= 0 || height <= 0 || thickness == 0u) {
        return;
    }
    if (stroke * 2 >= width || stroke * 2 >= height) {
        fill_rect(canvas, x, y, width, height, black);
        return;
    }
    fill_rect(canvas, x, y, width, stroke, black);
    fill_rect(canvas, x, (int32_t)y + height - stroke, width, stroke, black);
    fill_rect(canvas, x, (int32_t)y + stroke, stroke, (int32_t)height - stroke * 2, black);
    fill_rect(canvas, (int32_t)x + width - stroke, (int32_t)y + stroke,
              stroke, (int32_t)height - stroke * 2, black);
}

void watchy_ui_rule(watchy_canvas_t *canvas,
                    int16_t x,
                    int16_t y,
                    int16_t length,
                    uint8_t thickness,
                    bool black) {
    fill_rect(canvas, x, y, length, thickness, black);
}

void watchy_ui_circle(watchy_canvas_t *canvas,
                      int16_t center_x,
                      int16_t center_y,
                      int16_t radius,
                      bool filled,
                      bool black) {
    int32_t width;
    int32_t height;
    int32_t left;
    int32_t right;
    int32_t top;
    int32_t bottom;
    int32_t outer_squared;
    int32_t inner_squared;
    if (radius < 0 || !canvas_bounds(canvas, &width, &height)) {
        return;
    }
    if (radius == 0) {
        put_pixel(canvas, center_x, center_y, black);
        return;
    }
    left = (int32_t)center_x - radius;
    right = (int32_t)center_x + radius;
    top = (int32_t)center_y - radius;
    bottom = (int32_t)center_y + radius;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right >= width) right = width - 1;
    if (bottom >= height) bottom = height - 1;
    outer_squared = (int32_t)radius * radius;
    inner_squared = (int32_t)(radius - 1) * (radius - 1);
    for (int32_t y = top; y <= bottom; ++y) {
        for (int32_t x = left; x <= right; ++x) {
            const int32_t dx = x - center_x;
            const int32_t dy = y - center_y;
            const int32_t distance_squared = dx * dx + dy * dy;
            if (distance_squared <= outer_squared && (filled || distance_squared > inner_squared)) {
                put_pixel(canvas, x, y, black);
            }
        }
    }
}

static decoded_codepoint_t decode_utf8(const unsigned char *text) {
    const uint8_t first = text[0];
    decoded_codepoint_t decoded = {'?', 1u};
    if (first < 0x80u) {
        decoded.codepoint = first;
        return decoded;
    }
    if (first >= 0xc2u && first <= 0xdfu && text[1] != 0u &&
        text[1] >= 0x80u && text[1] <= 0xbfu) {
        decoded.codepoint = ((uint32_t)(first & 0x1fu) << 6) | (uint32_t)(text[1] & 0x3fu);
        decoded.length = 2u;
        return decoded;
    }
    if (first >= 0xe0u && first <= 0xefu && text[1] != 0u && text[2] != 0u &&
        text[1] >= 0x80u && text[1] <= 0xbfu && text[2] >= 0x80u && text[2] <= 0xbfu &&
        (first != 0xe0u || text[1] >= 0xa0u) &&
        (first != 0xedu || text[1] <= 0x9fu)) {
        decoded.codepoint = ((uint32_t)(first & 0x0fu) << 12) |
                            ((uint32_t)(text[1] & 0x3fu) << 6) |
                            (uint32_t)(text[2] & 0x3fu);
        decoded.length = 3u;
        return decoded;
    }
    if (first >= 0xf0u && first <= 0xf4u && text[1] != 0u && text[2] != 0u &&
        text[3] != 0u && text[1] >= 0x80u && text[1] <= 0xbfu &&
        text[2] >= 0x80u && text[2] <= 0xbfu && text[3] >= 0x80u && text[3] <= 0xbfu &&
        (first != 0xf0u || text[1] >= 0x90u) &&
        (first != 0xf4u || text[1] <= 0x8fu)) {
        decoded.codepoint = ((uint32_t)(first & 0x07u) << 18) |
                            ((uint32_t)(text[1] & 0x3fu) << 12) |
                            ((uint32_t)(text[2] & 0x3fu) << 6) |
                            (uint32_t)(text[3] & 0x3fu);
        decoded.length = 4u;
    }
    return decoded;
}

static const watchy_font_glyph_t *resolve_glyph(const watchy_font_t *font, uint32_t codepoint) {
    const watchy_font_glyph_t *glyph;
    if (font == NULL || font->glyphs == NULL || font->glyph_count == 0u) {
        return NULL;
    }
    glyph = watchy_font_find_glyph(font, codepoint);
    if (glyph == NULL && codepoint != '?') {
        glyph = watchy_font_find_glyph(font, '?');
    }
    return glyph;
}

static int32_t saturating_add(int32_t value, int32_t addend) {
    const int64_t result = (int64_t)value + addend;
    if (result > INT32_MAX) return INT32_MAX;
    if (result < INT32_MIN) return INT32_MIN;
    return (int32_t)result;
}

watchy_text_metrics_t watchy_ui_measure_text(const watchy_text_style_t *style,
                                             const char *utf8) {
    watchy_text_metrics_t metrics = {0, 0u, 0, 0};
    const unsigned char *cursor = (const unsigned char *)utf8;
    bool has_glyph = false;
    if (style == NULL || style->font == NULL || utf8 == NULL) {
        return metrics;
    }
    while (*cursor != 0u) {
        const decoded_codepoint_t decoded = decode_utf8(cursor);
        const watchy_font_glyph_t *glyph = resolve_glyph(style->font, decoded.codepoint);
        cursor += decoded.length;
        if (glyph == NULL) {
            continue;
        }
        if (has_glyph) {
            metrics.width = saturating_add(metrics.width, style->tracking);
        }
        metrics.width = saturating_add(metrics.width, glyph->advance);
        has_glyph = true;
    }
    if (has_glyph) {
        metrics.height = style->font->line_height;
        metrics.ascent = style->font->ascent;
        metrics.descent = style->font->descent;
    }
    return metrics;
}

static bool glyph_pixel(const watchy_font_t *font,
                        const watchy_font_glyph_t *glyph,
                        int32_t x,
                        int32_t y) {
    size_t offset;
    uint8_t mask;
    if (x < 0 || y < 0 || x >= glyph->width || y >= glyph->height ||
        glyph->stride == 0u || font->bitmap == NULL) {
        return false;
    }
    offset = (size_t)glyph->bitmap_offset + (size_t)y * glyph->stride + (size_t)x / 8u;
    mask = (uint8_t)(0x80u >> ((uint32_t)x & 7u));
    return (font->bitmap[offset] & mask) != 0u;
}

static void draw_glyph(watchy_canvas_t *canvas,
                       int32_t pen_x,
                       int32_t baseline_y,
                       const watchy_font_t *font,
                       const watchy_font_glyph_t *glyph,
                       bool outlined,
                       bool black) {
    const int32_t origin_x = pen_x + glyph->bearing_x;
    const int32_t origin_y = baseline_y - glyph->bearing_y;
    for (int32_t row = 0; row < glyph->height; ++row) {
        for (int32_t column = 0; column < glyph->width; ++column) {
            bool draw = glyph_pixel(font, glyph, column, row);
            if (draw && outlined) {
                draw = !glyph_pixel(font, glyph, column - 1, row) ||
                       !glyph_pixel(font, glyph, column + 1, row) ||
                       !glyph_pixel(font, glyph, column, row - 1) ||
                       !glyph_pixel(font, glyph, column, row + 1);
            }
            if (draw) {
                put_pixel(canvas, origin_x + column, origin_y + row, black);
            }
        }
    }
}

static void draw_text_at(watchy_canvas_t *canvas,
                         int32_t x,
                         int32_t baseline_y,
                         const char *utf8,
                         const watchy_text_style_t *style) {
    const unsigned char *cursor = (const unsigned char *)utf8;
    int32_t pen_x = x;
    bool has_glyph = false;
    if (style == NULL || style->font == NULL || utf8 == NULL) {
        return;
    }
    while (*cursor != 0u) {
        const decoded_codepoint_t decoded = decode_utf8(cursor);
        const watchy_font_glyph_t *glyph = resolve_glyph(style->font, decoded.codepoint);
        cursor += decoded.length;
        if (glyph == NULL) {
            continue;
        }
        if (has_glyph) {
            pen_x = saturating_add(pen_x, style->tracking);
        }
        draw_glyph(canvas, pen_x, baseline_y, style->font, glyph, style->outlined, style->black);
        pen_x = saturating_add(pen_x, glyph->advance);
        has_glyph = true;
    }
}

void watchy_ui_draw_text_font(watchy_canvas_t *canvas,
                              int16_t x,
                              int16_t baseline_y,
                              const char *utf8,
                              const watchy_text_style_t *style) {
    draw_text_at(canvas, x, baseline_y, utf8, style);
}

void watchy_ui_draw_text_centered(watchy_canvas_t *canvas,
                                  int16_t center_x,
                                  int16_t baseline_y,
                                  const char *utf8,
                                  const watchy_text_style_t *style) {
    const watchy_text_metrics_t metrics = watchy_ui_measure_text(style, utf8);
    draw_text_at(canvas, (int32_t)center_x - metrics.width / 2, baseline_y, utf8, style);
}

void watchy_ui_draw_text_right(watchy_canvas_t *canvas,
                               int16_t right_x,
                               int16_t baseline_y,
                               const char *utf8,
                               const watchy_text_style_t *style) {
    const watchy_text_metrics_t metrics = watchy_ui_measure_text(style, utf8);
    draw_text_at(canvas, (int32_t)right_x - metrics.width, baseline_y, utf8, style);
}

/* Public-domain 5 x 7 column font retained only for the temporary shell API.
 * Each low bit is the top pixel; entries cover ASCII 0x20 through 0x7f.
 */
static const uint8_t COMPAT_FONT[96][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5f,0x00,0x00},
    {0x00,0x07,0x00,0x07,0x00},{0x14,0x7f,0x14,0x7f,0x14},
    {0x24,0x2a,0x7f,0x2a,0x12},{0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1c,0x22,0x41,0x00},{0x00,0x41,0x22,0x1c,0x00},
    {0x14,0x08,0x3e,0x08,0x14},{0x08,0x08,0x3e,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},
    {0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},
    {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e},
    {0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3e},{0x7e,0x11,0x11,0x11,0x7e},
    {0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
    {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},
    {0x7f,0x09,0x09,0x09,0x01},{0x3e,0x41,0x49,0x49,0x7a},
    {0x7f,0x08,0x08,0x08,0x7f},{0x00,0x41,0x7f,0x41,0x00},
    {0x20,0x40,0x41,0x3f,0x01},{0x7f,0x08,0x14,0x22,0x41},
    {0x7f,0x40,0x40,0x40,0x40},{0x7f,0x02,0x0c,0x02,0x7f},
    {0x7f,0x04,0x08,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
    {0x7f,0x09,0x09,0x09,0x06},{0x3e,0x41,0x51,0x21,0x5e},
    {0x7f,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7f,0x01,0x01},{0x3f,0x40,0x40,0x40,0x3f},
    {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},
    {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},
    {0x61,0x51,0x49,0x45,0x43},{0x00,0x7f,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7f,0x00},
    {0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
    {0x7f,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7f},{0x38,0x54,0x54,0x54,0x18},
    {0x08,0x7e,0x09,0x01,0x02},{0x0c,0x52,0x52,0x52,0x3e},
    {0x7f,0x08,0x04,0x04,0x78},{0x00,0x44,0x7d,0x40,0x00},
    {0x20,0x40,0x44,0x3d,0x00},{0x7f,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7f,0x40,0x00},{0x7c,0x04,0x18,0x04,0x78},
    {0x7c,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
    {0x7c,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7c},
    {0x7c,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3f,0x44,0x40,0x20},{0x3c,0x40,0x40,0x20,0x7c},
    {0x1c,0x20,0x40,0x20,0x1c},{0x3c,0x40,0x30,0x40,0x3c},
    {0x44,0x28,0x10,0x28,0x44},{0x0c,0x50,0x50,0x50,0x3c},
    {0x44,0x64,0x54,0x4c,0x44},{0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x7f,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},
    {0x10,0x08,0x08,0x10,0x08},{0x00,0x06,0x09,0x09,0x06},
};

void watchy_ui_draw_text(watchy_canvas_t *canvas,
                         int16_t x,
                         int16_t y,
                         const char *text,
                         uint8_t scale,
                         bool black) {
    int32_t cursor = x;
    if (canvas == NULL || text == NULL || scale == 0u) {
        return;
    }
    while (*text != '\0') {
        unsigned char character = (unsigned char)*text++;
        if (character < 0x20u || character > 0x7fu) {
            character = '?';
        }
        for (uint8_t column = 0u; column < 5u; ++column) {
            const uint8_t bits = COMPAT_FONT[character - 0x20u][column];
            for (uint8_t row = 0u; row < 7u; ++row) {
                if ((bits & (uint8_t)(1u << row)) != 0u) {
                    fill_rect(canvas,
                              cursor + (int32_t)column * scale,
                              (int32_t)y + (int32_t)row * scale,
                              scale, scale, black);
                }
            }
        }
        cursor = saturating_add(cursor, 6 * scale);
    }
}
