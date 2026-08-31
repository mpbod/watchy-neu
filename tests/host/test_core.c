#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "watchy/runtime.h"
#include "watchy/transition.h"
#include "watchy/watchdog.h"
#include "watchy/wpk.h"

#include "transition_golden.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static wpk_header_t *make_valid_header(uint8_t *bytes, size_t size) {
    wpk_header_t *header = (wpk_header_t *)bytes;
    memset(bytes, 0, size);
    memcpy(header->magic, WPK_MAGIC, sizeof(header->magic));
    header->format_version = WPK_FORMAT_VERSION;
    header->header_size = WPK_HEADER_SIZE;
    header->manifest_offset = WPK_HEADER_SIZE;
    header->manifest_size = 8;
    header->elf_offset = WPK_HEADER_SIZE + 8;
    header->elf_size = 16;
    header->assets_offset = WPK_HEADER_SIZE + 24;
    header->assets_size = 8;
    header->total_size = header->assets_offset + header->assets_size;
    return header;
}

static int test_bundle_rejects_bad_magic(void) {
    uint8_t bytes[WPK_HEADER_SIZE] = {0};
    wpk_view_t view = {0};
    CHECK(wpk_parse(bytes, sizeof(bytes), &view) == WPK_ERR_MAGIC);
    return 0;
}

static int test_bundle_accepts_well_formed_layout(void) {
    uint8_t bytes[WPK_HEADER_SIZE + 40] = {0};
    wpk_header_t *header = make_valid_header(bytes, sizeof(bytes));
    wpk_view_t view = {0};
    CHECK(wpk_parse(bytes, header->total_size, &view) == WPK_OK);
    CHECK(view.manifest_size == 8);
    CHECK(view.elf_size == 16);
    CHECK(view.assets_size == 8);
    return 0;
}

static int test_bundle_rejects_truncated_blob(void) {
    uint8_t bytes[WPK_HEADER_SIZE + 40] = {0};
    wpk_header_t *header = make_valid_header(bytes, sizeof(bytes));
    wpk_view_t view = {0};
    CHECK(wpk_parse(bytes, header->total_size - 1u, &view) == WPK_ERR_TRUNCATED);
    return 0;
}

static int test_bundle_rejects_overlapping_sections(void) {
    uint8_t bytes[WPK_HEADER_SIZE + 40] = {0};
    wpk_header_t *header = make_valid_header(bytes, sizeof(bytes));
    header->manifest_offset = WPK_HEADER_SIZE;
    header->manifest_size = 16;
    header->elf_offset = WPK_HEADER_SIZE + 8;
    wpk_view_t view = {0};
    CHECK(wpk_parse(bytes, header->total_size, &view) == WPK_ERR_LAYOUT);
    return 0;
}

static int test_bundle_rejects_out_of_order_sections(void) {
    uint8_t bytes[WPK_HEADER_SIZE + 40] = {0};
    wpk_header_t *header = make_valid_header(bytes, sizeof(bytes));
    header->elf_offset = WPK_HEADER_SIZE + 16;
    header->assets_offset = WPK_HEADER_SIZE + 8;
    wpk_view_t view = {0};
    CHECK(wpk_parse(bytes, header->total_size, &view) == WPK_ERR_LAYOUT);
    return 0;
}

static int test_bundle_rejects_section_offset_overflow(void) {
    uint8_t bytes[WPK_HEADER_SIZE + 40] = {0};
    wpk_header_t *header = make_valid_header(bytes, sizeof(bytes));
    header->assets_offset = UINT32_MAX - 3;
    header->assets_size = 8;
    wpk_view_t view = {0};
    CHECK(wpk_parse(bytes, header->total_size, &view) == WPK_ERR_LAYOUT);
    return 0;
}

static int test_bundle_rejects_gap_before_manifest(void) {
    uint8_t bytes[WPK_HEADER_SIZE + 40] = {0};
    wpk_header_t *header = make_valid_header(bytes, sizeof(bytes));
    header->manifest_offset += 4;
    header->elf_offset = header->manifest_offset + header->manifest_size;
    header->assets_offset = header->elf_offset + header->elf_size;
    header->total_size = header->assets_offset + header->assets_size;
    wpk_view_t view = {0};
    CHECK(wpk_parse(bytes, header->total_size, &view) == WPK_ERR_LAYOUT);
    return 0;
}

static int test_bundle_rejects_gap_between_manifest_and_elf(void) {
    uint8_t bytes[WPK_HEADER_SIZE + 40] = {0};
    wpk_header_t *header = make_valid_header(bytes, sizeof(bytes));
    header->elf_offset += 4;
    header->assets_offset = header->elf_offset + header->elf_size;
    header->total_size = header->assets_offset + header->assets_size;
    wpk_view_t view = {0};
    CHECK(wpk_parse(bytes, header->total_size, &view) == WPK_ERR_LAYOUT);
    return 0;
}

static int test_bundle_rejects_gap_between_elf_and_assets(void) {
    uint8_t bytes[WPK_HEADER_SIZE + 40] = {0};
    wpk_header_t *header = make_valid_header(bytes, sizeof(bytes));
    header->assets_offset += 4;
    header->total_size = header->assets_offset + header->assets_size;
    wpk_view_t view = {0};
    CHECK(wpk_parse(bytes, header->total_size, &view) == WPK_ERR_LAYOUT);
    return 0;
}

static int test_bundle_rejects_trailing_bytes(void) {
    uint8_t bytes[WPK_HEADER_SIZE + 40] = {0};
    wpk_header_t *header = make_valid_header(bytes, sizeof(bytes));
    header->total_size += 4;
    wpk_view_t view = {0};
    CHECK(wpk_parse(bytes, header->total_size, &view) == WPK_ERR_LAYOUT);
    return 0;
}

static int test_bundle_rejects_oversized_blob_for_exact_package(void) {
    uint8_t bytes[WPK_HEADER_SIZE + 40] = {0};
    make_valid_header(bytes, sizeof(bytes));
    wpk_view_t view = {0};
    CHECK(wpk_parse(bytes, sizeof(bytes), &view) == WPK_ERR_LAYOUT);
    return 0;
}

static int test_abi_negotiation(void) {
    CHECK(watchy_abi_compatible(1, 0, 1, 0));
    CHECK(watchy_abi_compatible(1, 2, 1, 4));
    CHECK(!watchy_abi_compatible(1, 1, 1, 0));
    CHECK(!watchy_abi_compatible(2, 0, 1, 9));
    return 0;
}

