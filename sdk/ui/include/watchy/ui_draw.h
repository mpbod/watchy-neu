#ifndef WATCHY_UI_DRAW_H
#define WATCHY_UI_DRAW_H

#include <stdbool.h>
#include <stdint.h>

#include "watchy/sdk.h"
#include "watchy_fonts.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const watchy_font_t *font;
    int8_t tracking;
    bool black;
    bool outlined;
} watchy_text_style_t;

typedef struct {
    int32_t width;
    uint16_t height;
    int16_t ascent;
    int16_t descent;
} watchy_text_metrics_t;

/* One-bit canvases use a cleared bit for black and a set bit for white. All
 * writes are clipped to both the canvas metadata and the 200 x 200 panel.
 * Invalid UTF-8 consumes one byte and resolves through the '?' glyph. A valid
 * but absent codepoint uses the same fallback; if '?' is absent it is skipped.
 */
void watchy_ui_pixel(watchy_canvas_t *canvas, int16_t x, int16_t y, bool black);
void watchy_ui_fill(watchy_canvas_t *canvas, bool black);
void watchy_ui_rect(watchy_canvas_t *canvas,
                    int16_t x,
                    int16_t y,
                    int16_t width,
                    int16_t height,
                    bool black);
void watchy_ui_rect_outline(watchy_canvas_t *canvas,
                            int16_t x,
                            int16_t y,
                            int16_t width,
                            int16_t height,
                            uint8_t thickness,
                            bool black);
void watchy_ui_rule(watchy_canvas_t *canvas,
                    int16_t x,
                    int16_t y,
                    int16_t length,
                    uint8_t thickness,
                    bool black);
void watchy_ui_circle(watchy_canvas_t *canvas,
                      int16_t center_x,
                      int16_t center_y,
                      int16_t radius,
                      bool filled,
                      bool black);

/* x is the pen origin and baseline_y is the generated font baseline. Text
 * width is the sum of resolved glyph advances plus tracking between glyphs;
 * there is never trailing tracking.
 */
watchy_text_metrics_t watchy_ui_measure_text(const watchy_text_style_t *style,
                                             const char *utf8);
void watchy_ui_draw_text_font(watchy_canvas_t *canvas,
                              int16_t x,
                              int16_t baseline_y,
                              const char *utf8,
                              const watchy_text_style_t *style);
void watchy_ui_draw_text_centered(watchy_canvas_t *canvas,
                                  int16_t center_x,
                                  int16_t baseline_y,
                                  const char *utf8,
                                  const watchy_text_style_t *style);
void watchy_ui_draw_text_right(watchy_canvas_t *canvas,
                               int16_t right_x,
                               int16_t baseline_y,
                               const char *utf8,
                               const watchy_text_style_t *style);

/* Temporary shell compatibility entry point. New code should use generated
 * fonts and watchy_ui_draw_text_font().
 */
void watchy_ui_draw_text(watchy_canvas_t *canvas,
                         int16_t x,
                         int16_t y,
                         const char *text,
                         uint8_t scale,
                         bool black);

#ifdef __cplusplus
}
#endif

#endif
