#include "watchy/transition.h"

#include <string.h>

#define WATCHY_TRANSITION_STRIDE (WATCHY_TRANSITION_CANVAS_WIDTH / 8)

typedef struct {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} clipped_rect_t;

static uint8_t expected_write_count(watchy_transition_effect_t effect) {
    switch (effect) {
    case WATCHY_TRANSITION_CUT:
        return 1u;
    case WATCHY_TRANSITION_FLASH:
    case WATCHY_TRANSITION_DITHER:
        return 2u;
    case WATCHY_TRANSITION_PUSH:
    case WATCHY_TRANSITION_GROW:
    case WATCHY_TRANSITION_ODOMETER:
    case WATCHY_TRANSITION_SPLIT:
        return 3u;
    case WATCHY_TRANSITION_WIPE:
        return 4u;
    case WATCHY_TRANSITION_FILL:
    case WATCHY_TRANSITION_SHUTTER:
        return 5u;
    }
    return 0u;
}

static bool clip_rect(watchy_transition_rect_t rect, clipped_rect_t *out) {
    int32_t left = rect.x;
    int32_t top = rect.y;
    int32_t right;
    int32_t bottom;

    if (rect.width <= 0 || rect.height <= 0) {
        return false;
    }
    right = left + (int32_t)rect.width;
    bottom = top + (int32_t)rect.height;
    if (right <= 0 || bottom <= 0 || left >= WATCHY_TRANSITION_CANVAS_WIDTH ||
        top >= WATCHY_TRANSITION_CANVAS_HEIGHT) {
        return false;
    }
    if (left < 0) {
        left = 0;
    }
    if (top < 0) {
        top = 0;
    }
    if (right > WATCHY_TRANSITION_CANVAS_WIDTH) {
        right = WATCHY_TRANSITION_CANVAS_WIDTH;
    }
    if (bottom > WATCHY_TRANSITION_CANVAS_HEIGHT) {
        bottom = WATCHY_TRANSITION_CANVAS_HEIGHT;
    }
    *out = (clipped_rect_t){left, top, right, bottom};
    return true;
}

static bool valid_plan(const watchy_transition_plan_t *plan, clipped_rect_t *rect) {
    const uint8_t count = plan != NULL ? expected_write_count(plan->effect) : 0u;

    if (plan == NULL || count == 0u || plan->direction < WATCHY_TRANSITION_DIRECTION_NONE ||
        plan->direction > WATCHY_TRANSITION_DIRECTION_DOWN || !clip_rect(plan->rect, rect)) {
        return false;
    }
    if (plan->mandatory_clear) {
        return plan->effect == WATCHY_TRANSITION_CUT &&
               plan->direction == WATCHY_TRANSITION_DIRECTION_NONE && plan->write_count == 2u &&
               plan->target_full && plan->rect.x == 0 && plan->rect.y == 0 &&
               plan->rect.width == WATCHY_TRANSITION_CANVAS_WIDTH &&
               plan->rect.height == WATCHY_TRANSITION_CANVAS_HEIGHT;
    }
    return plan->write_count == count;
}

static bool pixel_black(const uint8_t *frame, int32_t x, int32_t y) {
    const size_t offset = (size_t)y * WATCHY_TRANSITION_STRIDE + (size_t)x / 8u;
    const uint8_t bit = (uint8_t)(0x80u >> ((uint32_t)x & 7u));
    return (frame[offset] & bit) == 0u;
}

static void set_pixel(uint8_t *frame, int32_t x, int32_t y, bool black) {
    const size_t offset = (size_t)y * WATCHY_TRANSITION_STRIDE + (size_t)x / 8u;
    const uint8_t bit = (uint8_t)(0x80u >> ((uint32_t)x & 7u));

    if (black) {
        frame[offset] &= (uint8_t)~bit;
    } else {
        frame[offset] |= bit;
    }
}

static void copy_pixel(uint8_t *out, const uint8_t *input, int32_t x, int32_t y) {
    set_pixel(out, x, y, pixel_black(input, x, y));
}

static void copy_rect(uint8_t *out, const uint8_t *input, clipped_rect_t rect) {
    for (int32_t y = rect.top; y < rect.bottom; ++y) {
        for (int32_t x = rect.left; x < rect.right; ++x) {
            copy_pixel(out, input, x, y);
        }
    }
}

static void fill_rect(uint8_t *out, clipped_rect_t rect, bool black) {
    for (int32_t y = rect.top; y < rect.bottom; ++y) {
        for (int32_t x = rect.left; x < rect.right; ++x) {
            set_pixel(out, x, y, black);
        }
    }
}