static int test_runtime_lifecycle(void) {
    watchy_runtime_t runtime;
    watchy_runtime_reset(&runtime);
    CHECK(runtime.state == WATCHY_RUNTIME_EMPTY);
    CHECK(watchy_runtime_transition(&runtime, WATCHY_RUNTIME_LOADED) == WATCHY_STATUS_OK);
    CHECK(watchy_runtime_transition(&runtime, WATCHY_RUNTIME_EMPTY) == WATCHY_STATUS_INVALID_STATE);
    CHECK(watchy_runtime_transition(&runtime, WATCHY_RUNTIME_STARTED) == WATCHY_STATUS_OK);
    CHECK(watchy_runtime_transition(&runtime, WATCHY_RUNTIME_LOADED) == WATCHY_STATUS_INVALID_STATE);
    CHECK(watchy_runtime_transition(&runtime, WATCHY_RUNTIME_STOPPED) == WATCHY_STATUS_OK);
    CHECK(watchy_runtime_transition(&runtime, WATCHY_RUNTIME_EMPTY) == WATCHY_STATUS_OK);
    CHECK(watchy_runtime_transition(&runtime, WATCHY_RUNTIME_STOPPED) == WATCHY_STATUS_INVALID_STATE);
    CHECK(watchy_runtime_transition(&runtime, WATCHY_RUNTIME_STARTED) == WATCHY_STATUS_INVALID_STATE);
    return 0;
}

static int test_refresh_policy_promotes_after_partial_limit(void) {
    watchy_refresh_policy_t policy;
    watchy_refresh_policy_reset(&policy, 3);
    CHECK(watchy_refresh_decide(&policy, WATCHY_REFRESH_PARTIAL) == WATCHY_REFRESH_PARTIAL);
    CHECK(watchy_refresh_decide(&policy, WATCHY_REFRESH_PARTIAL) == WATCHY_REFRESH_PARTIAL);
    CHECK(watchy_refresh_decide(&policy, WATCHY_REFRESH_PARTIAL) == WATCHY_REFRESH_FULL);
    CHECK(policy.partial_count == 0);
    return 0;
}

static int test_refresh_policy_resets_after_explicit_full(void) {
    watchy_refresh_policy_t policy;
    watchy_refresh_policy_reset(&policy, 3);
    CHECK(watchy_refresh_decide(&policy, WATCHY_REFRESH_PARTIAL) == WATCHY_REFRESH_PARTIAL);
    CHECK(policy.partial_count == 1);
    CHECK(watchy_refresh_decide(&policy, WATCHY_REFRESH_FULL) == WATCHY_REFRESH_FULL);
    CHECK(policy.partial_count == 0);
    CHECK(watchy_refresh_decide(&policy, WATCHY_REFRESH_PARTIAL) == WATCHY_REFRESH_PARTIAL);
    CHECK(policy.partial_count == 1);
    return 0;
}

static watchy_transition_request_v1_t valid_transition_request(void) {
    return (watchy_transition_request_v1_t){
        .size = sizeof(watchy_transition_request_v1_t),
        .effect = WATCHY_TRANSITION_WIPE,
        .direction = WATCHY_TRANSITION_DIRECTION_RIGHT,
    };
}

static bool transition_fixture_black(uint16_t x, uint16_t y, bool target) {
    if (target) {
        return (((uint32_t)x * 3u + (uint32_t)y * 5u) % 11u) < 5u;
    }
    return ((((uint16_t)(x / 5u)) + ((uint16_t)(y / 7u))) & 1u) != 0u;
}

static void transition_fixture_set(uint8_t *frame, uint16_t x, uint16_t y, bool black) {
    const size_t offset = (size_t)y * 25u + (size_t)x / 8u;
    const uint8_t bit = (uint8_t)(0x80u >> (x & 7u));

    if (black) {
        frame[offset] &= (uint8_t)~bit;
    } else {
        frame[offset] |= bit;
    }
}

static void make_transition_fixture(uint8_t *source, uint8_t *target) {
    memset(source, 0xff, WATCHY_TRANSITION_FRAME_BYTES);
    memset(target, 0xff, WATCHY_TRANSITION_FRAME_BYTES);
    for (uint16_t y = 0u; y < WATCHY_TRANSITION_CANVAS_HEIGHT; ++y) {
        for (uint16_t x = 0u; x < WATCHY_TRANSITION_CANVAS_WIDTH; ++x) {
            transition_fixture_set(source, x, y, transition_fixture_black(x, y, false));
            transition_fixture_set(target, x, y, transition_fixture_black(x, y, true));
        }
    }
}

static bool transition_pixel_black(const uint8_t *frame, uint16_t x, uint16_t y) {
    const size_t offset = (size_t)y * 25u + (size_t)x / 8u;
    const uint8_t bit = (uint8_t)(0x80u >> (x & 7u));
    return (frame[offset] & bit) == 0u;
}

