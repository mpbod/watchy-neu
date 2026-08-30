#include "watchy/display.h"

#include <string.h>

void watchy_framebuffer_fill(uint8_t *framebuffer, size_t size, bool black) {
    if (framebuffer == NULL || size < WATCHY_DISPLAY_FRAMEBUFFER_SIZE) {
        return;
    }
    memset(framebuffer, black ? 0x00 : 0xff, WATCHY_DISPLAY_FRAMEBUFFER_SIZE);
}

void watchy_framebuffer_draw_pixel(uint8_t *framebuffer,
                                   size_t size,
                                   int16_t x,
                                   int16_t y,
                                   bool black) {
    size_t offset;
    uint8_t bit;

    if (framebuffer == NULL || size < WATCHY_DISPLAY_FRAMEBUFFER_SIZE || x < 0 ||
        y < 0 || x >= WATCHY_DISPLAY_WIDTH || y >= WATCHY_DISPLAY_HEIGHT) {
        return;
    }

    offset = (size_t)y * WATCHY_DISPLAY_STRIDE + (size_t)x / 8u;
    bit = (uint8_t)(0x80u >> ((unsigned)x & 7u));
    if (black) {
        framebuffer[offset] &= (uint8_t)~bit;
    } else {
        framebuffer[offset] |= bit;
    }
}

void watchy_framebuffer_fill_rect(uint8_t *framebuffer,
                                  size_t size,
                                  int16_t x,
                                  int16_t y,
                                  int16_t width,
                                  int16_t height,
                                  bool black) {
    int32_t left = x;
    int32_t top = y;
    int32_t right = left + width;
    int32_t bottom = top + height;

    if (framebuffer == NULL || size < WATCHY_DISPLAY_FRAMEBUFFER_SIZE || width <= 0 || height <= 0) {
        return;
    }
    if (left < 0) {
        left = 0;
    }
    if (top < 0) {
        top = 0;
    }
    if (right > WATCHY_DISPLAY_WIDTH) {
        right = WATCHY_DISPLAY_WIDTH;
    }
    if (bottom > WATCHY_DISPLAY_HEIGHT) {
        bottom = WATCHY_DISPLAY_HEIGHT;
    }
    for (int32_t row = top; row < bottom; ++row) {
        for (int32_t column = left; column < right; ++column) {
            watchy_framebuffer_draw_pixel(framebuffer, size, (int16_t)column, (int16_t)row, black);
        }
    }
}
