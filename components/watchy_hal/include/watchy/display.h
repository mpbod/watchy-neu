#ifndef WATCHY_DISPLAY_H
#define WATCHY_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "watchy/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHY_DISPLAY_WIDTH 200
#define WATCHY_DISPLAY_HEIGHT 200
#define WATCHY_DISPLAY_STRIDE 25
#define WATCHY_DISPLAY_FRAMEBUFFER_SIZE 5000

void watchy_framebuffer_fill(uint8_t *framebuffer, size_t size, bool black);
void watchy_framebuffer_draw_pixel(uint8_t *framebuffer,
                                   size_t size,
                                   int16_t x,
                                   int16_t y,
                                   bool black);
void watchy_framebuffer_fill_rect(uint8_t *framebuffer,
                                  size_t size,
                                  int16_t x,
                                  int16_t y,
                                  int16_t width,
                                  int16_t height,
                                  bool black);

watchy_status_t watchy_display_init(void);
bool watchy_display_ready(void);
watchy_status_t watchy_display_set_partial_limit(uint16_t partial_limit);
watchy_canvas_t watchy_display_acquire(void);
watchy_status_t watchy_display_refresh(watchy_refresh_mode_t requested);
watchy_status_t watchy_display_power_off(void);
watchy_status_t watchy_display_deep_sleep(void);
void watchy_display_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