static uint32_t transition_fnv1a(const uint8_t *frame, size_t size) {
    uint32_t hash = UINT32_C(2166136261);

    for (size_t i = 0u; i < size; ++i) {
        hash ^= frame[i];
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static int test_transition_rejects_malformed_requests(void) {
    struct invalid_request_case {
        watchy_transition_request_v1_t request;
    } cases[] = {
        {.request = {.size = sizeof(watchy_transition_request_v1_t) - 1u,
                     .effect = WATCHY_TRANSITION_WIPE,
                     .direction = WATCHY_TRANSITION_DIRECTION_RIGHT}},
        {.request = {.size = sizeof(watchy_transition_request_v1_t),
                     .effect = WATCHY_TRANSITION_WIPE,
                     .direction = WATCHY_TRANSITION_DIRECTION_RIGHT,
                     .reserved = {1u, 0u}}},
        {.request = {.size = sizeof(watchy_transition_request_v1_t),
                     .effect = WATCHY_TRANSITION_WIPE,
                     .direction = WATCHY_TRANSITION_DIRECTION_RIGHT,
                     .reserved = {0u, 1u}}},
        {.request = {.size = sizeof(watchy_transition_request_v1_t),
                     .effect = WATCHY_TRANSITION_WIPE,
                     .direction = WATCHY_TRANSITION_DIRECTION_RIGHT,
                     .flags = UINT32_C(4)}},
        {.request = {.size = sizeof(watchy_transition_request_v1_t),
                     .effect = WATCHY_TRANSITION_WIPE,
                     .direction = (watchy_transition_direction_t)5}},
        {.request = {.size = sizeof(watchy_transition_request_v1_t),
                     .effect = (watchy_transition_effect_t)10,
                     .direction = WATCHY_TRANSITION_DIRECTION_RIGHT}},
        {.request = {.size = sizeof(watchy_transition_request_v1_t),
                     .effect = WATCHY_TRANSITION_WIPE,
                     .direction = WATCHY_TRANSITION_DIRECTION_RIGHT,
                     .flags = WATCHY_TRANSITION_HAS_RECT,
                     .rect = {0, 0, 0, 1}}},
        {.request = {.size = sizeof(watchy_transition_request_v1_t),
                     .effect = WATCHY_TRANSITION_WIPE,
                     .direction = WATCHY_TRANSITION_DIRECTION_RIGHT,
                     .flags = WATCHY_TRANSITION_HAS_RECT,
                     .rect = {INT16_MAX, 0, INT16_MAX, 1}}},
        {.request = {.size = sizeof(watchy_transition_request_v1_t),
                     .effect = WATCHY_TRANSITION_ODOMETER,
                     .direction = WATCHY_TRANSITION_DIRECTION_UP}},
    };

    CHECK(watchy_transition_validate(NULL) == WATCHY_STATUS_INVALID_ARGUMENT);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        CHECK(watchy_transition_validate(&cases[i].request) == WATCHY_STATUS_INVALID_ARGUMENT);
    }
    return 0;
}

static int test_transition_normalizes_omitted_rectangle(void) {
    watchy_transition_request_v1_t request = valid_transition_request();
    watchy_transition_policy_context_t context = {
        .level = WATCHY_TRANSITION_LEVEL_FULL,
        .attended = true,
        .battery_mv = 3900u,
        .source_valid = true,
    };
    watchy_transition_plan_t plan;

    CHECK(watchy_transition_plan(&request, &context, &plan) == WATCHY_STATUS_OK);
    CHECK(plan.rect.x == 0 && plan.rect.y == 0);
    CHECK(plan.rect.width == 200 && plan.rect.height == 200);
    CHECK(plan.effect == WATCHY_TRANSITION_WIPE);
    CHECK(plan.direction == WATCHY_TRANSITION_DIRECTION_RIGHT);
    CHECK(plan.write_count == 3u);
    CHECK(!plan.target_full && !plan.mandatory_clear);
    return 0;
}

static int test_transition_uses_cut_when_no_request_is_supplied(void) {
    watchy_transition_policy_context_t context = {
        .level = WATCHY_TRANSITION_LEVEL_FULL,
        .attended = true,
        .battery_mv = 3900u,
        .source_valid = true,
    };
    watchy_transition_plan_t plan;

    CHECK(watchy_transition_plan(NULL, &context, &plan) == WATCHY_STATUS_OK);
    CHECK(plan.effect == WATCHY_TRANSITION_CUT);
    CHECK(plan.direction == WATCHY_TRANSITION_DIRECTION_NONE);
    CHECK(plan.rect.x == 0 && plan.rect.y == 0);
    CHECK(plan.rect.width == 200 && plan.rect.height == 200);
    CHECK(plan.write_count == 1u);
    CHECK(!plan.target_full && !plan.mandatory_clear);
    return 0;
}

static int test_transition_rejects_out_of_range_policy_levels(void) {
    static const watchy_transition_level_t levels[] = {
        (watchy_transition_level_t)-1,
        (watchy_transition_level_t)3,
    };
    watchy_transition_request_v1_t request = valid_transition_request();
    watchy_transition_policy_context_t context = {
        .attended = true,
        .battery_mv = 3900u,
        .source_valid = true,
    };
    watchy_transition_plan_t plan;

    for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i) {
        context.level = levels[i];
        CHECK(watchy_transition_plan(&request, &context, &plan) == WATCHY_STATUS_INVALID_ARGUMENT);
    }
    return 0;
}

static int test_transition_assigns_bounded_write_counts(void) {
    static const struct transition_count_case {
        watchy_transition_effect_t effect;
        uint8_t write_count;
    } cases[] = {
        {WATCHY_TRANSITION_CUT, 1u},
        {WATCHY_TRANSITION_FLASH, 2u},
        {WATCHY_TRANSITION_WIPE, 3u},
        {WATCHY_TRANSITION_PUSH, 2u},
        {WATCHY_TRANSITION_DITHER, 2u},
        {WATCHY_TRANSITION_GROW, 2u},
        {WATCHY_TRANSITION_ODOMETER, 2u},
        {WATCHY_TRANSITION_SPLIT, 2u},
        {WATCHY_TRANSITION_FILL, 3u},
        {WATCHY_TRANSITION_SHUTTER, 3u},
    };
    watchy_transition_policy_context_t context = {
        .level = WATCHY_TRANSITION_LEVEL_FULL,
        .attended = true,
        .battery_mv = 3900u,
        .source_valid = true,
    };
    watchy_transition_plan_t plan;

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        watchy_transition_request_v1_t request = valid_transition_request();
        request.effect = cases[i].effect;
        if (request.effect == WATCHY_TRANSITION_ODOMETER) {
            request.flags = WATCHY_TRANSITION_HAS_RECT;
            request.rect = (watchy_transition_rect_t){20, 20, 40, 40};
        }
        CHECK(watchy_transition_plan(&request, &context, &plan) == WATCHY_STATUS_OK);
        CHECK(plan.effect == cases[i].effect);
        CHECK(plan.write_count == cases[i].write_count);
    }
    return 0;
}

static int test_transition_respects_full_target_preference(void) {
    watchy_transition_request_v1_t request = valid_transition_request();
    watchy_transition_policy_context_t context = {
        .level = WATCHY_TRANSITION_LEVEL_FULL,
        .attended = true,
        .battery_mv = 3900u,
        .source_valid = true,
    };
    watchy_transition_plan_t plan;

    request.flags = WATCHY_TRANSITION_PREFER_FULL;
    CHECK(watchy_transition_plan(&request, &context, &plan) == WATCHY_STATUS_OK);
    CHECK(plan.target_full);
    return 0;
}

static int test_transition_policy_matrix_downgrades_optional_motion(void) {
    watchy_transition_request_v1_t request = valid_transition_request();
    watchy_transition_policy_context_t context = {
        .level = WATCHY_TRANSITION_LEVEL_FULL,
        .attended = true,
        .battery_mv = 3900u,
        .source_valid = true,
    };
    watchy_transition_plan_t plan;

    CHECK(watchy_transition_plan(&request, &context, &plan) == WATCHY_STATUS_OK);
    CHECK(plan.effect == WATCHY_TRANSITION_WIPE && plan.write_count == 3u);
    context.level = WATCHY_TRANSITION_LEVEL_REDUCED;
    CHECK(watchy_transition_plan(&request, &context, &plan) == WATCHY_STATUS_OK);
    CHECK(plan.effect == WATCHY_TRANSITION_FLASH && plan.write_count == 2u);
    context.level = WATCHY_TRANSITION_LEVEL_OFF;
    CHECK(watchy_transition_plan(&request, &context, &plan) == WATCHY_STATUS_OK);
    CHECK(plan.effect == WATCHY_TRANSITION_CUT && plan.write_count == 1u);

    context.level = WATCHY_TRANSITION_LEVEL_FULL;
    context.battery_mv = 3549u;
    CHECK(watchy_transition_plan(&request, &context, &plan) == WATCHY_STATUS_OK);
    CHECK(plan.effect == WATCHY_TRANSITION_CUT && plan.write_count == 1u);
    context.battery_mv = 3900u;
    context.attended = false;
    CHECK(watchy_transition_plan(&request, &context, &plan) == WATCHY_STATUS_OK);
    CHECK(plan.effect == WATCHY_TRANSITION_CUT && plan.write_count == 1u);
    context.attended = true;
    context.safe_mode = true;
    CHECK(watchy_transition_plan(&request, &context, &plan) == WATCHY_STATUS_OK);
    CHECK(plan.effect == WATCHY_TRANSITION_CUT && plan.write_count == 1u);
    context.safe_mode = false;
    context.source_valid = false;
    CHECK(watchy_transition_plan(&request, &context, &plan) == WATCHY_STATUS_OK);
    CHECK(plan.effect == WATCHY_TRANSITION_CUT && plan.write_count == 1u);
    return 0;
}

