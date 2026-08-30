#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "watchy/packages.h"
#include "watchy/wpk.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static int test_package_id_accepts_only_bounded_lowercase_ascii(void) {
    CHECK(watchy_package_id_valid("clock.simple"));
    CHECK(watchy_package_id_valid("a"));
    CHECK(watchy_package_id_valid("a23456789012345678901234567890123456789012345678"));
    CHECK(!watchy_package_id_valid(NULL));
    CHECK(!watchy_package_id_valid(""));
    CHECK(!watchy_package_id_valid("Clock"));
    CHECK(!watchy_package_id_valid(".clock"));
    CHECK(!watchy_package_id_valid("clock/face"));
    CHECK(watchy_package_id_valid("clock..face"));
    CHECK(!watchy_package_id_valid("a234567890123456789012345678901234567890123456789"));
    return 0;
}

typedef struct {
    const uint8_t *normalized;
    size_t normalized_size;
    uint8_t digest[WATCHY_PACKAGE_DIGEST_SIZE];
} digest_probe_t;

static bool digest_probe_sha256(void *context,
                                const watchy_byte_region_t *regions,
                                size_t region_count,
                                uint8_t out_digest[WATCHY_PACKAGE_DIGEST_SIZE]) {
    digest_probe_t *probe = (digest_probe_t *)context;
    size_t cursor = 0u;

    if (probe == NULL || regions == NULL || region_count != 3u || out_digest == NULL) {
        return false;
    }
    for (size_t region = 0u; region < region_count; ++region) {
        if (regions[region].bytes == NULL ||
            regions[region].size > probe->normalized_size - cursor ||
            memcmp(regions[region].bytes,
                   probe->normalized + cursor,
                   regions[region].size) != 0) {
            return false;
        }
        cursor += regions[region].size;
    }
    if (cursor != probe->normalized_size) {
        return false;
    }
    memcpy(out_digest, probe->digest, sizeof(probe->digest));
    return true;
}

static int test_package_digest_hashes_the_complete_wpk_with_zeroed_digest(void) {
    uint8_t bytes[WPK_HEADER_SIZE + 8u];
    uint8_t normalized[WPK_HEADER_SIZE + 8u];
    digest_probe_t probe;
    watchy_crypto_api_t crypto = {.sha256 = digest_probe_sha256, .context = &probe};
    uint8_t calculated[WATCHY_PACKAGE_DIGEST_SIZE];
    wpk_header_t *header = (wpk_header_t *)bytes;

    for (size_t index = 0u; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(index + 1u);
    }
    probe.normalized = normalized;
    probe.normalized_size = sizeof(normalized);
    memcpy(normalized, bytes, sizeof(bytes));
    memset(normalized + offsetof(wpk_header_t, package_sha256),
           0,
           WATCHY_PACKAGE_DIGEST_SIZE);
    for (size_t index = 0u; index < sizeof(probe.digest); ++index) {
        probe.digest[index] = (uint8_t)(0xa0u + index);
    }
    memcpy(header->package_sha256, probe.digest, sizeof(probe.digest));

    CHECK(watchy_package_calculate_digest(bytes, sizeof(bytes), &crypto, calculated) ==
          WATCHY_PACKAGE_OK);
    CHECK(memcmp(calculated, probe.digest, sizeof(calculated)) == 0);
    CHECK(watchy_package_digest_matches(bytes, sizeof(bytes), &crypto) == WATCHY_PACKAGE_OK);
    header->package_sha256[17] ^= 0x80u;
    CHECK(watchy_package_digest_matches(bytes, sizeof(bytes), &crypto) == WATCHY_PACKAGE_ERR_DIGEST);
    return 0;
}

static watchy_package_status_t parse_manifest_text(const char *text,
                                                   watchy_package_manifest_t *out_manifest) {
    return watchy_package_manifest_parse((const uint8_t *)text, strlen(text), out_manifest);
}

static int test_manifest_accepts_the_canonical_required_schema(void) {
    static const char manifest_text[] =
        "{\"abi_major\":1,\"abi_minor\":0,\"assets\":[{\"path\":\"fonts/main.bin\",\"size\":3}],"
        "\"capabilities\":515,\"id\":\"clock.simple\",\"max_runtime_bytes\":65536,"
        "\"name\":\"Simple Clock\",\"type\":\"watchface\",\"version\":\"1.2.3\"}";
    watchy_package_manifest_t manifest;
    watchy_package_status_t status;

    status = parse_manifest_text(manifest_text, &manifest);
    if (status != WATCHY_PACKAGE_OK) {
        fprintf(stderr, "manifest status=%d\n", status);
    }
    CHECK(status == WATCHY_PACKAGE_OK);
    CHECK(strcmp(manifest.id, "clock.simple") == 0);
    CHECK(strcmp(manifest.name, "Simple Clock") == 0);
    CHECK(strcmp(manifest.version, "1.2.3") == 0);
    CHECK(manifest.type == WATCHY_PACKAGE_TYPE_WATCHFACE);
    CHECK(manifest.abi_major == 1u && manifest.abi_minor == 0u);
    CHECK(manifest.capabilities == 515u);
    CHECK(manifest.max_runtime_bytes == 65536u);
    CHECK(manifest.asset_count == 1u);
    CHECK(strcmp(manifest.assets[0].path, "fonts/main.bin") == 0);
    CHECK(manifest.assets[0].size == 3u);
    return 0;
}

