#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "watchy/runtime.h"
#include "watchy/transition.h"
#include "watchy/wpk.h"

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
    CHECK(plan.write_count == 4u);
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

static int test_transition_rejects_unknown_policy_level(void) {
    watchy_transition_request_v1_t request = valid_transition_request();
    watchy_transition_policy_context_t context = {
        .level = (watchy_transition_level_t)-1,
        .attended = true,
        .battery_mv = 3900u,
        .source_valid = true,
    };
    watchy_transition_plan_t plan;

    CHECK(watchy_transition_plan(&request, &context, &plan) == WATCHY_STATUS_INVALID_ARGUMENT);
    return 0;
}

static int test_transition_assigns_bounded_write_counts(void) {
    static const struct transition_count_case {
        watchy_transition_effect_t effect;
        uint8_t write_count;
    } cases[] = {
        {WATCHY_TRANSITION_CUT, 1u},
        {WATCHY_TRANSITION_FLASH, 2u},
        {WATCHY_TRANSITION_WIPE, 4u},
        {WATCHY_TRANSITION_PUSH, 3u},
        {WATCHY_TRANSITION_DITHER, 2u},
        {WATCHY_TRANSITION_GROW, 3u},
        {WATCHY_TRANSITION_ODOMETER, 3u},
        {WATCHY_TRANSITION_SPLIT, 3u},
        {WATCHY_TRANSITION_FILL, 5u},
        {WATCHY_TRANSITION_SHUTTER, 5u},
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
    CHECK(plan.effect == WATCHY_TRANSITION_WIPE && plan.write_count == 4u);
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
        test_transition_rejects_unknown_policy_level,
        test_transition_assigns_bounded_write_counts,
        test_transition_respects_full_target_preference,
        test_transition_policy_matrix_downgrades_optional_motion,
        test_transition_clear_overrides_optional_effect,
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