static int test_transition_clear_overrides_optional_effect(void) {
    watchy_transition_request_v1_t request = valid_transition_request();
    watchy_transition_policy_context_t context = {
        .level = WATCHY_TRANSITION_LEVEL_OFF,
        .attended = false,
        .safe_mode = true,
        .source_valid = false,
        .clear_required = true,
        .battery_mv = 3549u,
    };
    watchy_transition_plan_t plan;

    CHECK(watchy_transition_plan(&request, &context, &plan) == WATCHY_STATUS_OK);
    CHECK(plan.effect == WATCHY_TRANSITION_CUT);
    CHECK(plan.direction == WATCHY_TRANSITION_DIRECTION_NONE);
    CHECK(plan.rect.x == 0 && plan.rect.y == 0);
    CHECK(plan.rect.width == 200 && plan.rect.height == 200);
    CHECK(plan.write_count == 2u);
    CHECK(plan.target_full && plan.mandatory_clear);
    return 0;
}

static int test_transition_clear_overrides_invalid_optional_request(void) {
    watchy_transition_request_v1_t request = valid_transition_request();
    watchy_transition_policy_context_t context = {
        .level = WATCHY_TRANSITION_LEVEL_FULL,
        .attended = true,
        .clear_required = true,
        .battery_mv = 3900u,
        .source_valid = true,
    };
    watchy_transition_plan_t plan;

    request.flags = UINT32_C(4);
    CHECK(watchy_transition_validate(&request) == WATCHY_STATUS_INVALID_ARGUMENT);
    CHECK(watchy_transition_plan(&request, &context, &plan) == WATCHY_STATUS_OK);
    CHECK(plan.effect == WATCHY_TRANSITION_CUT);
    CHECK(plan.direction == WATCHY_TRANSITION_DIRECTION_NONE);
    CHECK(plan.write_count == 2u);
    CHECK(plan.target_full && plan.mandatory_clear);
    return 0;
}

static int test_transition_compositor_rejects_invalid_calls(void) {
    uint8_t source[WATCHY_TRANSITION_FRAME_BYTES];
    uint8_t target[WATCHY_TRANSITION_FRAME_BYTES];
    uint8_t scratch[WATCHY_TRANSITION_FRAME_BYTES];
    watchy_transition_plan_t plan = {
        .effect = WATCHY_TRANSITION_CUT,
        .direction = WATCHY_TRANSITION_DIRECTION_NONE,
        .rect = {0, 0, 200, 200},
        .write_count = 1u,
    };

    make_transition_fixture(source, target);
    CHECK(watchy_transition_compose_frame(NULL, 0u, source, target, scratch, sizeof(scratch)) ==
          WATCHY_STATUS_INVALID_ARGUMENT);
    CHECK(watchy_transition_compose_frame(&plan, 0u, NULL, target, scratch, sizeof(scratch)) ==
          WATCHY_STATUS_INVALID_ARGUMENT);
    CHECK(watchy_transition_compose_frame(&plan, 0u, source, NULL, scratch, sizeof(scratch)) ==
          WATCHY_STATUS_INVALID_ARGUMENT);
    CHECK(watchy_transition_compose_frame(&plan, 0u, source, target, NULL, sizeof(scratch)) ==
          WATCHY_STATUS_INVALID_ARGUMENT);
    CHECK(watchy_transition_compose_frame(&plan, 1u, source, target, scratch, sizeof(scratch)) ==
          WATCHY_STATUS_INVALID_ARGUMENT);
    CHECK(watchy_transition_compose_frame(&plan, 0u, source, target, scratch,
                                          WATCHY_TRANSITION_FRAME_BYTES - 1u) ==
          WATCHY_STATUS_INVALID_ARGUMENT);
    CHECK(watchy_transition_compose_frame(&plan, 0u, source, target, source, sizeof(source)) ==
          WATCHY_STATUS_INVALID_ARGUMENT);
    CHECK(watchy_transition_compose_frame(&plan, 0u, source, target, target, sizeof(target)) ==
          WATCHY_STATUS_INVALID_ARGUMENT);

    plan.effect = (watchy_transition_effect_t)10;
    CHECK(watchy_transition_compose_frame(&plan, 0u, source, target, scratch, sizeof(scratch)) ==
          WATCHY_STATUS_INVALID_ARGUMENT);
    plan.effect = WATCHY_TRANSITION_CUT;
    plan.rect = (watchy_transition_rect_t){200, 0, 1, 1};
    CHECK(watchy_transition_compose_frame(&plan, 0u, source, target, scratch, sizeof(scratch)) ==
          WATCHY_STATUS_INVALID_ARGUMENT);
    return 0;
}

static int test_transition_compositor_clips_intersecting_rectangles(void) {
    uint8_t source[WATCHY_TRANSITION_FRAME_BYTES];
    uint8_t target[WATCHY_TRANSITION_FRAME_BYTES];
    uint8_t scratch[WATCHY_TRANSITION_FRAME_BYTES];
    watchy_transition_plan_t plan = {
        .effect = WATCHY_TRANSITION_FLASH,
        .direction = WATCHY_TRANSITION_DIRECTION_NONE,
        .rect = {-4, -3, 9, 8},
        .write_count = 2u,
    };

    make_transition_fixture(source, target);
    CHECK(watchy_transition_compose_frame(&plan, 0u, source, target, scratch, sizeof(scratch)) ==
          WATCHY_STATUS_OK);
    CHECK(transition_pixel_black(scratch, 0u, 0u) != transition_pixel_black(target, 0u, 0u));
    CHECK(transition_pixel_black(scratch, 4u, 4u) != transition_pixel_black(target, 4u, 4u));
    CHECK(transition_pixel_black(scratch, 5u, 4u) == transition_pixel_black(source, 5u, 4u));
    CHECK(watchy_transition_compose_frame(&plan, 1u, source, target, scratch, sizeof(scratch)) ==
          WATCHY_STATUS_OK);
    CHECK(memcmp(scratch, target, sizeof(target)) == 0);

    plan.effect = WATCHY_TRANSITION_GROW;
    plan.rect = (watchy_transition_rect_t){0, 0, 3, 1};
    plan.write_count = 2u;
    CHECK(watchy_transition_compose_frame(&plan, 0u, source, target, scratch, sizeof(scratch)) ==
          WATCHY_STATUS_OK);
    CHECK(memcmp(scratch, source, sizeof(source)) == 0);
    return 0;
}