static int test_manifest_rejects_noncanonical_duplicate_or_unsafe_content(void) {
    static const char noncanonical_space[] =
        "{\"abi_major\":1, \"abi_minor\":0,\"assets\":[],\"capabilities\":0,\"id\":\"a\","
        "\"max_runtime_bytes\":1,\"name\":\"A\",\"type\":\"app\",\"version\":\"1\"}";
    static const char duplicate_id[] =
        "{\"abi_major\":1,\"abi_minor\":0,\"assets\":[],\"capabilities\":0,\"id\":\"a\","
        "\"id\":\"b\",\"max_runtime_bytes\":1,\"name\":\"A\",\"type\":\"app\",\"version\":\"1\"}";
    static const char unknown_capability[] =
        "{\"abi_major\":1,\"abi_minor\":0,\"assets\":[],\"capabilities\":1024,\"id\":\"a\","
        "\"max_runtime_bytes\":1,\"name\":\"A\",\"type\":\"app\",\"version\":\"1\"}";
    static const char traversal[] =
        "{\"abi_major\":1,\"abi_minor\":0,\"assets\":[{\"path\":\"../secret\",\"size\":1}],"
        "\"capabilities\":0,\"id\":\"a\",\"max_runtime_bytes\":1,\"name\":\"A\","
        "\"type\":\"app\",\"version\":\"1\"}";
    static const char collision[] =
        "{\"abi_major\":1,\"abi_minor\":0,\"assets\":[{\"path\":\"icons\",\"size\":1},"
        "{\"path\":\"icons/main.bin\",\"size\":1}],\"capabilities\":0,\"id\":\"a\","
        "\"max_runtime_bytes\":1,\"name\":\"A\",\"type\":\"app\",\"version\":\"1\"}";
    static const char excessive_runtime[] =
        "{\"abi_major\":1,\"abi_minor\":0,\"assets\":[],\"capabilities\":0,\"id\":\"a\","
        "\"max_runtime_bytes\":163841,\"name\":\"A\",\"type\":\"app\",\"version\":\"1\"}";
    static const char traversal_version[] =
        "{\"abi_major\":1,\"abi_minor\":0,\"assets\":[],\"capabilities\":0,\"id\":\"a\","
        "\"max_runtime_bytes\":1,\"name\":\"A\",\"type\":\"app\",\"version\":\"../1\"}";
    static const uint8_t invalid_utf8[] =
        "{\"abi_major\":1,\"abi_minor\":0,\"assets\":[],\"capabilities\":0,\"id\":\"a\","
        "\"max_runtime_bytes\":1,\"name\":\"\xc0\x80\",\"type\":\"app\",\"version\":\"1\"}";
    watchy_package_manifest_t manifest;

    CHECK(parse_manifest_text(noncanonical_space, &manifest) == WATCHY_PACKAGE_ERR_MANIFEST);
    CHECK(parse_manifest_text(duplicate_id, &manifest) == WATCHY_PACKAGE_ERR_MANIFEST);
    CHECK(parse_manifest_text(unknown_capability, &manifest) == WATCHY_PACKAGE_ERR_CAPABILITY);
    CHECK(parse_manifest_text(traversal, &manifest) == WATCHY_PACKAGE_ERR_PATH);
    CHECK(parse_manifest_text(collision, &manifest) == WATCHY_PACKAGE_ERR_COLLISION);
    CHECK(parse_manifest_text(excessive_runtime, &manifest) == WATCHY_PACKAGE_ERR_LIMIT);
    CHECK(parse_manifest_text(traversal_version, &manifest) == WATCHY_PACKAGE_ERR_PATH);
    CHECK(watchy_package_manifest_parse(invalid_utf8, sizeof(invalid_utf8) - 1u, &manifest) ==
          WATCHY_PACKAGE_ERR_UTF8);
    return 0;
}

static void put_u16le(uint8_t *bytes, size_t offset, uint16_t value) {
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1u] = (uint8_t)(value >> 8);
}

static void put_u32le(uint8_t *bytes, size_t offset, uint32_t value) {
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1u] = (uint8_t)(value >> 8);
    bytes[offset + 2u] = (uint8_t)(value >> 16);
    bytes[offset + 3u] = (uint8_t)(value >> 24);
}