static void compose_flash(const uint8_t *target, uint8_t *out, clipped_rect_t rect) {
    for (int32_t y = rect.top; y < rect.bottom; ++y) {
        for (int32_t x = rect.left; x < rect.right; ++x) {
            set_pixel(out, x, y, !pixel_black(target, x, y));
        }
    }
}

static void compose_wipe(uint8_t frame_index,
                         const uint8_t *target,
                         uint8_t *out,
                         clipped_rect_t rect) {
    const int32_t width = rect.right - rect.left;
    const int32_t progress = width * (int32_t)(frame_index + 1u) / 4;
    const int32_t edge = rect.left + progress;
    clipped_rect_t revealed = rect;
    clipped_rect_t leading = rect;

    revealed.right = edge;
    copy_rect(out, target, revealed);
    leading.left = edge - (progress < 2 ? progress : 2);
    leading.right = edge;
    fill_rect(out, leading, true);
}

static void compose_push(uint8_t frame_index,
                         watchy_transition_direction_t direction,
                         const uint8_t *source,
                         const uint8_t *target,
                         uint8_t *out,
                         clipped_rect_t rect) {
    const int32_t width = rect.right - rect.left;
    const int32_t height = rect.bottom - rect.top;
    const bool vertical = direction == WATCHY_TRANSITION_DIRECTION_UP ||
                          direction == WATCHY_TRANSITION_DIRECTION_DOWN;
    const int32_t length = vertical ? height : width;
    const int32_t progress = length * (int32_t)(frame_index + 1u) / 3;
    int32_t boundary;

    if (direction == WATCHY_TRANSITION_DIRECTION_NONE) {
        direction = WATCHY_TRANSITION_DIRECTION_RIGHT;
    }
    for (int32_t y = rect.top; y < rect.bottom; ++y) {
        for (int32_t x = rect.left; x < rect.right; ++x) {
            int32_t input_x = x;
            int32_t input_y = y;
            const uint8_t *input = source;

            switch (direction) {
            case WATCHY_TRANSITION_DIRECTION_LEFT:
                if (x < rect.right - progress) {
                    input = source;
                    input_x = x + progress;
                } else {
                    input = target;
                    input_x = x - (width - progress);
                }
                break;
            case WATCHY_TRANSITION_DIRECTION_RIGHT:
                if (x < rect.left + progress) {
                    input = target;
                    input_x = x + (width - progress);
                } else {
                    input = source;
                    input_x = x - progress;
                }
                break;
            case WATCHY_TRANSITION_DIRECTION_UP:
                if (y < rect.bottom - progress) {
                    input = source;
                    input_y = y + progress;
                } else {
                    input = target;
                    input_y = y - (height - progress);
                }
                break;
            case WATCHY_TRANSITION_DIRECTION_DOWN:
                if (y < rect.top + progress) {
                    input = target;
                    input_y = y + (height - progress);
                } else {
                    input = source;
                    input_y = y - progress;
                }
                break;
            case WATCHY_TRANSITION_DIRECTION_NONE:
                input = source;
                break;
            }
            set_pixel(out, x, y, pixel_black(input, input_x, input_y));
        }
    }

    if (vertical) {
        boundary = direction == WATCHY_TRANSITION_DIRECTION_DOWN ? rect.top + progress
                                                                 : rect.bottom - progress;
        if (boundary < rect.bottom) {
            fill_rect(out, (clipped_rect_t){rect.left, boundary, rect.right, boundary + 1}, true);
        }
    } else {
        boundary = direction == WATCHY_TRANSITION_DIRECTION_RIGHT ? rect.left + progress
                                                                  : rect.right - progress;
        if (boundary < rect.right) {
            fill_rect(out, (clipped_rect_t){boundary, rect.top, boundary + 1, rect.bottom}, true);
        }
    }
}

static void compose_dither(const uint8_t *target, uint8_t *out, clipped_rect_t rect) {
    for (int32_t y = rect.top; y < rect.bottom; ++y) {
        for (int32_t x = rect.left; x < rect.right; ++x) {
            if (((x + y) & 1) != 0) {
                copy_pixel(out, target, x, y);
            }
        }
    }
}