static int test_transition_effect_edges_and_checkerboard(void) {
    uint8_t source[WATCHY_TRANSITION_FRAME_BYTES];
    uint8_t target[WATCHY_TRANSITION_FRAME_BYTES];
    uint8_t scratch[WATCHY_TRANSITION_FRAME_BYTES];
    watchy_transition_plan_t plan = {
        .direction = WATCHY_TRANSITION_DIRECTION_RIGHT,
        .rect = {80, 80, 40, 36},
    };

    make_transition_fixture(source, target);

    plan.effect = WATCHY_TRANSITION_FLASH;
    plan.write_count = 2u;
    CHECK(watchy_transition_compose_frame(&plan, 0u, source, target, scratch, sizeof(scratch)) ==
          WATCHY_STATUS_OK);
    CHECK(transition_pixel_black(scratch, 84u, 84u) != transition_pixel_black(target, 84u, 84u));
    CHECK(transition_pixel_black(scratch, 79u, 84u) == transition_pixel_black(source, 79u, 84u));

    plan.effect = WATCHY_TRANSITION_WIPE;
    plan.write_count = 3u;
    CHECK(watchy_transition_compose_frame(&plan, 0u, source, target, scratch, sizeof(scratch)) ==
          WATCHY_STATUS_OK);
    CHECK(transition_pixel_black(scratch, 84u, 84u) == transition_pixel_black(target, 84u, 84u));
    CHECK(transition_pixel_black(scratch, 88u, 85u));
    CHECK(transition_pixel_black(scratch, 89u, 85u));
    CHECK(transition_pixel_black(scratch, 91u, 84u) == transition_pixel_black(source, 91u, 84u));

    plan.effect = WATCHY_TRANSITION_PUSH;
    plan.write_count = 2u;
    CHECK(watchy_transition_compose_frame(&plan, 0u, source, target, scratch, sizeof(scratch)) ==
          WATCHY_STATUS_OK);
    CHECK(transition_pixel_black(scratch, 83u, 84u) == transition_pixel_black(target, 110u, 84u));
    CHECK(transition_pixel_black(scratch, 93u, 84u));
    CHECK(transition_pixel_black(scratch, 95u, 84u) == transition_pixel_black(source, 82u, 84u));

    plan.effect = WATCHY_TRANSITION_DITHER;
    plan.write_count = 2u;
    CHECK(watchy_transition_compose_frame(&plan, 0u, source, target, scratch, sizeof(scratch)) ==
          WATCHY_STATUS_OK);
    CHECK(transition_pixel_black(scratch, 84u, 84u) == transition_pixel_black(source, 84u, 84u));
    CHECK(transition_pixel_black(scratch, 87u, 84u) == transition_pixel_black(target, 87u, 84u));

    plan.effect = WATCHY_TRANSITION_GROW;
    plan.write_count = 2u;
    CHECK(watchy_transition_compose_frame(&plan, 0u, source, target, scratch, sizeof(scratch)) ==
          WATCHY_STATUS_OK);
    CHECK(transition_pixel_black(scratch, 90u, 98u) == transition_pixel_black(source, 90u, 98u));
    CHECK(transition_pixel_black(scratch, 93u, 98u));
    CHECK(transition_pixel_black(scratch, 95u, 98u) == transition_pixel_black(target, 95u, 98u));

    plan.effect = WATCHY_TRANSITION_ODOMETER;
    plan.direction = WATCHY_TRANSITION_DIRECTION_UP;
    plan.write_count = 2u;
    CHECK(watchy_transition_compose_frame(&plan, 0u, source, target, scratch, sizeof(scratch)) ==
          WATCHY_STATUS_OK);
    CHECK(transition_pixel_black(scratch, 84u, 84u) == transition_pixel_black(source, 84u, 96u));
    CHECK(transition_pixel_black(scratch, 85u, 104u));
    CHECK(transition_pixel_black(scratch, 89u, 108u) == transition_pixel_black(target, 89u, 84u));

    plan.effect = WATCHY_TRANSITION_SPLIT;
    plan.direction = WATCHY_TRANSITION_DIRECTION_NONE;
    plan.write_count = 2u;
    CHECK(watchy_transition_compose_frame(&plan, 0u, source, target, scratch, sizeof(scratch)) ==
          WATCHY_STATUS_OK);
    CHECK(transition_pixel_black(scratch, 84u, 90u));
    CHECK(transition_pixel_black(scratch, 85u, 92u) == transition_pixel_black(source, 85u, 92u));
    CHECK(transition_pixel_black(scratch, 84u, 104u));

    plan.effect = WATCHY_TRANSITION_FILL;
    plan.write_count = 3u;
    CHECK(watchy_transition_compose_frame(&plan, 0u, source, target, scratch, sizeof(scratch)) ==
          WATCHY_STATUS_OK);
    CHECK(transition_pixel_black(scratch, 84u, 84u));
    CHECK(transition_pixel_black(scratch, 91u, 84u) == transition_pixel_black(source, 91u, 84u));

    plan.effect = WATCHY_TRANSITION_SHUTTER;
    plan.write_count = 3u;
    CHECK(watchy_transition_compose_frame(&plan, 0u, source, target, scratch, sizeof(scratch)) ==
          WATCHY_STATUS_OK);
    CHECK(transition_pixel_black(scratch, 84u, 84u));
    CHECK(transition_pixel_black(scratch, 91u, 84u) == transition_pixel_black(source, 91u, 84u));
    CHECK(transition_pixel_black(scratch, 115u, 93u));
    CHECK(transition_pixel_black(scratch, 109u, 93u) == transition_pixel_black(source, 109u, 93u));
    return 0;
}

static int test_transition_effects_match_frozen_frames(void) {
    static const uint8_t write_counts[] = {1u, 2u, 3u, 2u, 2u, 2u, 2u, 2u, 3u, 3u};
    static const watchy_transition_direction_t directions[] = {
        WATCHY_TRANSITION_DIRECTION_NONE, WATCHY_TRANSITION_DIRECTION_NONE,
        WATCHY_TRANSITION_DIRECTION_RIGHT, WATCHY_TRANSITION_DIRECTION_RIGHT,
        WATCHY_TRANSITION_DIRECTION_NONE, WATCHY_TRANSITION_DIRECTION_NONE,
        WATCHY_TRANSITION_DIRECTION_UP, WATCHY_TRANSITION_DIRECTION_NONE,
        WATCHY_TRANSITION_DIRECTION_NONE, WATCHY_TRANSITION_DIRECTION_NONE,
    };
    uint8_t source[WATCHY_TRANSITION_FRAME_BYTES];
    uint8_t target[WATCHY_TRANSITION_FRAME_BYTES];
    uint8_t scratch[WATCHY_TRANSITION_FRAME_BYTES];

    make_transition_fixture(source, target);
    for (uint8_t effect = WATCHY_TRANSITION_CUT; effect <= WATCHY_TRANSITION_SHUTTER; ++effect) {
        watchy_transition_plan_t plan = {
            .effect = (watchy_transition_effect_t)effect,
            .direction = directions[effect],
            .rect = {80, 80, 40, 36},
            .write_count = write_counts[effect],
        };

        for (uint8_t frame = 0u; frame < plan.write_count; ++frame) {
            CHECK(watchy_transition_compose_frame(&plan, frame, source, target, scratch,
                                                  sizeof(scratch)) == WATCHY_STATUS_OK);
            CHECK(transition_fnv1a(scratch, sizeof(scratch)) ==
                  watchy_transition_golden_hashes[effect][frame]);
        }
        CHECK(memcmp(scratch, target, sizeof(target)) == 0);
    }
    return 0;
}

