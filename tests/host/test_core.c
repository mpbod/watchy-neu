#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "watchy/runtime.h"
#include "watchy/wpk.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static int test_bundle_rejects_bad_magic(void) {
    uint8_t bytes[WPK_HEADER_SIZE] = {0};
    wpk_view_t view = {0};
    CHECK(wpk_parse(bytes, sizeof(bytes), &view) == WPK_ERR_MAGIC);
    return 0;
}

static int test_bundle_accepts_well_formed_layout(void) {
    uint8_t bytes[WPK_HEADER_SIZE + 32] = {0};
    wpk_header_t *header = (wpk_header_t *)bytes;
    memcpy(header->magic, WPK_MAGIC, sizeof(header->magic));
    header->format_version = WPK_FORMAT_VERSION;
    header->header_size = WPK_HEADER_SIZE;
    header->manifest_offset = WPK_HEADER_SIZE;
    header->manifest_size = 8;
    header->elf_offset = WPK_HEADER_SIZE + 8;
    header->elf_size = 16;
    header->assets_offset = WPK_HEADER_SIZE + 24;
    header->assets_size = 8;
    header->total_size = sizeof(bytes);
    wpk_view_t view = {0};
    CHECK(wpk_parse(bytes, sizeof(bytes), &view) == WPK_OK);
    CHECK(view.manifest_size == 8);
    CHECK(view.elf_size == 16);
    CHECK(view.assets_size == 8);
    return 0;
}

static int test_bundle_rejects_overlapping_sections(void) {
    uint8_t bytes[WPK_HEADER_SIZE + 32] = {0};
    wpk_header_t *header = (wpk_header_t *)bytes;
    memcpy(header->magic, WPK_MAGIC, sizeof(header->magic));
    header->format_version = WPK_FORMAT_VERSION;
    header->header_size = WPK_HEADER_SIZE;
    header->manifest_offset = WPK_HEADER_SIZE;
    header->manifest_size = 16;
    header->elf_offset = WPK_HEADER_SIZE + 8;
    header->elf_size = 16;
    header->assets_offset = WPK_HEADER_SIZE + 24;
    header->assets_size = 8;
    header->total_size = sizeof(bytes);
    wpk_view_t view = {0};
    CHECK(wpk_parse(bytes, sizeof(bytes), &view) == WPK_ERR_LAYOUT);
    return 0;
}

static int test_abi_negotiation(void) {
    CHECK(watchy_abi_compatible(1, 0, 1, 0));
    CHECK(watchy_abi_compatible(1, 0, 1, 4));
    CHECK(!watchy_abi_compatible(1, 1, 1, 0));
    CHECK(!watchy_abi_compatible(2, 0, 1, 9));
    return 0;
}

static int test_runtime_lifecycle(void) {
    watchy_runtime_t runtime;
    watchy_runtime_reset(&runtime);
    CHECK(runtime.state == WATCHY_RUNTIME_EMPTY);
    CHECK(watchy_runtime_transition(&runtime, WATCHY_RUNTIME_LOADED) == WATCHY_STATUS_OK);
    CHECK(watchy_runtime_transition(&runtime, WATCHY_RUNTIME_STARTED) == WATCHY_STATUS_OK);
    CHECK(watchy_runtime_transition(&runtime, WATCHY_RUNTIME_STOPPED) == WATCHY_STATUS_OK);
    CHECK(watchy_runtime_transition(&runtime, WATCHY_RUNTIME_EMPTY) == WATCHY_STATUS_OK);
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

int main(void) {
    int (*tests[])(void) = {
        test_bundle_rejects_bad_magic,
        test_bundle_accepts_well_formed_layout,
        test_bundle_rejects_overlapping_sections,
        test_abi_negotiation,
        test_runtime_lifecycle,
        test_refresh_policy_promotes_after_partial_limit,
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