static void make_valid_xtensa_elf(uint8_t bytes[168]) {
    memset(bytes, 0, 168u);
    bytes[0] = 0x7fu;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 1u;
    bytes[5] = 1u;
    bytes[6] = 1u;
    put_u16le(bytes, 16u, 3u);
    put_u16le(bytes, 18u, 94u);
    put_u32le(bytes, 20u, 1u);
    put_u32le(bytes, 28u, 52u);
    put_u32le(bytes, 32u, 84u);
    put_u16le(bytes, 40u, 52u);
    put_u16le(bytes, 42u, 32u);
    put_u16le(bytes, 44u, 1u);
    put_u16le(bytes, 46u, 40u);
    put_u16le(bytes, 48u, 2u);
    put_u16le(bytes, 50u, 0u);

    put_u32le(bytes, 52u, 1u);
    put_u32le(bytes, 56u, 164u);
    put_u32le(bytes, 60u, 0x1000u);
    put_u32le(bytes, 68u, 4u);
    put_u32le(bytes, 72u, 32u);
    put_u32le(bytes, 76u, 5u);
    put_u32le(bytes, 80u, 4u);

    put_u32le(bytes, 128u, 1u);
    put_u32le(bytes, 132u, 6u);
    put_u32le(bytes, 140u, 164u);
    put_u32le(bytes, 144u, 4u);
    put_u32le(bytes, 156u, 4u);
    bytes[164] = 0x06u;
    bytes[165] = 0x01u;
    bytes[166] = 0x00u;
    bytes[167] = 0x00u;
}