static int test_transition_mandatory_clear_inverts_then_targets(void) {
    uint8_t source[WATCHY_TRANSITION_FRAME_BYTES];
    uint8_t target[WATCHY_TRANSITION_FRAME_BYTES];
    uint8_t scratch[WATCHY_TRANSITION_FRAME_BYTES];
    watchy_transition_plan_t plan = {
        .effect = WATCHY_TRANSITION_CUT,
        .direction = WATCHY_TRANSITION_DIRECTION_NONE,
        .rect = {0, 0, 200, 200},
        .write_count = 2u,
        .target_full = true,
        .mandatory_clear = true,
    };

    make_transition_fixture(source, target);
    CHECK(watchy_transition_compose_frame(&plan, 0u, source, target, scratch, sizeof(scratch)) ==
          WATCHY_STATUS_OK);
    for (size_t i = 0u; i < sizeof(scratch); ++i) {
        CHECK(scratch[i] == (uint8_t)~target[i]);
    }
    CHECK(watchy_transition_compose_frame(&plan, 1u, source, target, scratch, sizeof(scratch)) ==
          WATCHY_STATUS_OK);
    CHECK(memcmp(scratch, target, sizeof(target)) == 0);
    return 0;
}

typedef struct {
    const uint8_t *target;
    size_t fail_at;
    size_t cancel_at;
    size_t write_count;
    size_t feed_count;
    size_t cancel_count;
    watchy_refresh_mode_t modes[5];
    bool frames_are_target[5];
} fake_writer_t;

static watchy_status_t fake_write(void *context,
                                  const uint8_t *frame,
                                  watchy_refresh_mode_t mode) {
    fake_writer_t *writer = context;

    if (writer->write_count == writer->fail_at) {
        ++writer->write_count;
        return WATCHY_STATUS_INVALID_STATE;
    }
    writer->modes[writer->write_count] = mode;
    writer->frames_are_target[writer->write_count] =
        memcmp(frame, writer->target, WATCHY_TRANSITION_FRAME_BYTES) == 0;
    ++writer->write_count;
    return WATCHY_STATUS_OK;
}

static bool fake_cancel(void *context) {
    fake_writer_t *writer = context;
    const bool cancelled = writer->cancel_count == writer->cancel_at;

    ++writer->cancel_count;
    return cancelled;
}

static void fake_feed(void *context) {
    fake_writer_t *writer = context;

    ++writer->feed_count;
}

static watchy_transition_plan_t test_transition_wipe_plan(void) {
    return (watchy_transition_plan_t){
        .effect = WATCHY_TRANSITION_WIPE,
        .direction = WATCHY_TRANSITION_DIRECTION_RIGHT,
        .rect = {0, 0, 200, 200},
        .write_count = 3u,
    };
}

static int test_transition_executor_writes_bounded_sequence_and_target(void) {
    uint8_t source[WATCHY_TRANSITION_FRAME_BYTES];
    uint8_t target[WATCHY_TRANSITION_FRAME_BYTES];
    uint8_t scratch[WATCHY_TRANSITION_FRAME_BYTES];
    watchy_transition_plan_t plan = test_transition_wipe_plan();
    fake_writer_t writer = {.target = target, .fail_at = SIZE_MAX, .cancel_at = SIZE_MAX};
    watchy_transition_result_t result;

    plan.target_full = true;
    make_transition_fixture(source, target);
    CHECK(watchy_transition_execute(&plan, source, target, scratch, sizeof(scratch), fake_write,
                                    fake_cancel, fake_feed, &writer, &result) == WATCHY_STATUS_OK);
    CHECK(writer.write_count == plan.write_count);
    CHECK(writer.feed_count == plan.write_count);
    CHECK(writer.cancel_count == plan.write_count - 1u);
    CHECK(writer.modes[0] == WATCHY_REFRESH_PARTIAL);
    CHECK(writer.modes[1] == WATCHY_REFRESH_PARTIAL);
    CHECK(writer.modes[2] == WATCHY_REFRESH_FULL);
    CHECK(!writer.frames_are_target[0]);
    CHECK(!writer.frames_are_target[1]);
    CHECK(writer.frames_are_target[2]);
    CHECK(result.writes_completed == plan.write_count);
    CHECK(result.completed && !result.cancelled && result.source_valid && result.last_frame_is_target);
    CHECK(result.failure_cause == WATCHY_TRANSITION_FAILURE_NONE);
    return 0;
}

static int test_transition_fill_keeps_snappy_budget_for_dark_targets(void) {
    static uint8_t source[WATCHY_TRANSITION_FRAME_BYTES];
    static uint8_t target[WATCHY_TRANSITION_FRAME_BYTES];
    static uint8_t scratch[WATCHY_TRANSITION_FRAME_BYTES];
    const watchy_transition_plan_t plan = {
        .effect = WATCHY_TRANSITION_FILL,
        .direction = WATCHY_TRANSITION_DIRECTION_NONE,
        .rect = {0, 0, WATCHY_TRANSITION_CANVAS_WIDTH, WATCHY_TRANSITION_CANVAS_HEIGHT},
        .write_count = 3u,
        .target_full = true,
    };
    watchy_transition_result_t result;

    memset(source, 0xff, sizeof(source));
    memset(target, 0x00, sizeof(target));
    fake_writer_t writer = {
        .target = target,
        .fail_at = SIZE_MAX,
        .cancel_at = SIZE_MAX,
    };
    CHECK(watchy_transition_execute(&plan, source, target, scratch, sizeof(scratch), fake_write,
                                    fake_cancel, fake_feed, &writer, &result) == WATCHY_STATUS_OK);
    CHECK(writer.write_count == 3u && writer.feed_count == 3u && writer.cancel_count == 2u);
    CHECK(!writer.frames_are_target[0] && !writer.frames_are_target[1] &&
          writer.frames_are_target[2]);
    CHECK(writer.modes[0] == WATCHY_REFRESH_PARTIAL &&
          writer.modes[1] == WATCHY_REFRESH_PARTIAL &&
          writer.modes[2] == WATCHY_REFRESH_FULL);
    CHECK(result.completed && !result.cancelled && result.writes_completed == 3u &&
          result.last_frame_is_target);

    target[0] = 0x80u;
    writer = (fake_writer_t){
        .target = target,
        .fail_at = SIZE_MAX,
        .cancel_at = SIZE_MAX,
    };
    CHECK(watchy_transition_execute(&plan, source, target, scratch, sizeof(scratch), fake_write,
                                    fake_cancel, fake_feed, &writer, &result) == WATCHY_STATUS_OK);
    CHECK(writer.write_count == 3u && writer.feed_count == 3u && writer.cancel_count == 2u);
    CHECK(!writer.frames_are_target[1] && writer.frames_are_target[2]);
    CHECK(writer.modes[1] == WATCHY_REFRESH_PARTIAL &&
          writer.modes[2] == WATCHY_REFRESH_FULL);
    CHECK(result.completed && !result.cancelled && result.writes_completed == 3u &&
          result.last_frame_is_target);
    return 0;
}

