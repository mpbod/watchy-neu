#ifndef WATCHY_UI_H
#define WATCHY_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "watchy/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

void watchy_ui_draw_text(watchy_canvas_t *canvas,
                         int16_t x,
                         int16_t y,
                         const char *text,
                         uint8_t scale,
                         bool black);
void watchy_ui_fill(watchy_canvas_t *canvas, bool black);
void watchy_ui_rect(watchy_canvas_t *canvas,
                    int16_t x,
                    int16_t y,
                    int16_t width,
                    int16_t height,
                    bool black);

#ifdef __cplusplus
}
#endif

#endif