static int test_elf_validator_accepts_only_sane_xtensa_shared_objects(void) {
    uint8_t elf[168];
    uint32_t runtime_bytes = 0u;

    make_valid_xtensa_elf(elf);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, &runtime_bytes) == WATCHY_PACKAGE_OK);
    CHECK(runtime_bytes == 32u);

    elf[4] = 2u;
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    elf[5] = 2u;
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    put_u16le(elf, 16u, 2u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    put_u16le(elf, 18u, 243u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    put_u32le(elf, 28u, UINT32_MAX - 8u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    put_u32le(elf, 76u, 4u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    put_u32le(elf, 144u, 8u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 16u, NULL) == WATCHY_PACKAGE_ERR_LIMIT);
    CHECK(watchy_package_elf_validate(elf,
                                      sizeof(elf),
                                      WATCHY_PACKAGE_RUNTIME_BYTES_MAX + 1u,
                                      NULL) == WATCHY_PACKAGE_ERR_LIMIT);
    return 0;
}

static int test_elf_validator_rejects_files_larger_than_384_kib(void) {
    static uint8_t oversized[WATCHY_PACKAGE_ELF_BYTES_MAX + 1u];
    CHECK(watchy_package_elf_validate(oversized, sizeof(oversized), 1u, NULL) ==
          WATCHY_PACKAGE_ERR_LIMIT);
    return 0;
}

static size_t make_valid_wpk(uint8_t *bytes, size_t capacity, digest_probe_t *probe) {
    static const char manifest_text[] =
        "{\"abi_major\":1,\"abi_minor\":0,\"assets\":[{\"path\":\"icon.bin\",\"size\":3}],"
        "\"capabilities\":515,\"id\":\"clock.simple\",\"max_runtime_bytes\":64,"
        "\"name\":\"Simple Clock\",\"type\":\"watchface\",\"version\":\"1.2.3\"}";
    const size_t manifest_size = sizeof(manifest_text) - 1u;
    const size_t total_size = WPK_HEADER_SIZE + manifest_size + 168u + 3u;
    wpk_header_t *header = (wpk_header_t *)bytes;

    if (capacity < total_size) {
        return 0u;
    }
    memset(bytes, 0, total_size);
    memcpy(header->magic, WPK_MAGIC, sizeof(header->magic));
    header->format_version = WPK_FORMAT_VERSION;
    header->header_size = WPK_HEADER_SIZE;
    header->manifest_offset = WPK_HEADER_SIZE;
    header->manifest_size = (uint32_t)manifest_size;
    header->elf_offset = header->manifest_offset + header->manifest_size;
    header->elf_size = 168u;
    header->assets_offset = header->elf_offset + header->elf_size;
    header->assets_size = 3u;
    header->total_size = (uint32_t)total_size;
    memcpy(bytes + header->manifest_offset, manifest_text, manifest_size);
    make_valid_xtensa_elf(bytes + header->elf_offset);
    memcpy(bytes + header->assets_offset, "abc", 3u);
    for (size_t index = 0u; index < sizeof(probe->digest); ++index) {
        probe->digest[index] = (uint8_t)(0x55u + index);
    }
    memcpy(header->package_sha256, probe->digest, sizeof(probe->digest));
    return total_size;
}

static int test_complete_wpk_validation_rejects_asset_mismatch_and_trailing_data(void) {
    uint8_t bytes[1024];
    uint8_t normalized[1024];
    digest_probe_t probe = {.normalized = normalized};
    watchy_crypto_api_t crypto = {.sha256 = digest_probe_sha256, .context = &probe};
    watchy_validated_package_t package;
    size_t size = make_valid_wpk(bytes, sizeof(bytes), &probe);
    wpk_header_t *header = (wpk_header_t *)bytes;
    uint8_t *asset_size_digit;

    CHECK(size != 0u);
    memcpy(normalized, bytes, size);
    memset(normalized + offsetof(wpk_header_t, package_sha256), 0, WATCHY_PACKAGE_DIGEST_SIZE);
    probe.normalized_size = size;
    CHECK(watchy_package_validate(bytes, size, &crypto, &package) == WATCHY_PACKAGE_OK);
    CHECK(strcmp(package.manifest.id, "clock.simple") == 0);
    CHECK(package.runtime_bytes == 32u);
    CHECK(package.assets_size == 3u);

    asset_size_digit = (uint8_t *)strstr((char *)bytes + header->manifest_offset,
                                        "\"path\":\"icon.bin\",\"size\":3");
    CHECK(asset_size_digit != NULL);
    asset_size_digit += strlen("\"path\":\"icon.bin\",\"size\":");
    *asset_size_digit = '4';
    memcpy(normalized, bytes, size);
    memset(normalized + offsetof(wpk_header_t, package_sha256), 0, WATCHY_PACKAGE_DIGEST_SIZE);
    CHECK(watchy_package_validate(bytes, size, &crypto, &package) == WATCHY_PACKAGE_ERR_ASSETS);

    *asset_size_digit = '3';
    memcpy(normalized, bytes, size);
    memset(normalized + offsetof(wpk_header_t, package_sha256), 0, WATCHY_PACKAGE_DIGEST_SIZE);
    bytes[size] = 0u;
    CHECK(watchy_package_validate(bytes, size + 1u, &crypto, &package) == WATCHY_PACKAGE_ERR_WPK);
    return 0;
}

typedef struct {
    watchy_package_index_t persisted;
    bool present;
    bool fail_save;
} fake_index_store_t;

static watchy_index_store_result_t fake_index_load(void *context,
                                                   watchy_package_index_t *out_index) {
    fake_index_store_t *store = (fake_index_store_t *)context;
    if (!store->present) {
        return WATCHY_INDEX_STORE_NOT_FOUND;
    }
    *out_index = store->persisted;
    return WATCHY_INDEX_STORE_OK;
}

static bool fake_index_save(void *context, const watchy_package_index_t *index) {
    fake_index_store_t *store = (fake_index_store_t *)context;
    if (store->fail_save) {
        return false;
    }
    store->persisted = *index;
    store->present = true;
    return true;
}

static watchy_package_index_store_t fake_store_api(fake_index_store_t *store) {
    return (watchy_package_index_store_t){
        .load = fake_index_load,
        .save = fake_index_save,
        .context = store,
    };
}

static int test_selection_is_transactional_when_persistence_fails(void) {
    fake_index_store_t store = {.present = true};
    watchy_package_index_store_t api = fake_store_api(&store);
    watchy_package_index_manager_t manager;
    const watchy_package_index_t *snapshot;

    store.persisted.magic = WATCHY_PACKAGE_INDEX_MAGIC;
    store.persisted.version = WATCHY_PACKAGE_INDEX_VERSION;
    strcpy(store.persisted.active_watchface, "clock.old@1.0");
    CHECK(watchy_package_index_init(&manager, &api) == WATCHY_PACKAGE_OK);
    store.fail_save = true;
    CHECK(watchy_package_select_watchface(&manager, "clock.new@2.0") == WATCHY_PACKAGE_ERR_STORE);
    snapshot = watchy_package_index_snapshot(&manager);
    CHECK(strcmp(snapshot->active_watchface, "clock.old@1.0") == 0);
    CHECK(snapshot->pending_watchface[0] == '\0');
    CHECK(snapshot->prior_watchface[0] == '\0');
    CHECK(strcmp(store.persisted.active_watchface, "clock.old@1.0") == 0);

    store.fail_save = false;
    CHECK(watchy_package_select_watchface(&manager, "clock.new@2.0") == WATCHY_PACKAGE_OK);
    snapshot = watchy_package_index_snapshot(&manager);
    CHECK(strcmp(snapshot->active_watchface, "clock.old@1.0") == 0);
    CHECK(strcmp(snapshot->pending_watchface, "clock.new@2.0") == 0);
    CHECK(strcmp(snapshot->prior_watchface, "clock.old@1.0") == 0);
    return 0;
}

static int test_pending_watchface_promotes_after_render_and_rolls_back_on_failure(void) {
    fake_index_store_t store = {0};
    watchy_package_index_store_t api = fake_store_api(&store);
    watchy_package_index_manager_t manager;
    const watchy_package_index_t *snapshot;

    CHECK(watchy_package_index_init(&manager, &api) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_select_watchface(&manager, "clock.good@1.0") == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_promote_pending(&manager, "clock.good@1.0") == WATCHY_PACKAGE_OK);
    snapshot = watchy_package_index_snapshot(&manager);
    CHECK(strcmp(snapshot->active_watchface, "clock.good@1.0") == 0);
    CHECK(snapshot->pending_watchface[0] == '\0');

    CHECK(watchy_package_select_watchface(&manager, "clock.bad@1.0") == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_rollback_pending(&manager, "clock.bad@1.0") == WATCHY_PACKAGE_OK);
    snapshot = watchy_package_index_snapshot(&manager);
    CHECK(strcmp(snapshot->active_watchface, "clock.good@1.0") == 0);
    CHECK(snapshot->pending_watchface[0] == '\0');
    return 0;
}

static int test_three_incomplete_attempts_quarantine_persistently_and_safe_mode_bypasses(void) {
    fake_index_store_t store = {0};
    watchy_package_index_store_t api = fake_store_api(&store);
    watchy_package_index_manager_t manager;
    watchy_package_index_manager_t reloaded;

    CHECK(watchy_package_index_init(&manager, &api) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_begin_attempt(&manager, "clock.risky@1.0", true) ==
          WATCHY_PACKAGE_ERR_SAFE_MODE);
    CHECK(!watchy_package_is_quarantined(&manager, "clock.risky@1.0"));

    for (unsigned attempt = 0u; attempt < 2u; ++attempt) {
        CHECK(watchy_package_begin_attempt(&manager, "clock.risky@1.0", false) == WATCHY_PACKAGE_OK);
        CHECK(watchy_package_finish_attempt(&manager, "clock.risky@1.0", false) == WATCHY_PACKAGE_OK);
        CHECK(!watchy_package_is_quarantined(&manager, "clock.risky@1.0"));
    }
    CHECK(watchy_package_begin_attempt(&manager, "clock.risky@1.0", false) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_finish_attempt(&manager, "clock.risky@1.0", false) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_is_quarantined(&manager, "clock.risky@1.0"));
    CHECK(watchy_package_begin_attempt(&manager, "clock.risky@1.0", false) ==
          WATCHY_PACKAGE_ERR_QUARANTINED);

    CHECK(watchy_package_index_init(&reloaded, &api) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_is_quarantined(&reloaded, "clock.risky@1.0"));
    CHECK(watchy_package_begin_attempt(&reloaded, "clock.clean@1.0", false) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_finish_attempt(&reloaded, "clock.clean@1.0", true) == WATCHY_PACKAGE_OK);
    CHECK(!watchy_package_is_quarantined(&reloaded, "clock.clean@1.0"));
    return 0;
}

static int test_index_rejects_corrupt_persisted_counts_and_strings(void) {
    fake_index_store_t store = {.present = true};
    watchy_package_index_store_t api = fake_store_api(&store);
    watchy_package_index_manager_t manager;

    store.persisted.magic = WATCHY_PACKAGE_INDEX_MAGIC;
    store.persisted.version = WATCHY_PACKAGE_INDEX_VERSION;
    store.persisted.installed_count = WATCHY_PACKAGE_INSTALLED_MAX + 1u;
    CHECK(watchy_package_index_init(&manager, &api) == WATCHY_PACKAGE_ERR_STORE);
    memset(&store.persisted, 0, sizeof(store.persisted));
    store.persisted.magic = WATCHY_PACKAGE_INDEX_MAGIC;
    store.persisted.version = WATCHY_PACKAGE_INDEX_VERSION;
    store.persisted.health_count = 1u;
    memset(store.persisted.health[0].package_ref,
           'x',
           sizeof(store.persisted.health[0].package_ref));
    CHECK(watchy_package_index_init(&manager, &api) == WATCHY_PACKAGE_ERR_STORE);
    return 0;
}

typedef enum {
    CALLBACKS_OK = 0,
    FAIL_ON_LOAD,
    FAIL_ON_START,
    FAIL_ON_EVENT,
    FAIL_ON_RENDER,
} callback_failure_t;

typedef struct {
    bool live_handle;
    callback_failure_t failure;
} fake_loader_t;

static fake_loader_t *active_fake_loader;

static watchy_status_t package_on_load(const watchy_host_caps_v1_t *host, void **user_data) {
    if (host == NULL || user_data == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    *user_data = active_fake_loader;
    return active_fake_loader->failure == FAIL_ON_LOAD ? WATCHY_STATUS_INVALID_STATE : WATCHY_STATUS_OK;
}

static void package_on_unload(void *user_data) {
    (void)user_data;
}

static watchy_status_t package_on_start(void *user_data) {
    fake_loader_t *loader = (fake_loader_t *)user_data;
    return loader->failure == FAIL_ON_START ? WATCHY_STATUS_INVALID_STATE : WATCHY_STATUS_OK;
}

static void package_on_stop(void *user_data) {
    (void)user_data;
}

static watchy_status_t package_on_event(void *user_data, const watchy_event_t *event) {
    fake_loader_t *loader = (fake_loader_t *)user_data;
    if (event == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    return loader->failure == FAIL_ON_EVENT ? WATCHY_STATUS_INVALID_STATE : WATCHY_STATUS_OK;
}

static watchy_status_t package_on_render(void *user_data,
                                         watchy_canvas_t *canvas,
                                         watchy_refresh_mode_t *mode) {
    fake_loader_t *loader = (fake_loader_t *)user_data;
    if (canvas == NULL || mode == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    *mode = WATCHY_REFRESH_PARTIAL;
    return loader->failure == FAIL_ON_RENDER ? WATCHY_STATUS_INVALID_STATE : WATCHY_STATUS_OK;
}

static const watchy_package_descriptor_v1_t *fake_package_entry(void) {
    static const watchy_package_descriptor_v1_t descriptor = {
        .size = sizeof(watchy_package_descriptor_v1_t),
        .metadata = {
            .identifier = "clock.test",
            .name = "Test Clock",
            .version = "1.0",
            .abi = {.major = WATCHY_ABI_V1_MAJOR, .minor = WATCHY_ABI_V1_MINOR},
            .flags = 0u,
        },
        .callbacks = {
            .on_load = package_on_load,
            .on_unload = package_on_unload,
            .on_start = package_on_start,
            .on_stop = package_on_stop,
            .on_event = package_on_event,
            .on_render = package_on_render,
        },
    };
    return &descriptor;
}

static void *fake_dlopen(void *context, const char *path, int mode) {
    fake_loader_t *loader = (fake_loader_t *)context;
    if (path == NULL || path[0] == '\0' || mode != WATCHY_PACKAGE_RTLD_NOW || loader->live_handle) {
        return NULL;
    }
    loader->live_handle = true;
    active_fake_loader = loader;
    return loader;
}

static void *fake_dlsym(void *context, void *handle, const char *symbol) {
    union {
        void *object;
        watchy_package_entry_fn_t function;
    } address;
    if (handle != context || strcmp(symbol, "watchy_package_entry") != 0) {
        return NULL;
    }
    address.function = fake_package_entry;
    return address.object;
}

static int fake_dlclose(void *context, void *handle) {
    fake_loader_t *loader = (fake_loader_t *)context;
    if (handle != loader || !loader->live_handle) {
        return -1;
    }
    loader->live_handle = false;
    return 0;
}

static watchy_package_manifest_t runtime_manifest(void) {
    watchy_package_manifest_t manifest = {
        .type = WATCHY_PACKAGE_TYPE_WATCHFACE,
        .abi_major = WATCHY_ABI_V1_MAJOR,
        .abi_minor = WATCHY_ABI_V1_MINOR,
        .max_runtime_bytes = 64u,
    };
    strcpy(manifest.id, "clock.test");
    strcpy(manifest.name, "Test Clock");
    strcpy(manifest.version, "1.0");
    return manifest;
}

static watchy_package_loader_api_t fake_loader_api(fake_loader_t *loader) {
    return (watchy_package_loader_api_t){
        .open = fake_dlopen,
        .symbol = fake_dlsym,
        .close = fake_dlclose,
        .context = loader,
    };
}

static int load_and_start_session(watchy_package_session_t *session,
                                  fake_loader_t *loader,
                                  const watchy_package_manifest_t *manifest,
                                  const watchy_host_caps_v1_t *host) {
    watchy_package_loader_api_t api = fake_loader_api(loader);
    CHECK(watchy_package_session_init(session, &api, NULL) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_session_load(session, "/data/packages/clock.test/1.0/package.so",
                                      manifest, host, false, false) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_session_start(session) == WATCHY_PACKAGE_OK);
    return 0;
}

static int test_lifecycle_closes_the_handle_after_every_callback_failure(void) {
    watchy_package_manifest_t manifest = runtime_manifest();
    watchy_host_caps_v1_t host = {
        .abi = {.major = WATCHY_ABI_V1_MAJOR, .minor = WATCHY_ABI_V1_MINOR},
        .size = sizeof(watchy_host_caps_v1_t),
    };
    watchy_package_session_t session;
    fake_loader_t loader = {.failure = FAIL_ON_LOAD};
    watchy_event_t event = {.type = WATCHY_EVENT_TICK, .data.tick_seconds = 1u};
    uint8_t pixel = 0xffu;
    watchy_canvas_t canvas = {
        .width = 1u, .height = 1u, .stride = 1u, .rotation = 0u,
        .format = WATCHY_PIXEL_MONO, .pixels = &pixel,
    };
    watchy_refresh_mode_t mode = WATCHY_REFRESH_FULL;
    watchy_package_loader_api_t api = fake_loader_api(&loader);

    CHECK(watchy_package_session_init(&session, &api, NULL) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_session_load(&session, "/package.so", &manifest, &host, false, false) ==
          WATCHY_PACKAGE_ERR_CALLBACK);
    CHECK(!loader.live_handle && !watchy_package_session_loaded(&session));

    loader.failure = FAIL_ON_START;
    CHECK(watchy_package_session_load(&session, "/package.so", &manifest, &host, false, false) ==
          WATCHY_PACKAGE_OK);
    CHECK(watchy_package_session_start(&session) == WATCHY_PACKAGE_ERR_CALLBACK);
    CHECK(!loader.live_handle && !watchy_package_session_loaded(&session));

    loader.failure = FAIL_ON_EVENT;
    CHECK(load_and_start_session(&session, &loader, &manifest, &host) == 0);
    CHECK(watchy_package_session_event(&session, &event) == WATCHY_PACKAGE_ERR_CALLBACK);
    CHECK(!loader.live_handle && !watchy_package_session_loaded(&session));

    loader.failure = FAIL_ON_RENDER;
    CHECK(load_and_start_session(&session, &loader, &manifest, &host) == 0);
    CHECK(watchy_package_session_render(&session, &canvas, &mode) == WATCHY_PACKAGE_ERR_CALLBACK);
    CHECK(!loader.live_handle && !watchy_package_session_loaded(&session));
    return 0;
}

static int test_session_enforces_one_handle_and_bypasses_safe_or_quarantined_packages(void) {
    watchy_package_manifest_t manifest = runtime_manifest();
    watchy_host_caps_v1_t host = {
        .abi = {.major = WATCHY_ABI_V1_MAJOR, .minor = WATCHY_ABI_V1_MINOR},
        .size = sizeof(watchy_host_caps_v1_t),
    };
    fake_loader_t loader = {0};
    watchy_package_loader_api_t api = fake_loader_api(&loader);
    watchy_package_session_t session;

    CHECK(watchy_package_session_init(&session, &api, NULL) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_session_load(&session, "/package.so", &manifest, &host, true, false) ==
          WATCHY_PACKAGE_ERR_SAFE_MODE);
    CHECK(!loader.live_handle);
    CHECK(watchy_package_session_load(&session, "/package.so", &manifest, &host, false, true) ==
          WATCHY_PACKAGE_ERR_QUARANTINED);
    CHECK(!loader.live_handle);
    CHECK(watchy_package_session_load(&session, "/package.so", &manifest, &host, false, false) ==
          WATCHY_PACKAGE_OK);
    CHECK(loader.live_handle);
    CHECK(watchy_package_session_load(&session, "/second.so", &manifest, &host, false, false) ==
          WATCHY_PACKAGE_ERR_STATE);
    CHECK(loader.live_handle);
    CHECK(watchy_package_session_stop(&session) == WATCHY_PACKAGE_OK);
    CHECK(!loader.live_handle && !watchy_package_session_loaded(&session));
    return 0;
}

typedef struct {
    bool stage_present;
    bool temp_present;
    bool final_present;
    bool unpacked_files_read_only;
} fake_package_fs_t;

static bool fake_fs_write(void *context,
                          const char *path,
                          const uint8_t *bytes,
                          size_t size,
                          bool read_only) {
    fake_package_fs_t *fs = (fake_package_fs_t *)context;
    if (path == NULL || (bytes == NULL && size != 0u)) {
        return false;
    }
    if (strncmp(path, "/data/staging/", strlen("/data/staging/")) == 0) {
        fs->stage_present = true;
    } else if (strncmp(path, "/data/packages/", strlen("/data/packages/")) == 0) {
        fs->temp_present = true;
        fs->unpacked_files_read_only = fs->unpacked_files_read_only && read_only;
    }
    return true;
}

static bool fake_fs_mkdirs(void *context, const char *path) {
    fake_package_fs_t *fs = (fake_package_fs_t *)context;
    if (strstr(path, "/data/packages/") != NULL) {
        fs->temp_present = true;
    }
    return true;
}

static bool fake_fs_sync_tree(void *context, const char *path) {
    fake_package_fs_t *fs = (fake_package_fs_t *)context;
    return path != NULL && (fs->stage_present || fs->temp_present || fs->final_present);
}

static bool fake_fs_rename(void *context, const char *source, const char *destination) {
    fake_package_fs_t *fs = (fake_package_fs_t *)context;
    if (!fs->temp_present || strstr(source, ".new") == NULL || strstr(destination, ".new") != NULL) {
        return false;
    }
    fs->temp_present = false;
    fs->final_present = true;
    return true;
}

static bool fake_fs_remove_tree(void *context, const char *path) {
    fake_package_fs_t *fs = (fake_package_fs_t *)context;
    if (strncmp(path, "/data/staging/", strlen("/data/staging/")) == 0) {
        fs->stage_present = false;
    } else if (strstr(path, ".new") != NULL) {
        fs->temp_present = false;
    } else if (strncmp(path, "/data/packages/", strlen("/data/packages/")) == 0) {
        fs->final_present = false;
    }
    return true;
}

static watchy_package_fs_api_t fake_fs_api(fake_package_fs_t *fs) {
    return (watchy_package_fs_api_t){
        .write_file = fake_fs_write,
        .mkdirs = fake_fs_mkdirs,
        .sync_tree = fake_fs_sync_tree,
        .rename_atomic = fake_fs_rename,
        .remove_tree = fake_fs_remove_tree,
        .context = fs,
    };
}

static int test_install_transaction_stages_validates_unpacks_and_rolls_back_nvs_failure(void) {
    uint8_t wpk[1024];
    uint8_t normalized[1024];
    digest_probe_t probe = {.normalized = normalized};
    watchy_crypto_api_t crypto = {.sha256 = digest_probe_sha256, .context = &probe};
    fake_index_store_t store = {0};
    watchy_package_index_store_t store_api = fake_store_api(&store);
    watchy_package_index_manager_t manager;
    fake_package_fs_t fs = {.unpacked_files_read_only = true};
    watchy_package_fs_api_t fs_api = fake_fs_api(&fs);
    char installed_ref[WATCHY_PACKAGE_REF_MAX + 1u];
    size_t wpk_size = make_valid_wpk(wpk, sizeof(wpk), &probe);

    CHECK(wpk_size != 0u);
    memcpy(normalized, wpk, wpk_size);
    memset(normalized + offsetof(wpk_header_t, package_sha256), 0, WATCHY_PACKAGE_DIGEST_SIZE);
    probe.normalized_size = wpk_size;
    CHECK(watchy_package_index_init(&manager, &store_api) == WATCHY_PACKAGE_OK);

    ((wpk_header_t *)wpk)->package_sha256[0] ^= 1u;
    CHECK(watchy_package_install(&manager, &fs_api, wpk, wpk_size, &crypto, installed_ref) ==
          WATCHY_PACKAGE_ERR_DIGEST);
    CHECK(!fs.stage_present && !fs.temp_present && !fs.final_present);
    CHECK(watchy_package_index_snapshot(&manager)->installed_count == 0u);
    ((wpk_header_t *)wpk)->package_sha256[0] ^= 1u;

    store.fail_save = true;
    CHECK(watchy_package_install(&manager, &fs_api, wpk, wpk_size, &crypto, installed_ref) ==
          WATCHY_PACKAGE_ERR_STORE);
    CHECK(!fs.stage_present && !fs.temp_present && !fs.final_present);
    CHECK(watchy_package_index_snapshot(&manager)->installed_count == 0u);

    store.fail_save = false;
    CHECK(watchy_package_install(&manager, &fs_api, wpk, wpk_size, &crypto, installed_ref) ==
          WATCHY_PACKAGE_OK);
    CHECK(strcmp(installed_ref, "clock.simple@1.2.3") == 0);
    CHECK(!fs.stage_present && !fs.temp_present && fs.final_present);
    CHECK(fs.unpacked_files_read_only);
    CHECK(watchy_package_index_snapshot(&manager)->installed_count == 1u);
    CHECK(strcmp(watchy_package_index_snapshot(&manager)->installed[0], installed_ref) == 0);
    return 0;
}

int main(void) {
    CHECK(test_package_id_accepts_only_bounded_lowercase_ascii() == 0);
    CHECK(test_package_digest_hashes_the_complete_wpk_with_zeroed_digest() == 0);
    CHECK(test_manifest_accepts_the_canonical_required_schema() == 0);
    CHECK(test_manifest_rejects_noncanonical_duplicate_or_unsafe_content() == 0);
    CHECK(test_elf_validator_accepts_only_sane_xtensa_shared_objects() == 0);
    CHECK(test_elf_validator_rejects_files_larger_than_384_kib() == 0);
    CHECK(test_complete_wpk_validation_rejects_asset_mismatch_and_trailing_data() == 0);
    CHECK(test_selection_is_transactional_when_persistence_fails() == 0);
    CHECK(test_pending_watchface_promotes_after_render_and_rolls_back_on_failure() == 0);
    CHECK(test_three_incomplete_attempts_quarantine_persistently_and_safe_mode_bypasses() == 0);
    CHECK(test_index_rejects_corrupt_persisted_counts_and_strings() == 0);
    CHECK(test_lifecycle_closes_the_handle_after_every_callback_failure() == 0);
    CHECK(test_session_enforces_one_handle_and_bypasses_safe_or_quarantined_packages() == 0);
    CHECK(test_install_transaction_stages_validates_unpacks_and_rolls_back_nvs_failure() == 0);
    puts("PASS 14 package tests");
    return 0;
}