typedef struct {
    watchy_watchdog_membership_t membership;
    bool fail_enroll;
    bool fail_feed;
    bool fail_unenroll;
    unsigned status_calls;
    unsigned enroll_calls;
    unsigned feed_calls;
    unsigned unenroll_calls;
} watchdog_probe_t;

static watchy_watchdog_membership_t watchdog_status(void *context) {
    watchdog_probe_t *probe = context;
    ++probe->status_calls;
    return probe->membership;
}

static bool watchdog_enroll(void *context) {
    watchdog_probe_t *probe = context;
    ++probe->enroll_calls;
    if (probe->fail_enroll) {
        return false;
    }
    probe->membership = WATCHY_WATCHDOG_ENROLLED;
    return true;
}

static bool watchdog_feed(void *context) {
    watchdog_probe_t *probe = context;
    ++probe->feed_calls;
    return !probe->fail_feed && probe->membership == WATCHY_WATCHDOG_ENROLLED;
}

static bool watchdog_unenroll(void *context) {
    watchdog_probe_t *probe = context;
    ++probe->unenroll_calls;
    if (probe->fail_unenroll) {
        return false;
    }
    probe->membership = WATCHY_WATCHDOG_NOT_ENROLLED;
    return true;
}

static int test_nested_display_and_package_watchdog_scopes_leave_idle_shell_unenrolled(void) {
    watchdog_probe_t probe = {.membership = WATCHY_WATCHDOG_NOT_ENROLLED};
    const watchy_watchdog_ops_t ops = {
        .status = watchdog_status,
        .enroll = watchdog_enroll,
        .feed = watchdog_feed,
        .unenroll = watchdog_unenroll,
        .context = &probe,
    };
    watchy_watchdog_scope_t package_scope = {0};
    watchy_watchdog_scope_t display_scope = {0};

    CHECK(watchy_watchdog_scope_begin(&package_scope, &ops) == WATCHY_STATUS_OK);
    CHECK(package_scope.owns_enrollment);
    CHECK(watchy_watchdog_scope_begin(&display_scope, &ops) == WATCHY_STATUS_OK);
    CHECK(!display_scope.owns_enrollment);
    CHECK(watchy_watchdog_scope_feed(&display_scope) == WATCHY_STATUS_OK);
    CHECK(watchy_watchdog_scope_end(&display_scope) == WATCHY_STATUS_OK);
    CHECK(probe.membership == WATCHY_WATCHDOG_ENROLLED);
    CHECK(probe.unenroll_calls == 0u);
    CHECK(watchy_watchdog_scope_end(&package_scope) == WATCHY_STATUS_OK);

    /* The subsequent 30-second interactive wait owns no watchdog enrollment. */
    CHECK(probe.membership == WATCHY_WATCHDOG_NOT_ENROLLED);
    CHECK(probe.enroll_calls == 1u && probe.unenroll_calls == 1u);

    probe.membership = WATCHY_WATCHDOG_ENROLLED;
    CHECK(watchy_watchdog_scope_begin(&display_scope, &ops) == WATCHY_STATUS_OK);
    CHECK(!display_scope.owns_enrollment);
    CHECK(watchy_watchdog_scope_end(&display_scope) == WATCHY_STATUS_OK);
    CHECK(probe.membership == WATCHY_WATCHDOG_ENROLLED);
    CHECK(probe.enroll_calls == 1u && probe.unenroll_calls == 1u);

    probe = (watchdog_probe_t){
        .membership = WATCHY_WATCHDOG_NOT_ENROLLED,
        .fail_enroll = true,
    };
    CHECK(watchy_watchdog_scope_begin(&package_scope, &ops) ==
          WATCHY_STATUS_INVALID_STATE);
    CHECK(!package_scope.active && probe.enroll_calls == 1u && probe.unenroll_calls == 0u);

    probe = (watchdog_probe_t){
        .membership = WATCHY_WATCHDOG_NOT_ENROLLED,
        .fail_feed = true,
    };
    CHECK(watchy_watchdog_scope_begin(&package_scope, &ops) ==
          WATCHY_STATUS_INVALID_STATE);
    CHECK(!package_scope.active && probe.membership == WATCHY_WATCHDOG_NOT_ENROLLED);
    CHECK(probe.enroll_calls == 1u && probe.feed_calls == 1u &&
          probe.unenroll_calls == 1u);

    probe = (watchdog_probe_t){
        .membership = WATCHY_WATCHDOG_NOT_ENROLLED,
        .fail_unenroll = true,
    };
    CHECK(watchy_watchdog_scope_begin(&package_scope, &ops) == WATCHY_STATUS_OK);
    CHECK(watchy_watchdog_scope_end(&package_scope) == WATCHY_STATUS_INVALID_STATE);
    CHECK(!package_scope.active && probe.unenroll_calls == 1u);
    return 0;
}

static int test_transition_executor_cancels_optional_sequence_at_write_boundaries(void) {
    uint8_t source[WATCHY_TRANSITION_FRAME_BYTES];
    uint8_t target[WATCHY_TRANSITION_FRAME_BYTES];
    uint8_t scratch[WATCHY_TRANSITION_FRAME_BYTES];
    watchy_transition_plan_t plan = test_transition_wipe_plan();

    make_transition_fixture(source, target);
    for (size_t cancelled_after = 0u; cancelled_after + 1u < plan.write_count;
         ++cancelled_after) {
        fake_writer_t writer = {
            .target = target,
            .fail_at = SIZE_MAX,
            .cancel_at = cancelled_after,
        };
        watchy_transition_result_t result;

        CHECK(watchy_transition_execute(&plan, source, target, scratch, sizeof(scratch), fake_write,
                                        fake_cancel, fake_feed, &writer, &result) == WATCHY_STATUS_OK);
        CHECK(writer.write_count == cancelled_after + 1u);
        CHECK(writer.feed_count == cancelled_after + 1u);
        CHECK(writer.cancel_count == cancelled_after + 1u);
        CHECK(result.writes_completed == cancelled_after + 1u);
        CHECK(!result.completed && result.cancelled && result.source_valid &&
              !result.last_frame_is_target);
        CHECK(result.failure_cause == WATCHY_TRANSITION_FAILURE_NONE);
    }
    return 0;
}