static void compose_grow(uint8_t frame_index,
                         const uint8_t *target,
                         uint8_t *out,
                         clipped_rect_t rect) {
    const int32_t width = rect.right - rect.left;
    const int32_t height = rect.bottom - rect.top;
    const int32_t grown_width = width * (int32_t)(frame_index + 1u) / 3;
    const int32_t grown_height = height * (int32_t)(frame_index + 1u) / 3;
    clipped_rect_t grown = {
        rect.left + (width - grown_width) / 2,
        rect.top + (height - grown_height) / 2,
        rect.left + (width - grown_width) / 2 + grown_width,
        rect.top + (height - grown_height) / 2 + grown_height,
    };

    if (grown_width == 0 || grown_height == 0) {
        return;
    }
    copy_rect(out, target, grown);
    for (int32_t x = grown.left; x < grown.right; ++x) {
        set_pixel(out, x, grown.top, true);
        set_pixel(out, x, grown.bottom - 1, true);
    }
    for (int32_t y = grown.top; y < grown.bottom; ++y) {
        set_pixel(out, grown.left, y, true);
        set_pixel(out, grown.right - 1, y, true);
    }
}

static void compose_split(uint8_t frame_index, uint8_t *out, clipped_rect_t rect) {
    const int32_t height = rect.bottom - rect.top;
    const int32_t depth = height * (int32_t)(frame_index + 1u) / 3;
    clipped_rect_t top = rect;
    clipped_rect_t bottom = rect;

    top.bottom = rect.top + depth;
    bottom.top = rect.bottom - depth;
    fill_rect(out, top, true);
    fill_rect(out, bottom, true);
}

static void compose_fill(uint8_t frame_index, uint8_t *out, clipped_rect_t rect) {
    const int32_t width = rect.right - rect.left;
    clipped_rect_t fill = rect;
    fill.right = rect.left + width * (int32_t)(frame_index + 1u) / 4;
    fill_rect(out, fill, true);
}

static void compose_shutter(uint8_t frame_index, uint8_t *out, clipped_rect_t rect) {
    const int32_t width = rect.right - rect.left;
    const int32_t height = rect.bottom - rect.top;
    const int32_t progress = width * (int32_t)(frame_index + 1u) / 4;

    for (int32_t band = 0; band < 4; ++band) {
        clipped_rect_t fill = {
            rect.left,
            rect.top + height * band / 4,
            rect.right,
            rect.top + height * (band + 1) / 4,
        };
        if ((band & 1) == 0) {
            fill.right = rect.left + progress;
        } else {
            fill.left = rect.right - progress;
        }
        fill_rect(out, fill, true);
    }
}

watchy_status_t watchy_transition_compose_frame(const watchy_transition_plan_t *plan,
                                                uint8_t frame_index,
                                                const uint8_t *source,
                                                const uint8_t *target,
                                                uint8_t *out,
                                                size_t size) {
    clipped_rect_t rect;

    if (source == NULL || target == NULL || out == NULL || out == source || out == target ||
        size < WATCHY_TRANSITION_FRAME_BYTES || !valid_plan(plan, &rect) ||
        frame_index >= plan->write_count) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (frame_index + 1u == plan->write_count) {
        memcpy(out, target, WATCHY_TRANSITION_FRAME_BYTES);
        return WATCHY_STATUS_OK;
    }
    if (plan->mandatory_clear) {
        for (size_t i = 0u; i < WATCHY_TRANSITION_FRAME_BYTES; ++i) {
            out[i] = (uint8_t)~target[i];
        }
        return WATCHY_STATUS_OK;
    }

    memcpy(out, source, WATCHY_TRANSITION_FRAME_BYTES);
    switch (plan->effect) {
    case WATCHY_TRANSITION_CUT:
        copy_rect(out, target, rect);
        break;
    case WATCHY_TRANSITION_FLASH:
        compose_flash(target, out, rect);
        break;
    case WATCHY_TRANSITION_WIPE:
        compose_wipe(frame_index, target, out, rect);
        break;
    case WATCHY_TRANSITION_PUSH:
        compose_push(frame_index, plan->direction, source, target, out, rect);
        break;
    case WATCHY_TRANSITION_DITHER:
        compose_dither(target, out, rect);
        break;
    case WATCHY_TRANSITION_GROW:
        compose_grow(frame_index, target, out, rect);
        break;
    case WATCHY_TRANSITION_ODOMETER:
        compose_push(frame_index,
                     plan->direction == WATCHY_TRANSITION_DIRECTION_DOWN
                         ? WATCHY_TRANSITION_DIRECTION_DOWN
                         : WATCHY_TRANSITION_DIRECTION_UP,
                     source, target, out, rect);
        break;
    case WATCHY_TRANSITION_SPLIT:
        compose_split(frame_index, out, rect);
        break;
    case WATCHY_TRANSITION_FILL:
        compose_fill(frame_index, out, rect);
        break;
    case WATCHY_TRANSITION_SHUTTER:
        compose_shutter(frame_index, out, rect);
        break;
    }
    return WATCHY_STATUS_OK;
}