static int test_transition_executor_completes_mandatory_clear_without_cancellation(void) {
    uint8_t source[WATCHY_TRANSITION_FRAME_BYTES];
    uint8_t target[WATCHY_TRANSITION_FRAME_BYTES];
    uint8_t scratch[WATCHY_TRANSITION_FRAME_BYTES];
    watchy_transition_plan_t plan = {
        .effect = WATCHY_TRANSITION_CUT,
        .direction = WATCHY_TRANSITION_DIRECTION_NONE,
        .rect = {0, 0, 200, 200},
        .write_count = 2u,
        .target_full = true,
        .mandatory_clear = true,
    };
    fake_writer_t writer = {.target = target, .fail_at = SIZE_MAX, .cancel_at = 0u};
    watchy_transition_result_t result;

    make_transition_fixture(source, target);
    CHECK(watchy_transition_execute(&plan, source, target, scratch, sizeof(scratch), fake_write,
                                    fake_cancel, fake_feed, &writer, &result) == WATCHY_STATUS_OK);
    CHECK(writer.write_count == plan.write_count);
    CHECK(writer.feed_count == plan.write_count);
    CHECK(writer.cancel_count == 0u);
    CHECK(writer.modes[0] == WATCHY_REFRESH_FULL);
    CHECK(writer.modes[1] == WATCHY_REFRESH_FULL);
    CHECK(!writer.frames_are_target[0]);
    CHECK(writer.frames_are_target[1]);
    CHECK(result.writes_completed == plan.write_count);
    CHECK(result.completed && !result.cancelled && result.source_valid && result.last_frame_is_target);
    CHECK(result.failure_cause == WATCHY_TRANSITION_FAILURE_NONE);
    return 0;
}

static int test_transition_executor_invalidates_source_for_each_failed_write(void) {
    uint8_t source[WATCHY_TRANSITION_FRAME_BYTES];
    uint8_t target[WATCHY_TRANSITION_FRAME_BYTES];
    uint8_t scratch[WATCHY_TRANSITION_FRAME_BYTES];
    watchy_transition_plan_t plan = test_transition_wipe_plan();

    make_transition_fixture(source, target);
    for (size_t fail_at = 0u; fail_at < plan.write_count; ++fail_at) {
        fake_writer_t writer = {.target = target, .fail_at = fail_at, .cancel_at = SIZE_MAX};
        watchy_transition_result_t result;

        CHECK(watchy_transition_execute(&plan, source, target, scratch, sizeof(scratch), fake_write,
                                        fake_cancel, fake_feed, &writer, &result) ==
              WATCHY_STATUS_INVALID_STATE);
        CHECK(writer.write_count == fail_at + 1u);
        CHECK(writer.feed_count == fail_at);
        CHECK(writer.cancel_count == fail_at);
        CHECK(result.writes_completed == fail_at);
        CHECK(!result.completed && !result.cancelled && !result.source_valid &&
              !result.last_frame_is_target);
        CHECK(result.failure_cause == WATCHY_TRANSITION_FAILURE_WRITE);
    }
    return 0;
}

static int test_transition_executor_reports_composition_failure_before_write(void) {
    uint8_t target[WATCHY_TRANSITION_FRAME_BYTES];
    uint8_t scratch[WATCHY_TRANSITION_FRAME_BYTES];
    watchy_transition_plan_t plan = test_transition_wipe_plan();
    fake_writer_t writer = {.target = target, .fail_at = SIZE_MAX, .cancel_at = SIZE_MAX};
    watchy_transition_result_t result;

    memset(target, 0xff, sizeof(target));
    CHECK(watchy_transition_execute(&plan, NULL, target, scratch, sizeof(scratch), fake_write,
                                    fake_cancel, fake_feed, &writer, &result) ==
          WATCHY_STATUS_INVALID_STATE);
    CHECK(writer.write_count == 0u);
    CHECK(writer.feed_count == 0u);
    CHECK(writer.cancel_count == 0u);
    CHECK(result.writes_completed == 0u);
    CHECK(!result.completed && !result.cancelled && !result.source_valid &&
          !result.last_frame_is_target);
    CHECK(result.failure_cause == WATCHY_TRANSITION_FAILURE_COMPOSE);
    return 0;
}

static int test_transition_executor_rejects_empty_plan_without_callbacks(void) {
    const watchy_transition_plan_t plan = {0};
    fake_writer_t writer = {.fail_at = SIZE_MAX, .cancel_at = SIZE_MAX};
    watchy_transition_result_t result;

    CHECK(watchy_transition_execute(&plan, NULL, NULL, NULL, 0u, fake_write, fake_cancel,
                                    fake_feed, &writer, &result) == WATCHY_STATUS_INVALID_STATE);
    CHECK(writer.write_count == 0u);
    CHECK(writer.feed_count == 0u);
    CHECK(writer.cancel_count == 0u);
    CHECK(result.writes_completed == 0u);
    CHECK(!result.completed && !result.cancelled && !result.source_valid &&
          !result.last_frame_is_target);
    return 0;
}

int main(void) {
    int (*tests[])(void) = {
        test_bundle_rejects_bad_magic,
        test_bundle_accepts_well_formed_layout,
        test_bundle_rejects_truncated_blob,
        test_bundle_rejects_overlapping_sections,
        test_bundle_rejects_out_of_order_sections,
        test_bundle_rejects_section_offset_overflow,
        test_bundle_rejects_gap_before_manifest,
        test_bundle_rejects_gap_between_manifest_and_elf,
        test_bundle_rejects_gap_between_elf_and_assets,
        test_bundle_rejects_trailing_bytes,
        test_bundle_rejects_oversized_blob_for_exact_package,
        test_abi_negotiation,
        test_runtime_lifecycle,
        test_refresh_policy_promotes_after_partial_limit,
        test_refresh_policy_resets_after_explicit_full,
        test_transition_rejects_malformed_requests,
        test_transition_normalizes_omitted_rectangle,
        test_transition_uses_cut_when_no_request_is_supplied,
        test_transition_rejects_out_of_range_policy_levels,
        test_transition_assigns_bounded_write_counts,
        test_transition_respects_full_target_preference,
        test_transition_policy_matrix_downgrades_optional_motion,
        test_transition_clear_overrides_optional_effect,
        test_transition_clear_overrides_invalid_optional_request,
        test_transition_compositor_rejects_invalid_calls,
        test_transition_compositor_clips_intersecting_rectangles,
        test_transition_effect_edges_and_checkerboard,
        test_transition_effects_match_frozen_frames,
        test_transition_mandatory_clear_inverts_then_targets,
        test_transition_executor_writes_bounded_sequence_and_target,
        test_transition_fill_keeps_snappy_budget_for_dark_targets,
        test_transition_executor_cancels_optional_sequence_at_write_boundaries,
        test_transition_executor_completes_mandatory_clear_without_cancellation,
        test_transition_executor_invalidates_source_for_each_failed_write,
        test_transition_executor_reports_composition_failure_before_write,
        test_transition_executor_rejects_empty_plan_without_callbacks,
        test_nested_display_and_package_watchdog_scopes_leave_idle_shell_unenrolled,
    };
    const size_t count = sizeof(tests) / sizeof(tests[0]);
    for (size_t i = 0; i < count; ++i) {
        if (tests[i]() != 0) {
            return 1;
        }
    }
    printf("PASS %zu tests\n", count);
    return 0;
}
