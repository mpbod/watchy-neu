#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "watchy/packages.h"
#include "watchy/package_host.h"
#include "watchy/package_runtime.h"
#include "watchy/package_crypto.h"
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

static int test_software_sha256_matches_known_vector_across_regions(void) {
    static const uint8_t expected[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad,
    };
    const watchy_byte_region_t regions[] = {
        {.bytes = (const uint8_t *)"a", .size = 1u},
        {.bytes = (const uint8_t *)"bc", .size = 2u},
    };
    uint8_t digest[32];
    CHECK(watchy_package_sha256_regions(NULL, regions, 2u, digest));
    CHECK(memcmp(digest, expected, sizeof(expected)) == 0);
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
        "\"max_runtime_bytes\":81921,\"name\":\"A\",\"type\":\"app\",\"version\":\"1\"}";
    static const char excessive_assets[] =
        "{\"abi_major\":1,\"abi_minor\":0,\"assets\":[{\"path\":\"large.bin\",\"size\":32769}],"
        "\"capabilities\":0,\"id\":\"a\",\"max_runtime_bytes\":1,\"name\":\"A\","
        "\"type\":\"app\",\"version\":\"1\"}";
    static const char excessive_asset_aggregate[] =
        "{\"abi_major\":1,\"abi_minor\":0,\"assets\":[{\"path\":\"a\",\"size\":20000},"
        "{\"path\":\"b\",\"size\":12769}],\"capabilities\":0,\"id\":\"a\","
        "\"max_runtime_bytes\":1,\"name\":\"A\",\"type\":\"app\",\"version\":\"1\"}";
    static const char exact_limits[] =
        "{\"abi_major\":1,\"abi_minor\":1,\"assets\":[{\"path\":\"large.bin\",\"size\":32768}],"
        "\"capabilities\":0,\"id\":\"a\",\"max_runtime_bytes\":81920,\"name\":\"A\","
        "\"type\":\"app\",\"version\":\"1\"}";
    static const char traversal_version[] =
        "{\"abi_major\":1,\"abi_minor\":0,\"assets\":[],\"capabilities\":0,\"id\":\"a\","
        "\"max_runtime_bytes\":1,\"name\":\"A\",\"type\":\"app\",\"version\":\"../1\"}";
    static const uint8_t invalid_utf8[] =
        "{\"abi_major\":1,\"abi_minor\":0,\"assets\":[],\"capabilities\":0,\"id\":\"a\","
        "\"max_runtime_bytes\":1,\"name\":\"\xc0\x80\",\"type\":\"app\",\"version\":\"1\"}";
    static const char reserved_asset[] =
        "{\"abi_major\":1,\"abi_minor\":1,\"assets\":[{\"path\":\"package.so\",\"size\":1}],"
        "\"capabilities\":0,\"id\":\"a\",\"max_runtime_bytes\":1,\"name\":\"A\","
        "\"type\":\"app\",\"version\":\"1\"}";
    static const char reserved_prefix[] =
        "{\"abi_major\":1,\"abi_minor\":1,\"assets\":[{\"path\":\".watchy-tmp/x\",\"size\":1}],"
        "\"capabilities\":0,\"id\":\"a\",\"max_runtime_bytes\":1,\"name\":\"A\","
        "\"type\":\"app\",\"version\":\"1\"}";
    static const char non_profile_escape[] =
        "{\"abi_major\":1,\"abi_minor\":1,\"assets\":[],\"capabilities\":0,\"id\":\"a\","
        "\"max_runtime_bytes\":1,\"name\":\"line\\nfeed\",\"type\":\"app\",\"version\":\"1\"}";
    static const char reserved_version[] =
        "{\"abi_major\":1,\"abi_minor\":1,\"assets\":[],\"capabilities\":0,\"id\":\"a\","
        "\"max_runtime_bytes\":1,\"name\":\"A\",\"type\":\"app\","
        "\"version\":\".watchy-txn-1234abcd\"}";
    watchy_package_manifest_t manifest;

    CHECK(parse_manifest_text(noncanonical_space, &manifest) == WATCHY_PACKAGE_ERR_MANIFEST);
    CHECK(parse_manifest_text(duplicate_id, &manifest) == WATCHY_PACKAGE_ERR_MANIFEST);
    CHECK(parse_manifest_text(unknown_capability, &manifest) == WATCHY_PACKAGE_ERR_CAPABILITY);
    CHECK(parse_manifest_text(traversal, &manifest) == WATCHY_PACKAGE_ERR_PATH);
    CHECK(parse_manifest_text(collision, &manifest) == WATCHY_PACKAGE_ERR_COLLISION);
    CHECK(parse_manifest_text(excessive_runtime, &manifest) == WATCHY_PACKAGE_ERR_LIMIT);
    CHECK(parse_manifest_text(excessive_assets, &manifest) == WATCHY_PACKAGE_ERR_LIMIT);
    CHECK(parse_manifest_text(excessive_asset_aggregate, &manifest) == WATCHY_PACKAGE_ERR_LIMIT);
    CHECK(parse_manifest_text(exact_limits, &manifest) == WATCHY_PACKAGE_OK);
    CHECK(parse_manifest_text(traversal_version, &manifest) == WATCHY_PACKAGE_ERR_PATH);
    CHECK(watchy_package_manifest_parse(invalid_utf8, sizeof(invalid_utf8) - 1u, &manifest) ==
          WATCHY_PACKAGE_ERR_UTF8);
    CHECK(parse_manifest_text(reserved_asset, &manifest) == WATCHY_PACKAGE_ERR_PATH);
    CHECK(parse_manifest_text(reserved_prefix, &manifest) == WATCHY_PACKAGE_ERR_PATH);
    CHECK(parse_manifest_text(non_profile_escape, &manifest) == WATCHY_PACKAGE_ERR_MANIFEST);
    CHECK(parse_manifest_text(reserved_version, &manifest) == WATCHY_PACKAGE_ERR_PATH);
    CHECK(!watchy_package_relative_path_valid(".watchy-state-1234abcd.new"));
    CHECK(!watchy_package_relative_path_valid("dir/.watchy-state-1234abcd.new"));
    CHECK(watchy_package_relative_path_valid("1.new.0/data"));
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

#define ELF_FIXTURE_SIZE 444u
#define UNDEFINED_RELOCATION_ELF_SIZE 504u

static void make_valid_xtensa_elf(uint8_t bytes[ELF_FIXTURE_SIZE]) {
    static const char names[] = "\0.text\0.shstrtab\0.dynstr\0.dynsym\0.rela.text\0";
    static const char symbols[] = "\0watchy_package_entry\0";
    memset(bytes, 0, ELF_FIXTURE_SIZE);
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
    put_u32le(bytes, 24u, 0x1000u);
    put_u32le(bytes, 28u, 52u);
    put_u32le(bytes, 32u, 84u);
    put_u16le(bytes, 40u, 52u);
    put_u16le(bytes, 42u, 32u);
    put_u16le(bytes, 44u, 1u);
    put_u16le(bytes, 46u, 40u);
    put_u16le(bytes, 48u, 6u);
    put_u16le(bytes, 50u, 2u);

    put_u32le(bytes, 52u, 1u);
    put_u32le(bytes, 56u, 440u);
    put_u32le(bytes, 60u, 0x1000u);
    put_u32le(bytes, 68u, 4u);
    put_u32le(bytes, 72u, 4u);
    put_u32le(bytes, 76u, 5u);
    put_u32le(bytes, 80u, 4u);

    put_u32le(bytes, 124u, 1u);
    put_u32le(bytes, 128u, 1u);
    put_u32le(bytes, 132u, 6u);
    put_u32le(bytes, 136u, 0x1000u);
    put_u32le(bytes, 140u, 440u);
    put_u32le(bytes, 144u, 4u);
    put_u32le(bytes, 156u, 4u);

    put_u32le(bytes, 164u, 7u);
    put_u32le(bytes, 168u, 3u);
    put_u32le(bytes, 180u, 324u);
    put_u32le(bytes, 184u, (uint32_t)sizeof(names));
    put_u32le(bytes, 196u, 1u);
    put_u32le(bytes, 204u, 17u);
    put_u32le(bytes, 208u, 3u);
    put_u32le(bytes, 220u, 372u);
    put_u32le(bytes, 224u, (uint32_t)sizeof(symbols));
    put_u32le(bytes, 236u, 1u);

    put_u32le(bytes, 244u, 25u);
    put_u32le(bytes, 248u, 11u);
    put_u32le(bytes, 260u, 396u);
    put_u32le(bytes, 264u, 32u);
    put_u32le(bytes, 268u, 3u);
    put_u32le(bytes, 272u, 1u);
    put_u32le(bytes, 276u, 4u);
    put_u32le(bytes, 280u, 16u);

    put_u32le(bytes, 284u, 33u);
    put_u32le(bytes, 288u, 4u);
    put_u32le(bytes, 300u, 428u);
    put_u32le(bytes, 304u, 12u);
    put_u32le(bytes, 308u, 4u);
    put_u32le(bytes, 312u, 1u);
    put_u32le(bytes, 316u, 4u);
    put_u32le(bytes, 320u, 12u);

    memcpy(bytes + 324u, names, sizeof(names));
    memcpy(bytes + 372u, symbols, sizeof(symbols));
    put_u32le(bytes, 412u, 1u);
    put_u32le(bytes, 416u, 0x1000u);
    put_u32le(bytes, 420u, 4u);
    bytes[424] = 0x12u;
    put_u16le(bytes, 426u, 1u);
    put_u32le(bytes, 428u, 0x1000u);
    put_u32le(bytes, 432u, 5u);
    bytes[440] = 0x06u;
    bytes[441] = 0x01u;
}

static void make_undefined_relocation_xtensa_elf(
    uint8_t bytes[UNDEFINED_RELOCATION_ELF_SIZE]) {
    static const char symbols[] = "\0watchy_package_entry\0memset\0";
    make_valid_xtensa_elf(bytes);
    memset(bytes + ELF_FIXTURE_SIZE, 0, UNDEFINED_RELOCATION_ELF_SIZE - ELF_FIXTURE_SIZE);

    /* Move the text, dynamic strings, and relocation to make room for a third
     * dynamic symbol that is undefined and referenced by GLOB_DAT. */
    put_u32le(bytes, 56u, 500u);
    put_u32le(bytes, 140u, 500u);
    put_u32le(bytes, 220u, 444u);
    put_u32le(bytes, 224u, (uint32_t)sizeof(symbols));
    put_u32le(bytes, 264u, 48u);
    put_u32le(bytes, 300u, 480u);
    memcpy(bytes + 444u, symbols, sizeof(symbols));
    memset(bytes + 428u, 0, 16u);
    put_u32le(bytes, 428u, 22u);
    bytes[440u] = 0x10u;
    put_u32le(bytes, 480u, 0x1000u);
    put_u32le(bytes, 484u, (2u << 8u) | 3u);
    put_u32le(bytes, 488u, 0u);
    bytes[500u] = 0x06u;
    bytes[501u] = 0x01u;
}

static int test_elf_validator_accepts_only_sane_xtensa_shared_objects(void) {
    uint8_t elf[ELF_FIXTURE_SIZE];
    uint32_t runtime_bytes = 0u;

    make_valid_xtensa_elf(elf);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, &runtime_bytes) == WATCHY_PACKAGE_OK);
    /* 4 text + one target TLSF allocation for the 8-byte esp_symtab table
     * (12-byte minimum payload + 4-byte header) + one allocation for the
     * 21-byte entry name (24-byte aligned payload + 4-byte header). */
    CHECK(runtime_bytes == 48u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 48u, NULL) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 47u, NULL) ==
          WATCHY_PACKAGE_ERR_LIMIT);

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
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 3u, NULL) == WATCHY_PACKAGE_ERR_LIMIT);
    CHECK(watchy_package_elf_validate(elf,
                                      sizeof(elf),
                                      WATCHY_PACKAGE_RUNTIME_BYTES_MAX + 1u,
                                      NULL) == WATCHY_PACKAGE_ERR_LIMIT);
    return 0;
}

static int test_elf_validator_rejects_files_larger_than_64_kib(void) {
    static uint8_t oversized[(64u * 1024u) + 1u];
    CHECK(watchy_package_elf_validate(oversized, sizeof(oversized), 1u, NULL) ==
          WATCHY_PACKAGE_ERR_LIMIT);
    return 0;
}

static int test_elf_validator_rejects_relocations_to_undefined_symbols(void) {
    uint8_t elf[UNDEFINED_RELOCATION_ELF_SIZE];

    make_undefined_relocation_xtensa_elf(elf);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) ==
          WATCHY_PACKAGE_ERR_ELF);
    return 0;
}

static int test_elf_validator_rejects_loader_consumed_hostile_structures(void) {
    uint8_t elf[ELF_FIXTURE_SIZE];

    make_valid_xtensa_elf(elf);
    put_u16le(elf, 50u, 0u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    put_u32le(elf, 124u, UINT32_MAX);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    put_u32le(elf, 24u, 0x1004u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    put_u32le(elf, 268u, 0u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    put_u32le(elf, 280u, 8u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    put_u32le(elf, 308u, 0u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    put_u32le(elf, 312u, 0u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    put_u32le(elf, 320u, 8u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    put_u32le(elf, 412u, UINT32_MAX);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    put_u16le(elf, 426u, 6u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    put_u32le(elf, 432u, 0x201u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    put_u32le(elf, 432u, 6u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    put_u16le(elf, 426u, 0u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    put_u32le(elf, 212u, 2u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    put_u32le(elf, 220u, 324u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    put_u32le(elf, 136u, UINT32_MAX - 1u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    /* Reuse the relocation bytes as a second, non-overlapping .dynsym. The
     * loader would allocate and overwrite its persistent table twice. */
    make_valid_xtensa_elf(elf);
    put_u32le(elf, 284u, 25u);
    put_u32le(elf, 288u, 11u);
    put_u32le(elf, 300u, 428u);
    put_u32le(elf, 304u, 16u);
    put_u32le(elf, 308u, 3u);
    put_u32le(elf, 312u, 1u);
    put_u32le(elf, 316u, 4u);
    put_u32le(elf, 320u, 16u);
    memset(elf + 428u, 0, 16u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    make_valid_xtensa_elf(elf);
    put_u32le(elf, 284u, 17u);
    put_u32le(elf, 288u, 3u);
    put_u32le(elf, 300u, 428u);
    put_u32le(elf, 304u, 16u);
    put_u32le(elf, 316u, 1u);
    memset(elf + 428u, 0, 16u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) == WATCHY_PACKAGE_ERR_ELF);
    return 0;
}

static int test_elf_rejects_section_offset_beyond_its_load_file_range(void) {
    uint8_t elf[ELF_FIXTURE_SIZE + 4u];
    memset(elf, 0, sizeof(elf));
    make_valid_xtensa_elf(elf);
    /* The file contains the bytes, but the PT_LOAD file range ends at 88.
     * Subtracting file_end - sh_offset without the upper-bound check wraps. */
    put_u32le(elf, 56u, 84u);
    put_u32le(elf, 140u, ELF_FIXTURE_SIZE);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) ==
          WATCHY_PACKAGE_ERR_ELF);
    return 0;
}

#define MANY_FUNCTIONS 3000u
#define MANY_DYNSYM_OFFSET 348u
#define MANY_TEXT_OFFSET (MANY_DYNSYM_OFFSET + (MANY_FUNCTIONS + 1u) * 16u)
#define MANY_ELF_SIZE (MANY_TEXT_OFFSET + 4u)

static void make_many_symbol_xtensa_elf(uint8_t bytes[MANY_ELF_SIZE],
                                        uint32_t function_count,
                                        bool define_all) {
    static const char names[] = "\0.text\0.shstrtab\0.dynstr\0.dynsym\0";
    static const char symbols[] = "\0watchy_package_entry\0f\0";
    memset(bytes, 0, MANY_ELF_SIZE);
    memcpy(bytes, "\177ELF", 4u);
    bytes[4] = 1u;
    bytes[5] = 1u;
    bytes[6] = 1u;
    put_u16le(bytes, 16u, 3u);
    put_u16le(bytes, 18u, 94u);
    put_u32le(bytes, 20u, 1u);
    put_u32le(bytes, 24u, 0x1000u);
    put_u32le(bytes, 28u, 52u);
    put_u32le(bytes, 32u, 84u);
    put_u16le(bytes, 40u, 52u);
    put_u16le(bytes, 42u, 32u);
    put_u16le(bytes, 44u, 1u);
    put_u16le(bytes, 46u, 40u);
    put_u16le(bytes, 48u, 5u);
    put_u16le(bytes, 50u, 2u);
    put_u32le(bytes, 52u, 1u);
    put_u32le(bytes, 56u, MANY_TEXT_OFFSET);
    put_u32le(bytes, 60u, 0x1000u);
    put_u32le(bytes, 68u, 4u);
    put_u32le(bytes, 72u, 4u);
    put_u32le(bytes, 76u, 5u);
    put_u32le(bytes, 80u, 4u);
    put_u32le(bytes, 124u, 1u);
    put_u32le(bytes, 128u, 1u);
    put_u32le(bytes, 132u, 6u);
    put_u32le(bytes, 136u, 0x1000u);
    put_u32le(bytes, 140u, MANY_TEXT_OFFSET);
    put_u32le(bytes, 144u, 4u);
    put_u32le(bytes, 156u, 4u);
    put_u32le(bytes, 164u, 7u);
    put_u32le(bytes, 168u, 3u);
    put_u32le(bytes, 180u, 284u);
    put_u32le(bytes, 184u, (uint32_t)sizeof(names));
    put_u32le(bytes, 196u, 1u);
    put_u32le(bytes, 204u, 17u);
    put_u32le(bytes, 208u, 3u);
    put_u32le(bytes, 220u, 320u);
    put_u32le(bytes, 224u, (uint32_t)sizeof(symbols));
    put_u32le(bytes, 236u, 1u);
    put_u32le(bytes, 244u, 25u);
    put_u32le(bytes, 248u, 11u);
    put_u32le(bytes, 260u, MANY_DYNSYM_OFFSET);
    put_u32le(bytes, 264u, (function_count + 1u) * 16u);
    put_u32le(bytes, 268u, 3u);
    put_u32le(bytes, 272u, 1u);
    put_u32le(bytes, 276u, 4u);
    put_u32le(bytes, 280u, 16u);
    memcpy(bytes + 284u, names, sizeof(names));
    memcpy(bytes + 320u, symbols, sizeof(symbols));
    for (uint32_t index = 1u; index <= function_count; ++index) {
        const size_t offset = MANY_DYNSYM_OFFSET + (size_t)index * 16u;
        const bool defined = index == 1u || define_all;
        put_u32le(bytes, offset, defined ? 1u : 22u);
        put_u32le(bytes, offset + 4u, defined ? 0x1000u : 0u);
        put_u32le(bytes, offset + 8u, defined ? 4u : 0u);
        bytes[offset + 12u] = 0x12u;
        put_u16le(bytes, offset + 14u, defined ? 1u : 0u);
    }
    bytes[MANY_TEXT_OFFSET] = 0x06u;
    bytes[MANY_TEXT_OFFSET + 1u] = 0x01u;
}

static int test_elf_requires_exactly_the_literal_single_export(void) {
    uint8_t elf[ELF_FIXTURE_SIZE];

    make_valid_xtensa_elf(elf);
    elf[373u] = 'x';
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) ==
          WATCHY_PACKAGE_ERR_ELF);

    make_valid_xtensa_elf(elf);
    elf[424u] = 0x02u;
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 64u, NULL) ==
          WATCHY_PACKAGE_ERR_ELF);

    return 0;
}

static int test_elf_rejects_duplicate_and_thousands_of_defined_exports(void) {
    static uint8_t elf[MANY_ELF_SIZE];

    make_many_symbol_xtensa_elf(elf, 2u, true);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf),
                                      WATCHY_PACKAGE_RUNTIME_BYTES_MAX, NULL) ==
          WATCHY_PACKAGE_ERR_ELF);

    make_many_symbol_xtensa_elf(elf, MANY_FUNCTIONS, true);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf),
                                      WATCHY_PACKAGE_RUNTIME_BYTES_MAX, NULL) ==
          WATCHY_PACKAGE_ERR_ELF);
    return 0;
}

static int test_elf_runtime_accounts_for_thousands_of_undefined_global_functions(void) {
    static uint8_t elf[MANY_ELF_SIZE];
    uint32_t runtime_bytes = 0u;

    make_many_symbol_xtensa_elf(elf, MANY_FUNCTIONS, false);
    /* One defined package entry plus 2,999 undefined globals consumed by
     * elf_loader: 4 text + (24,000 table payload + 4 header) +
     * (24 entry-name payload + 4 header) + 2,999 * (12 + 4). */
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 72020u, &runtime_bytes) ==
          WATCHY_PACKAGE_OK);
    CHECK(runtime_bytes == 72020u);
    CHECK(watchy_package_elf_validate(elf, sizeof(elf), 72019u, NULL) ==
          WATCHY_PACKAGE_ERR_LIMIT);
    return 0;
}

static int test_reconciliation_distinguishes_valid_new_versions_from_transactions(void) {
    CHECK(watchy_package_version_valid("1.new.0"));
    CHECK(watchy_package_version_valid("release.new.beta"));
    CHECK(!watchy_package_version_valid(".watchy-txn-1234abcd"));
    CHECK(watchy_package_transaction_name_valid(".watchy-txn-1234abcd"));
    CHECK(!watchy_package_transaction_name_valid("1.new.0"));
    CHECK(!watchy_package_transaction_name_valid(".watchy-txn-1234abc"));
    CHECK(!watchy_package_transaction_name_valid(".watchy-txn-1234abcd-extra"));
    CHECK(watchy_package_reconcile_version("1.new.0", true) ==
          WATCHY_PACKAGE_RECONCILE_KEEP);
    CHECK(watchy_package_reconcile_version("1.new.0", false) ==
          WATCHY_PACKAGE_RECONCILE_REMOVE_UNINDEXED);
    CHECK(watchy_package_reconcile_version(".watchy-txn-1234abcd", false) ==
          WATCHY_PACKAGE_RECONCILE_REMOVE_TRANSACTION);
    CHECK(watchy_package_reconcile_version(".watchy-txn-bad", false) ==
          WATCHY_PACKAGE_RECONCILE_REMOVE_INVALID);
    return 0;
}

typedef struct {
    unsigned executions;
    unsigned rollbacks;
    watchy_async_status_t observed;
} async_probe_t;

static watchy_status_t execute_async(void *context, uint32_t operation) {
    async_probe_t *probe = (async_probe_t *)context;
    ++probe->executions;
    return operation <= 8u ? WATCHY_STATUS_OK : WATCHY_STATUS_INVALID_STATE;
}

static watchy_async_status_t observe_async(void *context, uint32_t operation) {
    (void)operation;
    return ((async_probe_t *)context)->observed;
}

static watchy_status_t rollback_async(void *context, uint32_t operation) {
    (void)operation;
    ++((async_probe_t *)context)->rollbacks;
    return WATCHY_STATUS_OK;
}

static int test_async_policy_waits_for_terminal_events_and_rolls_back_cancel_or_timeout(void) {
    watchy_package_async_slot_t slot = {0};
    watchy_request_id_t next = 0u;
    watchy_request_id_t request_id = 0u;
    watchy_async_status_t status;
    async_probe_t probe = {
        .observed = {.state = WATCHY_ASYNC_PENDING,
                     .result = WATCHY_STATUS_INVALID_STATE},
    };
    CHECK(watchy_package_async_begin(&slot, &next, 7u, 8u, &request_id) == WATCHY_STATUS_OK);
    CHECK(request_id == 1u);
    CHECK(watchy_package_async_pump_slot(&slot, 100u, 1000u, execute_async,
                                         observe_async, rollback_async, &probe) ==
          WATCHY_PACKAGE_OK);
    CHECK(watchy_package_async_status_slot(&slot, request_id, &status) == WATCHY_STATUS_OK);
    CHECK(status.state == WATCHY_ASYNC_PENDING);
    CHECK(probe.executions == 1u);
    probe.observed = (watchy_async_status_t){.state = WATCHY_ASYNC_FAILED,
                                             .result = WATCHY_STATUS_INVALID_STATE};
    CHECK(watchy_package_async_pump_slot(&slot, 200u, 1000u, execute_async,
                                         observe_async, rollback_async, &probe) ==
          WATCHY_PACKAGE_OK);
    watchy_package_async_slot_t completed = slot;
    watchy_request_id_t blocked_id = 0u;
    CHECK(watchy_package_async_begin(&completed, &next, 8u, 8u, &blocked_id) ==
          WATCHY_STATUS_INVALID_ARGUMENT);
    CHECK(watchy_package_async_status_slot(&slot, request_id, &status) == WATCHY_STATUS_OK);
    CHECK(status.state == WATCHY_ASYNC_FAILED && status.result == WATCHY_STATUS_INVALID_STATE);
    CHECK(probe.executions == 1u);

    probe.observed = (watchy_async_status_t){.state = WATCHY_ASYNC_PENDING,
                                             .result = WATCHY_STATUS_INVALID_STATE};
    CHECK(watchy_package_async_begin(&slot, &next, 8u, 8u, &request_id) == WATCHY_STATUS_OK);
    CHECK(watchy_package_async_pump_slot(&slot, 300u, 1000u, execute_async,
                                         observe_async, rollback_async, &probe) ==
          WATCHY_PACKAGE_OK);
    CHECK(watchy_package_async_cancel_slot(&slot, request_id, rollback_async, &probe) ==
          WATCHY_STATUS_OK);
    CHECK(watchy_package_async_status_slot(&slot, request_id, &status) == WATCHY_STATUS_OK);
    CHECK(status.state == WATCHY_ASYNC_CANCELLED);
    CHECK(probe.rollbacks == 1u);

    CHECK(watchy_package_async_begin(&slot, &next, 7u, 8u, &request_id) == WATCHY_STATUS_OK);
    CHECK(watchy_package_async_pump_slot(&slot, 500u, 1000u, execute_async,
                                         observe_async, rollback_async, &probe) ==
          WATCHY_PACKAGE_OK);
    CHECK(watchy_package_async_pump_slot(&slot, 1500u, 1000u, execute_async,
                                         observe_async, rollback_async, &probe) ==
          WATCHY_PACKAGE_OK);
    CHECK(watchy_package_async_status_slot(&slot, request_id, &status) == WATCHY_STATUS_OK);
    CHECK(status.state == WATCHY_ASYNC_FAILED);
    CHECK(probe.rollbacks == 2u);
    return 0;
}

static int test_runner_post_policy_requires_cleanup_for_exit_or_refresh_failure(void) {
    CHECK(watchy_package_post_action(true, false, true, false) == WATCHY_PACKAGE_POST_CONTINUE);
    CHECK(watchy_package_post_action(true, false, true, true) ==
          WATCHY_PACKAGE_POST_CLEAN_EXIT);
    CHECK(watchy_package_post_action(true, true, false, false) ==
          WATCHY_PACKAGE_POST_FAIL_CLEANUP);
    CHECK(watchy_package_post_action(false, false, true, false) ==
          WATCHY_PACKAGE_POST_FAIL_CLEANUP);
    return 0;
}

typedef struct {
    watchy_package_status_t start_status;
    watchy_package_status_t render_status;
    watchy_package_status_t stop_status;
    bool active_after_start;
    bool active;
    unsigned render_calls;
    unsigned stop_calls;
} watchface_cycle_probe_t;

static watchy_package_status_t cycle_start(void *context) {
    watchface_cycle_probe_t *probe = context;
    probe->active = probe->active_after_start;
    return probe->start_status;
}

static bool cycle_active(void *context) {
    return ((watchface_cycle_probe_t *)context)->active;
}

static watchy_package_status_t cycle_render(void *context) {
    watchface_cycle_probe_t *probe = context;
    ++probe->render_calls;
    return probe->render_status;
}

static watchy_package_status_t cycle_stop(void *context) {
    watchface_cycle_probe_t *probe = context;
    ++probe->stop_calls;
    probe->active = false;
    return probe->stop_status;
}

static int test_watchface_cycle_requires_a_successful_render_and_refresh(void) {
    watchface_cycle_probe_t probe = {
        .start_status = WATCHY_PACKAGE_OK,
        .render_status = WATCHY_PACKAGE_OK,
        .stop_status = WATCHY_PACKAGE_OK,
        .active_after_start = false,
    };
    const watchy_package_watchface_runner_t runner = {
        .start = cycle_start,
        .active = cycle_active,
        .render = cycle_render,
        .stop = cycle_stop,
        .context = &probe,
    };

    CHECK(!watchy_package_run_watchface_cycle(&runner));
    CHECK(probe.render_calls == 0u);
    CHECK(probe.stop_calls == 0u);

    probe.active_after_start = true;
    probe.active = false;
    CHECK(watchy_package_run_watchface_cycle(&runner));
    CHECK(probe.render_calls == 1u);
    CHECK(probe.stop_calls == 1u);

    probe.active = false;
    probe.render_status = WATCHY_PACKAGE_ERR_CALLBACK;
    CHECK(!watchy_package_run_watchface_cycle(&runner));
    CHECK(probe.render_calls == 2u);
    CHECK(probe.stop_calls == 2u);
    return 0;
}

static int test_upload_finalize_distinguishes_incomplete_content_from_storage_failures(void) {
    CHECK(watchy_package_upload_finalize_status(true, true, false, true, true) ==
          WATCHY_PACKAGE_ERR_WPK);
    CHECK(watchy_package_upload_finalize_status(true, true, true, false, true) ==
          WATCHY_PACKAGE_ERR_FILESYSTEM);
    CHECK(watchy_package_upload_finalize_status(true, true, true, true, false) ==
          WATCHY_PACKAGE_ERR_FILESYSTEM);
    CHECK(watchy_package_upload_finalize_status(true, true, true, true, true) ==
          WATCHY_PACKAGE_OK);
    return 0;
}

static int test_callback_budget_stops_repeated_sleeps_and_watchdog_feeds(void) {
    watchy_package_callback_budget_t budget = {0};
    watchy_package_callback_budget_begin(&budget, 100u);
    CHECK(watchy_package_callback_budget_reserve_sleep(&budget, 100u, 2000u));
    CHECK(watchy_package_callback_budget_reserve_sleep(&budget, 100u, 2000u));
    CHECK(!watchy_package_callback_budget_reserve_sleep(&budget, 100u, 1u));
    CHECK(watchy_package_callback_budget_may_feed(&budget, 4099u));
    CHECK(!watchy_package_callback_budget_may_feed(&budget, 4101u));
    watchy_package_callback_budget_end(&budget);
    CHECK(!watchy_package_callback_budget_reserve_sleep(&budget, 100u, 1u));
    CHECK(!watchy_package_callback_budget_may_feed(&budget, 100u));
    return 0;
}

static watchy_transition_request_v1_t valid_package_transition_request(void) {
    return (watchy_transition_request_v1_t){
        .size = sizeof(watchy_transition_request_v1_t),
        .effect = WATCHY_TRANSITION_ODOMETER,
        .direction = WATCHY_TRANSITION_DIRECTION_UP,
        .rect = {.x = 20, .y = 40, .width = 80, .height = 24},
        .flags = WATCHY_TRANSITION_HAS_RECT | WATCHY_TRANSITION_PREFER_FULL,
    };
}

static int test_transition_latch_copies_one_request_and_consumes_it_once(void) {
    watchy_package_transition_latch_t latch = {0};
    watchy_transition_request_v1_t request = valid_package_transition_request();
    const watchy_transition_request_v1_t expected = request;
    watchy_transition_request_v1_t taken = {0};

    CHECK(watchy_package_transition_latch(&latch, &request) == WATCHY_STATUS_OK);
    request.effect = WATCHY_TRANSITION_CUT;
    CHECK(watchy_package_transition_latch(&latch, &expected) == WATCHY_STATUS_BUSY);
    CHECK(watchy_package_transition_take(&latch, &taken));
    CHECK(memcmp(&taken, &expected, sizeof(taken)) == 0);
    CHECK(!watchy_package_transition_take(&latch, &taken));

    CHECK(watchy_package_transition_latch(&latch, &expected) == WATCHY_STATUS_OK);
    CHECK(watchy_package_transition_take(&latch, NULL));
    CHECK(!watchy_package_transition_take(&latch, &taken));
    return 0;
}

static int test_transition_latch_rejects_invalid_requests_without_occupying(void) {
    watchy_package_transition_latch_t latch = {0};
    const watchy_transition_request_v1_t valid = valid_package_transition_request();
    watchy_transition_request_v1_t invalid[] = {
        valid,
        valid,
        valid,
        valid,
        valid,
        valid,
        valid,
        valid,
        valid,
    };
    watchy_transition_request_v1_t taken;

    invalid[0].size -= 1u;
    invalid[1].effect = (watchy_transition_effect_t)(WATCHY_TRANSITION_SHUTTER + 1);
    invalid[2].direction = (watchy_transition_direction_t)(WATCHY_TRANSITION_DIRECTION_DOWN + 1);
    invalid[3].flags |= UINT32_C(0x80000000);
    invalid[4].reserved[1] = 1u;
    invalid[5].rect.width = 0;
    invalid[6].rect.x = 200;
    invalid[6].rect.width = 1;
    invalid[7].rect.y = 200;
    invalid[7].rect.height = 1;
    invalid[8].flags &= ~WATCHY_TRANSITION_HAS_RECT;

    CHECK(watchy_package_transition_latch(NULL, &valid) == WATCHY_STATUS_INVALID_ARGUMENT);
    CHECK(watchy_package_transition_latch(&latch, NULL) == WATCHY_STATUS_INVALID_ARGUMENT);
    CHECK(!watchy_package_transition_take(NULL, &taken));
    for (size_t index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        CHECK(watchy_package_transition_latch(&latch, &invalid[index]) ==
              WATCHY_STATUS_INVALID_ARGUMENT);
        CHECK(watchy_package_transition_latch(&latch, &valid) == WATCHY_STATUS_OK);
        CHECK(watchy_package_transition_take(&latch, &taken));
    }
    return 0;
}

typedef struct {
    const void *address;
    size_t size;
    unsigned calls;
    bool readable;
} transition_readable_probe_t;

static bool transition_request_readable(void *opaque, const void *address, size_t size) {
    transition_readable_probe_t *probe = opaque;
    probe->address = address;
    probe->size = size;
    ++probe->calls;
    return probe->readable;
}

static int test_transition_callback_admits_only_readable_active_requests(void) {
    watchy_package_host_context_t context = {0};
    watchy_transition_request_v1_t valid = valid_package_transition_request();
    watchy_transition_request_v1_t unreadable = {.size = 0u};
    transition_readable_probe_t probe = {0};

    watchy_package_transition_bind(&context, transition_request_readable, &probe);
    CHECK(context.system.context == &context);
    CHECK(context.system.request_transition != NULL);
    CHECK(context.system.request_transition(NULL, &valid) == WATCHY_STATUS_INVALID_STATE);
    CHECK(context.system.request_transition(context.system.context, &valid) ==
          WATCHY_STATUS_INVALID_STATE);
    CHECK(probe.calls == 0u);

    watchy_package_callback_budget_begin(&context.callback_budget, 10u);
    CHECK(context.system.request_transition(context.system.context, &unreadable) ==
          WATCHY_STATUS_INVALID_ARGUMENT);
    CHECK(probe.calls == 1u);
    CHECK(probe.address == &unreadable && probe.size == sizeof(unreadable));
    CHECK(!context.transition.occupied);

    probe.readable = true;
    CHECK(context.system.request_transition(context.system.context, &valid) ==
          WATCHY_STATUS_OK);
    CHECK(context.system.request_transition(context.system.context, &valid) ==
          WATCHY_STATUS_BUSY);
    CHECK(probe.calls == 3u);
    return 0;
}

typedef struct {
    watchy_status_t status[2];
    const watchy_transition_request_v1_t *request[2];
    watchy_transition_effect_t effect;
    watchy_refresh_mode_t mode[2];
    unsigned calls;
} transition_present_probe_t;

static watchy_status_t transition_present(void *opaque,
                                          watchy_refresh_mode_t mode,
                                          const watchy_transition_request_v1_t *request) {
    transition_present_probe_t *probe = opaque;
    const unsigned call = probe->calls++;
    CHECK(call < 2u);
    probe->request[call] = request;
    probe->mode[call] = mode;
    if (request != NULL) {
        probe->effect = request->effect;
    }
    return probe->status[call];
}

static int test_transition_failed_runner_and_deinit_cleanup_is_idempotent(void) {
    watchy_package_host_context_t context = {0};
    watchy_transition_request_v1_t valid = valid_package_transition_request();
    watchy_transition_request_v1_t taken;
    transition_present_probe_t probe = {0};

    CHECK(watchy_package_transition_latch(&context.transition, &valid) == WATCHY_STATUS_OK);
    CHECK(watchy_package_transition_present_after_render(
              &context.transition, false, WATCHY_REFRESH_PARTIAL,
              transition_present, &probe) ==
          WATCHY_STATUS_INVALID_STATE);
    CHECK(probe.calls == 0u);
    CHECK(!watchy_package_transition_take(&context.transition, &taken));

    /* A stop callback may latch again before runner cleanup reaches host deinit. */
    CHECK(watchy_package_transition_latch(&context.transition, &valid) == WATCHY_STATUS_OK);
    watchy_package_transition_cleanup(&context);
    watchy_package_transition_cleanup(&context);
    CHECK(!watchy_package_transition_take(&context.transition, &taken));
    watchy_package_transition_cleanup(NULL);
    return 0;
}

static int test_transition_present_consumes_once_and_only_falls_back_on_rejection(void) {
    watchy_package_transition_latch_t latch = {0};
    const watchy_transition_request_v1_t valid = valid_package_transition_request();
    transition_present_probe_t probe = {.status = {WATCHY_STATUS_OK, WATCHY_STATUS_OK}};

    CHECK(watchy_package_transition_latch(&latch, &valid) == WATCHY_STATUS_OK);
    CHECK(watchy_package_transition_present_after_render(
              &latch, true, WATCHY_REFRESH_FULL, transition_present, &probe) ==
          WATCHY_STATUS_OK);
    CHECK(probe.calls == 1u && probe.request[0] != NULL);
    CHECK(probe.effect == valid.effect && probe.mode[0] == WATCHY_REFRESH_FULL);

    probe = (transition_present_probe_t){.status = {WATCHY_STATUS_INVALID_ARGUMENT,
                                                    WATCHY_STATUS_OK}};
    CHECK(watchy_package_transition_latch(&latch, &valid) == WATCHY_STATUS_OK);
    CHECK(watchy_package_transition_present_after_render(
              &latch, true, WATCHY_REFRESH_PARTIAL, transition_present, &probe) ==
          WATCHY_STATUS_OK);
    CHECK(probe.calls == 2u);
    CHECK(probe.request[0] != NULL && probe.request[1] == NULL);

    probe = (transition_present_probe_t){.status = {WATCHY_STATUS_INVALID_STATE,
                                                    WATCHY_STATUS_OK}};
    CHECK(watchy_package_transition_latch(&latch, &valid) == WATCHY_STATUS_OK);
    CHECK(watchy_package_transition_present_after_render(
              &latch, true, WATCHY_REFRESH_PARTIAL, transition_present, &probe) ==
          WATCHY_STATUS_INVALID_STATE);
    CHECK(probe.calls == 1u);

    probe = (transition_present_probe_t){.status = {WATCHY_STATUS_INVALID_ARGUMENT,
                                                    WATCHY_STATUS_INVALID_STATE}};
    CHECK(watchy_package_transition_latch(&latch, &valid) == WATCHY_STATUS_OK);
    CHECK(watchy_package_transition_present_after_render(
              &latch, true, WATCHY_REFRESH_PARTIAL, transition_present, &probe) ==
          WATCHY_STATUS_INVALID_STATE);
    CHECK(probe.calls == 2u);
    return 0;
}

static bool ensure_watchdog(void *context) {
    return *(const bool *)context;
}

static int test_watchdog_enrollment_adapter_fails_closed(void) {
    bool enrolled = false;
    watchy_package_watchdog_api_t watchdog = {
        .ensure_current = ensure_watchdog,
        .context = &enrolled,
    };
    CHECK(watchy_package_watchdog_ensure_current(&watchdog) == WATCHY_PACKAGE_ERR_STATE);
    enrolled = true;
    CHECK(watchy_package_watchdog_ensure_current(&watchdog) == WATCHY_PACKAGE_OK);
    watchdog.ensure_current = NULL;
    CHECK(watchy_package_watchdog_ensure_current(&watchdog) == WATCHY_PACKAGE_ERR_STATE);
    return 0;
}

static int test_state_pointer_and_canvas_policies_fail_closed_at_boundaries(void) {
    uint8_t pixels[32] = {0};
    watchy_canvas_t bound = {
        .pixels = pixels, .width = 8u, .height = 8u, .stride = 4u,
        .rotation = 0u, .format = WATCHY_PIXEL_GRAY4,
    };
    watchy_canvas_t changed = bound;
    CHECK(watchy_package_state_quota_allows(16383u, 1u));
    CHECK(!watchy_package_state_quota_allows(16384u, 1u));
    CHECK(watchy_package_storage_node_allowed(WATCHY_PACKAGE_STORAGE_REGULAR, false));
    CHECK(watchy_package_storage_node_allowed(WATCHY_PACKAGE_STORAGE_DIRECTORY, true));
    CHECK(!watchy_package_storage_node_allowed(WATCHY_PACKAGE_STORAGE_DIRECTORY, false));
    CHECK(!watchy_package_storage_node_allowed(WATCHY_PACKAGE_STORAGE_LINK, true));
    CHECK(watchy_package_range_within(0x1000u, 0x100u, (const void *)0x1080u, 0x80u));
    CHECK(!watchy_package_range_within(0x1000u, 0x100u, (const void *)0x1080u, 0x81u));
    CHECK(!watchy_package_range_within(UINTPTR_MAX - 1u, 4u,
                                       (const void *)(UINTPTR_MAX - 1u), 1u));
    CHECK(watchy_package_canvas_binding_valid(&bound, &bound, sizeof(pixels)));
    changed.stride = 3u;
    CHECK(!watchy_package_canvas_binding_valid(&bound, &changed, sizeof(pixels)));
    CHECK(!watchy_package_canvas_binding_valid(&bound, &bound, sizeof(pixels) - 1u));
    CHECK(watchy_package_upload_heap_allows(
        WATCHY_PACKAGE_WPK_BYTES_MAX,
        WATCHY_PACKAGE_WPK_BYTES_MAX + WATCHY_PACKAGE_UPLOAD_HEAP_RESERVE_BYTES,
        WATCHY_PACKAGE_WPK_BYTES_MAX));
    CHECK(!watchy_package_upload_heap_allows(
        WATCHY_PACKAGE_WPK_BYTES_MAX,
        WATCHY_PACKAGE_WPK_BYTES_MAX + WATCHY_PACKAGE_UPLOAD_HEAP_RESERVE_BYTES - 1u,
        WATCHY_PACKAGE_WPK_BYTES_MAX));
    CHECK(!watchy_package_upload_heap_allows(
        WATCHY_PACKAGE_WPK_BYTES_MAX,
        WATCHY_PACKAGE_WPK_BYTES_MAX + WATCHY_PACKAGE_UPLOAD_HEAP_RESERVE_BYTES,
        WATCHY_PACKAGE_WPK_BYTES_MAX - 1u));

    CHECK(watchy_package_state_temporary_name_valid(".watchy-state-0123abcd"));
    CHECK(!watchy_package_state_temporary_name_valid(".watchy-state-0123abc"));
    CHECK(!watchy_package_state_temporary_name_valid(".watchy-state-0123ABCD"));
    CHECK(!watchy_package_state_temporary_name_valid("notes.watchy-state-0123abcd"));

    char resolved[192];
    CHECK(watchy_package_resolve_storage_path("/data/packages/sample/1", "/data/state/sample",
                                               "state/presses.bin", true,
                                               resolved, sizeof(resolved)));
    CHECK(strcmp(resolved, "/data/state/sample/presses.bin") == 0);
    CHECK(watchy_package_resolve_storage_path("/data/packages/sample/1", "/data/state/sample",
                                               "assets/probe.bin", false,
                                               resolved, sizeof(resolved)));
    CHECK(strcmp(resolved, "/data/packages/sample/1/probe.bin") == 0);
    CHECK(!watchy_package_resolve_storage_path("/data/packages/sample/1", "/data/state/sample",
                                                "presses.bin", true,
                                                resolved, sizeof(resolved)));
    return 0;
}

static int test_absolute_wpk_limit_is_80_kib_before_parsing(void) {
    static uint8_t exact[80u * 1024u];
    static uint8_t oversized[(80u * 1024u) + 1u];
    digest_probe_t probe = {0};
    watchy_crypto_api_t crypto = {.sha256 = digest_probe_sha256, .context = &probe};
    watchy_validated_package_t package;
    CHECK(watchy_package_validate(exact, sizeof(exact), &crypto, &package) ==
          WATCHY_PACKAGE_ERR_WPK);
    CHECK(watchy_package_validate(oversized, sizeof(oversized), &crypto, &package) ==
          WATCHY_PACKAGE_ERR_LIMIT);
    return 0;
}

static size_t make_valid_wpk(uint8_t *bytes, size_t capacity, digest_probe_t *probe) {
    static const char manifest_text[] =
        "{\"abi_major\":1,\"abi_minor\":0,\"assets\":[{\"path\":\"icon.bin\",\"size\":3}],"
        "\"capabilities\":515,\"id\":\"clock.simple\",\"max_runtime_bytes\":64,"
        "\"name\":\"Simple Clock\",\"type\":\"watchface\",\"version\":\"1.2.3\"}";
    const size_t manifest_size = sizeof(manifest_text) - 1u;
    const size_t total_size = WPK_HEADER_SIZE + manifest_size + ELF_FIXTURE_SIZE + 3u;
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
    header->elf_size = ELF_FIXTURE_SIZE;
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
    CHECK(package.runtime_bytes == 48u);
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

static int test_complete_wpk_validation_rejects_unsorted_assets(void) {
    static const char manifest_text[] =
        "{\"abi_major\":1,\"abi_minor\":1,\"assets\":[{\"path\":\"z.bin\",\"size\":1},"
        "{\"path\":\"a.bin\",\"size\":2}],\"capabilities\":1,\"id\":\"order.test\","
        "\"max_runtime_bytes\":64,\"name\":\"Order\",\"type\":\"app\",\"version\":\"1\"}";
    uint8_t bytes[1024];
    uint8_t normalized[1024];
    digest_probe_t probe = {.normalized = normalized};
    watchy_crypto_api_t crypto = {.sha256 = digest_probe_sha256, .context = &probe};
    watchy_validated_package_t package;
    wpk_header_t *header = (wpk_header_t *)bytes;
    const size_t manifest_size = sizeof(manifest_text) - 1u;
    const size_t total_size = WPK_HEADER_SIZE + manifest_size + ELF_FIXTURE_SIZE + 3u;

    memset(bytes, 0, total_size);
    memcpy(header->magic, WPK_MAGIC, sizeof(header->magic));
    header->format_version = WPK_FORMAT_VERSION;
    header->header_size = WPK_HEADER_SIZE;
    header->manifest_offset = WPK_HEADER_SIZE;
    header->manifest_size = (uint32_t)manifest_size;
    header->elf_offset = header->manifest_offset + header->manifest_size;
    header->elf_size = ELF_FIXTURE_SIZE;
    header->assets_offset = header->elf_offset + header->elf_size;
    header->assets_size = 3u;
    header->total_size = (uint32_t)total_size;
    memcpy(bytes + header->manifest_offset, manifest_text, manifest_size);
    make_valid_xtensa_elf(bytes + header->elf_offset);
    memcpy(bytes + header->assets_offset, "ZAA", 3u);
    for (size_t index = 0u; index < sizeof(probe.digest); ++index) {
        probe.digest[index] = (uint8_t)(0x80u + index);
    }
    memcpy(header->package_sha256, probe.digest, sizeof(probe.digest));
    memcpy(normalized, bytes, total_size);
    memset(normalized + offsetof(wpk_header_t, package_sha256), 0,
           WATCHY_PACKAGE_DIGEST_SIZE);
    probe.normalized_size = total_size;

    CHECK(watchy_package_validate(bytes, total_size, &crypto, &package) ==
          WATCHY_PACKAGE_ERR_MANIFEST);
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
    store.persisted.installed_count = 2u;
    strcpy(store.persisted.installed[0], "clock.old@1.0");
    strcpy(store.persisted.installed[1], "clock.new@2.0");
    store.persisted.installed_types[0] = WATCHY_PACKAGE_TYPE_WATCHFACE;
    store.persisted.installed_types[1] = WATCHY_PACKAGE_TYPE_WATCHFACE;
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

static int test_unregister_removes_health_and_all_selection_references_transactionally(void) {
    fake_index_store_t store = {0};
    watchy_package_index_store_t api = fake_store_api(&store);
    watchy_package_index_manager_t manager;
    const watchy_package_index_t *snapshot;

    CHECK(watchy_package_index_init(&manager, &api) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_register_installed(&manager, "clock.old@1") == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_register_installed(&manager, "clock.keep@2") == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_select_watchface(&manager, "clock.keep@2") == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_promote_pending(&manager, "clock.keep@2") == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_select_watchface(&manager, "clock.old@1") == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_begin_attempt(&manager, "clock.old@1", false) == WATCHY_PACKAGE_OK);

    store.fail_save = true;
    CHECK(watchy_package_unregister(&manager, "clock.old@1") == WATCHY_PACKAGE_ERR_STORE);
    CHECK(watchy_package_is_installed(&manager, "clock.old@1", NULL));

    store.fail_save = false;
    CHECK(watchy_package_unregister(&manager, "clock.old@1") == WATCHY_PACKAGE_OK);
    snapshot = watchy_package_index_snapshot(&manager);
    CHECK(!watchy_package_is_installed(&manager, "clock.old@1", NULL));
    CHECK(watchy_package_is_installed(&manager, "clock.keep@2", NULL));
    CHECK(strcmp(snapshot->active_watchface, "clock.keep@2") == 0);
    CHECK(snapshot->pending_watchface[0] == '\0');
    CHECK(snapshot->prior_watchface[0] == '\0');
    CHECK(snapshot->health_count == 0u);
    return 0;
}

static int test_empty_index_pruning_and_last_version_state_decisions_are_transactional(void) {
    fake_index_store_t store = {0};
    watchy_package_index_store_t api = fake_store_api(&store);
    watchy_package_index_manager_t manager;
    const watchy_package_index_t *snapshot;

    CHECK(watchy_package_index_init(&manager, &api) == WATCHY_PACKAGE_OK);
    CHECK(store.present);
    CHECK(watchy_package_register_installed(&manager, "clock.good@1") == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_register_installed(&manager, "clock.missing@1") == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_register_installed(&manager, "clock.good@2") == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_index_has_id(&manager, "clock.good"));
    CHECK(watchy_package_select_watchface(&manager, "clock.good@1") == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_promote_pending(&manager, "clock.good@1") == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_select_watchface(&manager, "clock.missing@1") == WATCHY_PACKAGE_OK);

    store.fail_save = true;
    CHECK(watchy_package_unregister(&manager, "clock.missing@1") == WATCHY_PACKAGE_ERR_STORE);
    snapshot = watchy_package_index_snapshot(&manager);
    CHECK(strcmp(snapshot->pending_watchface, "clock.missing@1") == 0);

    store.fail_save = false;
    CHECK(watchy_package_unregister(&manager, "clock.missing@1") == WATCHY_PACKAGE_OK);
    snapshot = watchy_package_index_snapshot(&manager);
    CHECK(strcmp(snapshot->active_watchface, "clock.good@1") == 0);
    CHECK(snapshot->pending_watchface[0] == '\0');
    CHECK(watchy_package_unregister(&manager, "clock.good@1") == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_index_has_id(&manager, "clock.good"));
    CHECK(watchy_package_unregister(&manager, "clock.good@2") == WATCHY_PACKAGE_OK);
    CHECK(!watchy_package_index_has_id(&manager, "clock.good"));

    CHECK(watchy_package_register_installed(&manager, "clock.temp@1") == WATCHY_PACKAGE_OK);
    store.fail_save = true;
    CHECK(watchy_package_index_clear(&manager) == WATCHY_PACKAGE_ERR_STORE);
    CHECK(watchy_package_is_installed(&manager, "clock.temp@1", NULL));
    store.fail_save = false;
    CHECK(watchy_package_index_clear(&manager) == WATCHY_PACKAGE_OK);
    snapshot = watchy_package_index_snapshot(&manager);
    CHECK(snapshot->installed_count == 0u && snapshot->health_count == 0u);
    CHECK(snapshot->active_watchface[0] == '\0' && snapshot->pending_watchface[0] == '\0');
    return 0;
}

static int test_pending_watchface_promotes_after_render_and_rolls_back_on_failure(void) {
    fake_index_store_t store = {0};
    watchy_package_index_store_t api = fake_store_api(&store);
    watchy_package_index_manager_t manager;
    const watchy_package_index_t *snapshot;

    CHECK(watchy_package_index_init(&manager, &api) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_register_installed(&manager, "clock.good@1.0") == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_register_installed(&manager, "clock.bad@1.0") == WATCHY_PACKAGE_OK);
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
    CHECK(watchy_package_register_installed(&manager, "clock.risky@1.0") == WATCHY_PACKAGE_OK);
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
    CHECK(watchy_package_register_installed(&reloaded, "clock.clean@1.0") == WATCHY_PACKAGE_OK);
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

static int test_index_wire_is_fixed_width_and_rejects_corruption_or_bad_selection(void) {
    static uint8_t wire[WATCHY_PACKAGE_INDEX_WIRE_MAX];
    static watchy_package_index_t decoded;
    fake_index_store_t store = {0};
    watchy_package_index_store_t api = fake_store_api(&store);
    watchy_package_index_manager_t manager;
    size_t wire_size = 0u;
    const size_t installed_offset = 12u + 3u * (WATCHY_PACKAGE_REF_MAX + 1u);
    const size_t health_offset = installed_offset +
        WATCHY_PACKAGE_INSTALLED_MAX * (2u + WATCHY_PACKAGE_REF_MAX + 1u);

    CHECK(watchy_package_index_init(&manager, &api) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_register_installed_typed(&manager, "face@1", WATCHY_PACKAGE_TYPE_WATCHFACE) ==
          WATCHY_PACKAGE_OK);
    CHECK(watchy_package_register_installed_typed(&manager, "app@1", WATCHY_PACKAGE_TYPE_APP) ==
          WATCHY_PACKAGE_OK);
    CHECK(watchy_package_select_watchface(&manager, "missing@1") == WATCHY_PACKAGE_ERR_STATE);
    CHECK(watchy_package_select_watchface(&manager, "app@1") == WATCHY_PACKAGE_ERR_STATE);
    CHECK(watchy_package_select_watchface(&manager, "face@1") == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_begin_attempt(&manager, "face@1", false) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_index_encode(watchy_package_index_snapshot(&manager),
                                      wire, sizeof(wire), &wire_size) == WATCHY_PACKAGE_OK);
    CHECK(wire_size > 0u && wire_size < sizeof(wire));
    CHECK(watchy_package_index_decode(wire, wire_size, &decoded) == WATCHY_PACKAGE_OK);
    CHECK(decoded.installed_count == 2u && decoded.installed_types[1] == WATCHY_PACKAGE_TYPE_APP);

    wire[installed_offset + 2u + strlen("face@1") + 1u] = 0x5au;
    CHECK(watchy_package_index_decode(wire, wire_size, &decoded) == WATCHY_PACKAGE_ERR_STORE);
    CHECK(watchy_package_index_encode(watchy_package_index_snapshot(&manager),
                                      wire, sizeof(wire), &wire_size) == WATCHY_PACKAGE_OK);

    wire[6] = 1u;
    CHECK(watchy_package_index_decode(wire, wire_size, &decoded) == WATCHY_PACKAGE_ERR_STORE);
    CHECK(watchy_package_index_encode(watchy_package_index_snapshot(&manager),
                                      wire, sizeof(wire), &wire_size) == WATCHY_PACKAGE_OK);
    wire[installed_offset] = 9u;
    CHECK(watchy_package_index_decode(wire, wire_size, &decoded) == WATCHY_PACKAGE_ERR_STORE);
    CHECK(watchy_package_index_encode(watchy_package_index_snapshot(&manager),
                                      wire, sizeof(wire), &wire_size) == WATCHY_PACKAGE_OK);
    wire[installed_offset + 1u] = 1u;
    CHECK(watchy_package_index_decode(wire, wire_size, &decoded) == WATCHY_PACKAGE_ERR_STORE);
    CHECK(watchy_package_index_encode(watchy_package_index_snapshot(&manager),
                                      wire, sizeof(wire), &wire_size) == WATCHY_PACKAGE_OK);
    wire[health_offset + 1u] = 2u;
    CHECK(watchy_package_index_decode(wire, wire_size, &decoded) == WATCHY_PACKAGE_ERR_STORE);
    return 0;
}

typedef enum {
    CALLBACKS_OK = 0,
    FAIL_ON_LOAD,
    FAIL_ON_START,
    FAIL_ON_EVENT,
    FAIL_ON_RENDER,
    MUTATE_RENDER_CANVAS,
} callback_failure_t;

typedef struct {
    bool live_handle;
    bool fail_close;
    bool fail_readable;
    int executable_calls;
    int fail_executable_call;
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
    if (loader->failure == MUTATE_RENDER_CANVAS) {
        canvas->pixels = NULL;
    }
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
    loader->executable_calls = 0;
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
    if (loader->fail_close) {
        return -1;
    }
    loader->live_handle = false;
    return 0;
}

static bool fake_readable(void *context, const void *address, size_t size) {
    fake_loader_t *loader = (fake_loader_t *)context;
    return !loader->fail_readable && address != NULL && size != 0u;
}

static bool fake_writable(void *context, void *address, size_t size) {
    (void)context;
    return address != NULL && size != 0u;
}

static bool fake_executable(void *context, const void *address, size_t size) {
    fake_loader_t *loader = (fake_loader_t *)context;
    ++loader->executable_calls;
    return address != NULL && size != 0u &&
           (loader->fail_executable_call == 0 ||
            loader->executable_calls != loader->fail_executable_call);
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
        .readable = fake_readable,
        .writable = fake_writable,
        .executable = fake_executable,
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

typedef struct {
    unsigned before_calls;
    unsigned after_calls;
    bool within_budget;
} watchdog_budget_probe_t;

static void budget_before(void *context) {
    ++((watchdog_budget_probe_t *)context)->before_calls;
}

static bool budget_after(void *context) {
    watchdog_budget_probe_t *probe = (watchdog_budget_probe_t *)context;
    ++probe->after_calls;
    return probe->within_budget;
}

static int test_returning_over_budget_callback_fails_and_closes_the_session(void) {
    watchy_package_manifest_t manifest = runtime_manifest();
    watchy_host_caps_v1_t host = {
        .abi = {.major = WATCHY_ABI_V1_MAJOR, .minor = WATCHY_ABI_V1_MINOR},
        .size = sizeof(watchy_host_caps_v1_t),
    };
    fake_loader_t loader = {0};
    watchy_package_loader_api_t api = fake_loader_api(&loader);
    watchy_package_session_t session = {0};
    watchdog_budget_probe_t probe = {.within_budget = false};
    watchy_package_watchdog_api_t watchdog = {
        .before_callback = budget_before,
        .after_callback = budget_after,
        .context = &probe,
    };
    CHECK(watchy_package_session_init(&session, &api, &watchdog) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_session_load(&session, "/package.so", &manifest, &host,
                                      false, false) == WATCHY_PACKAGE_ERR_CALLBACK);
    CHECK(probe.before_calls == 1u && probe.after_calls == 1u);
    CHECK(!loader.live_handle && !watchy_package_session_loaded(&session));
    return 0;
}

static int test_lifecycle_closes_the_handle_after_every_callback_failure(void) {
    watchy_package_manifest_t manifest = runtime_manifest();
    watchy_host_caps_v1_t host = {
        .abi = {.major = WATCHY_ABI_V1_MAJOR, .minor = WATCHY_ABI_V1_MINOR},
        .size = sizeof(watchy_host_caps_v1_t),
    };
    watchy_package_session_t session = {0};
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

    loader.failure = MUTATE_RENDER_CANVAS;
    canvas.pixels = &pixel;
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
    watchy_package_session_t session = {0};

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

static int test_session_global_owner_reinit_and_pointer_policy(void) {
    watchy_package_manifest_t manifest = runtime_manifest();
    watchy_host_caps_v1_t host = {
        .abi = {.major = WATCHY_ABI_V1_MAJOR, .minor = WATCHY_ABI_V1_MINOR},
        .size = sizeof(watchy_host_caps_v1_t),
    };
    fake_loader_t first_loader = {0};
    fake_loader_t second_loader = {0};
    watchy_package_loader_api_t first_api = fake_loader_api(&first_loader);
    watchy_package_loader_api_t second_api = fake_loader_api(&second_loader);
    watchy_package_session_t fresh;
    watchy_package_session_t first = {0};
    watchy_package_session_t second = {0};

    memset(&fresh, 0xa5, sizeof(fresh));
    CHECK(watchy_package_session_init(&fresh, &first_api, NULL) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_session_init(&first, &first_api, NULL) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_session_init(&second, &second_api, NULL) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_session_load(&first, "/one.so", &manifest, &host, false, false) ==
          WATCHY_PACKAGE_OK);
    CHECK(watchy_package_session_load(&second, "/two.so", &manifest, &host, false, false) ==
          WATCHY_PACKAGE_ERR_STATE);
    CHECK(watchy_package_session_init(&first, &first_api, NULL) == WATCHY_PACKAGE_ERR_STATE);
    CHECK(watchy_package_session_stop(&first) == WATCHY_PACKAGE_OK);

    second_loader.fail_readable = true;
    CHECK(watchy_package_session_load(&second, "/two.so", &manifest, &host, false, false) ==
          WATCHY_PACKAGE_ERR_DESCRIPTOR);
    CHECK(!second_loader.live_handle);
    second_loader.fail_readable = false;
    second_loader.fail_executable_call = 2;
    CHECK(watchy_package_session_load(&second, "/two.so", &manifest, &host, false, false) ==
          WATCHY_PACKAGE_ERR_DESCRIPTOR);
    CHECK(!second_loader.live_handle);
    return 0;
}

static int test_dlclose_failure_poison_keeps_global_owner(void) {
    watchy_package_manifest_t manifest = runtime_manifest();
    watchy_host_caps_v1_t host = {
        .abi = {.major = WATCHY_ABI_V1_MAJOR, .minor = WATCHY_ABI_V1_MINOR},
        .size = sizeof(watchy_host_caps_v1_t),
    };
    fake_loader_t poison_loader = {.fail_close = true};
    fake_loader_t blocked_loader = {0};
    watchy_package_loader_api_t poison_api = fake_loader_api(&poison_loader);
    watchy_package_loader_api_t blocked_api = fake_loader_api(&blocked_loader);
    watchy_package_session_t poison = {0};
    watchy_package_session_t blocked = {0};

    CHECK(watchy_package_session_init(&poison, &poison_api, NULL) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_session_init(&blocked, &blocked_api, NULL) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_session_load(&poison, "/poison.so", &manifest, &host, false, false) ==
          WATCHY_PACKAGE_OK);
    CHECK(watchy_package_session_start(&poison) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_session_stop(&poison) == WATCHY_PACKAGE_ERR_LOADER);
    CHECK(poison.poisoned && watchy_package_session_loaded(&poison));
    CHECK(watchy_package_session_init(&poison, &poison_api, NULL) == WATCHY_PACKAGE_ERR_STATE);
    CHECK(watchy_package_session_load(&blocked, "/blocked.so", &manifest, &host, false, false) ==
          WATCHY_PACKAGE_ERR_STATE);
    CHECK(!blocked_loader.live_handle);
    return 0;
}

typedef struct {
    bool stage_present;
    bool temp_present;
    bool final_present;
    bool unpacked_files_read_only;
    uint8_t stage_bytes[1024];
    size_t stage_size;
    uint32_t next_unique;
    unsigned operations;
    bool tamper_stage;
    bool transaction_name_seen;
    unsigned transaction_collisions;
    bool collision_path_present;
} fake_package_fs_t;

static const char *path_basename(const char *path) {
    const char *separator = path != NULL ? strrchr(path, '/') : NULL;
    return separator != NULL ? separator + 1u : path;
}

static bool fake_transaction_path(const char *path) {
    return watchy_package_transaction_name_valid(path_basename(path));
}

static bool fake_fs_write(void *context,
                          const char *path,
                          const uint8_t *bytes,
                          size_t size,
                          bool read_only) {
    fake_package_fs_t *fs = (fake_package_fs_t *)context;
    ++fs->operations;
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

static bool fake_fs_write_exclusive(void *context,
                                    const char *path,
                                    const uint8_t *bytes,
                                    size_t size) {
    fake_package_fs_t *fs = (fake_package_fs_t *)context;
    ++fs->operations;
    if (fs->stage_present || path == NULL || bytes == NULL || size > sizeof(fs->stage_bytes)) {
        return false;
    }
    memcpy(fs->stage_bytes, bytes, size);
    if (fs->tamper_stage && size != 0u) {
        fs->stage_bytes[size - 1u] ^= 0x80u;
    }
    fs->stage_size = size;
    fs->stage_present = true;
    return true;
}

static bool fake_fs_read(void *context,
                         const char *path,
                         uint8_t *bytes,
                         size_t capacity,
                         size_t *out_size) {
    fake_package_fs_t *fs = (fake_package_fs_t *)context;
    ++fs->operations;
    if (!fs->stage_present || path == NULL || bytes == NULL || out_size == NULL ||
        capacity < fs->stage_size) {
        return false;
    }
    memcpy(bytes, fs->stage_bytes, fs->stage_size);
    *out_size = fs->stage_size;
    return true;
}

static bool fake_fs_mkdirs(void *context, const char *path) {
    fake_package_fs_t *fs = (fake_package_fs_t *)context;
    ++fs->operations;
    (void)path;
    return true;
}

static bool fake_fs_mkdir_exclusive(void *context, const char *path) {
    fake_package_fs_t *fs = (fake_package_fs_t *)context;
    ++fs->operations;
    if (fs->temp_present || !fake_transaction_path(path)) {
        return false;
    }
    if (fs->transaction_collisions != 0u) {
        --fs->transaction_collisions;
        fs->collision_path_present = true;
        return false;
    }
    fs->temp_present = true;
    fs->transaction_name_seen = true;
    return true;
}

static bool fake_fs_sync_tree(void *context, const char *path) {
    fake_package_fs_t *fs = (fake_package_fs_t *)context;
    ++fs->operations;
    return path != NULL && (fs->stage_present || fs->temp_present || fs->final_present);
}

static bool fake_fs_rename(void *context, const char *source, const char *destination) {
    fake_package_fs_t *fs = (fake_package_fs_t *)context;
    ++fs->operations;
    if (!fs->temp_present || !fake_transaction_path(source) ||
        fake_transaction_path(destination) ||
        !watchy_package_version_valid(path_basename(destination))) {
        return false;
    }
    fs->temp_present = false;
    fs->final_present = true;
    return true;
}

static bool fake_fs_exists(void *context, const char *path) {
    fake_package_fs_t *fs = (fake_package_fs_t *)context;
    ++fs->operations;
    if (path != NULL && strncmp(path, "/data/staging/", strlen("/data/staging/")) == 0) {
        return fs->stage_present;
    }
    if (fake_transaction_path(path) && fs->collision_path_present) {
        fs->collision_path_present = false;
        return true;
    }
    return path != NULL && !fake_transaction_path(path) && fs->final_present;
}

static uint32_t fake_fs_unique(void *context) {
    fake_package_fs_t *fs = (fake_package_fs_t *)context;
    ++fs->operations;
    return ++fs->next_unique;
}

static bool fake_fs_remove_tree(void *context, const char *path) {
    fake_package_fs_t *fs = (fake_package_fs_t *)context;
    ++fs->operations;
    if (strncmp(path, "/data/staging/", strlen("/data/staging/")) == 0) {
        fs->stage_present = false;
    } else if (fake_transaction_path(path)) {
        fs->temp_present = false;
    } else if (strncmp(path, "/data/packages/", strlen("/data/packages/")) == 0) {
        fs->final_present = false;
    }
    return true;
}

static watchy_package_fs_api_t fake_fs_api(fake_package_fs_t *fs) {
    return (watchy_package_fs_api_t){
        .write_file = fake_fs_write,
        .write_file_exclusive = fake_fs_write_exclusive,
        .read_file = fake_fs_read,
        .mkdirs = fake_fs_mkdirs,
        .mkdir_exclusive = fake_fs_mkdir_exclusive,
        .sync_tree = fake_fs_sync_tree,
        .rename_noreplace = fake_fs_rename,
        .path_exists = fake_fs_exists,
        .remove_tree = fake_fs_remove_tree,
        .unique_id = fake_fs_unique,
        .context = fs,
    };
}

static int test_install_transaction_reuses_one_buffer_and_rolls_back_nvs_failure(void) {
    uint8_t wpk[1024];
    uint8_t normalized[1024];
    digest_probe_t probe = {.normalized = normalized};
    watchy_crypto_api_t crypto = {.sha256 = digest_probe_sha256, .context = &probe};
    fake_index_store_t store = {0};
    watchy_package_index_store_t store_api = fake_store_api(&store);
    watchy_package_index_manager_t manager;
    fake_package_fs_t fs = {
        .unpacked_files_read_only = true,
        .transaction_collisions = 1u,
    };
    watchy_package_fs_api_t fs_api = fake_fs_api(&fs);
    watchy_package_install_workspace_t workspace = {
        .stage_bytes = wpk,
        .stage_capacity = sizeof(wpk),
    };
    char installed_ref[WATCHY_PACKAGE_REF_MAX + 1u];
    size_t wpk_size = make_valid_wpk(wpk, sizeof(wpk), &probe);

    CHECK(wpk_size != 0u);
    memcpy(normalized, wpk, wpk_size);
    memset(normalized + offsetof(wpk_header_t, package_sha256), 0, WATCHY_PACKAGE_DIGEST_SIZE);
    probe.normalized_size = wpk_size;
    CHECK(watchy_package_index_init(&manager, &store_api) == WATCHY_PACKAGE_OK);

    ((wpk_header_t *)wpk)->package_sha256[0] ^= 1u;
    CHECK(watchy_package_install(&manager, &fs_api, &workspace,
                                 wpk, wpk_size, &crypto, installed_ref) ==
          WATCHY_PACKAGE_ERR_DIGEST);
    CHECK(!fs.stage_present && !fs.temp_present && !fs.final_present);
    CHECK(watchy_package_index_snapshot(&manager)->installed_count == 0u);
    ((wpk_header_t *)wpk)->package_sha256[0] ^= 1u;

    store.fail_save = true;
    CHECK(watchy_package_install(&manager, &fs_api, &workspace,
                                 wpk, wpk_size, &crypto, installed_ref) ==
          WATCHY_PACKAGE_ERR_STORE);
    CHECK(!fs.stage_present && !fs.temp_present && !fs.final_present);
    CHECK(watchy_package_index_snapshot(&manager)->installed_count == 0u);

    store.fail_save = false;
    CHECK(watchy_package_install(&manager, &fs_api, &workspace,
                                 wpk, wpk_size, &crypto, installed_ref) ==
          WATCHY_PACKAGE_OK);
    CHECK(strcmp(installed_ref, "clock.simple@1.2.3") == 0);
    CHECK(!fs.stage_present && !fs.temp_present && fs.final_present);
    CHECK(fs.transaction_name_seen);
    CHECK(fs.unpacked_files_read_only);
    CHECK(watchy_package_index_snapshot(&manager)->installed_count == 1u);
    CHECK(strcmp(watchy_package_index_snapshot(&manager)->installed[0], installed_ref) == 0);
    return 0;
}

static int test_install_duplicate_and_unindexed_final_are_never_deleted(void) {
    uint8_t wpk[1024];
    uint8_t normalized[1024];
    uint8_t stage_readback[1024];
    digest_probe_t probe = {.normalized = normalized};
    watchy_crypto_api_t crypto = {.sha256 = digest_probe_sha256, .context = &probe};
    fake_index_store_t store = {0};
    watchy_package_index_store_t store_api = fake_store_api(&store);
    watchy_package_index_manager_t manager;
    fake_package_fs_t fs = {.unpacked_files_read_only = true, .final_present = true};
    watchy_package_fs_api_t fs_api = fake_fs_api(&fs);
    watchy_package_install_workspace_t workspace = {
        .stage_bytes = stage_readback, .stage_capacity = sizeof(stage_readback),
    };
    char installed_ref[WATCHY_PACKAGE_REF_MAX + 1u];
    const size_t wpk_size = make_valid_wpk(wpk, sizeof(wpk), &probe);

    CHECK(wpk_size != 0u);
    memcpy(normalized, wpk, wpk_size);
    memset(normalized + offsetof(wpk_header_t, package_sha256), 0, WATCHY_PACKAGE_DIGEST_SIZE);
    probe.normalized_size = wpk_size;
    CHECK(watchy_package_index_init(&manager, &store_api) == WATCHY_PACKAGE_OK);

    fs.final_present = false;
    fs.stage_present = true;
    CHECK(watchy_package_install(&manager, &fs_api, &workspace,
                                 wpk, wpk_size, &crypto, installed_ref) ==
          WATCHY_PACKAGE_ERR_FILESYSTEM);
    CHECK(fs.stage_present && !fs.temp_present && !fs.final_present);
    fs.stage_present = false;
    fs.final_present = true;

    CHECK(watchy_package_install(&manager, &fs_api, &workspace,
                                 wpk, wpk_size, &crypto, installed_ref) ==
          WATCHY_PACKAGE_ERR_STATE);
    CHECK(fs.final_present && !fs.stage_present && !fs.temp_present);

    fs.final_present = false;
    CHECK(watchy_package_register_installed(&manager, "clock.simple@1.2.3") == WATCHY_PACKAGE_OK);
    fs.final_present = true;
    fs.operations = 0u;
    CHECK(watchy_package_install(&manager, &fs_api, &workspace,
                                 wpk, wpk_size, &crypto, installed_ref) ==
          WATCHY_PACKAGE_ERR_STATE);
    CHECK(fs.operations == 0u && fs.final_present);

    fs.final_present = false;
    fs.operations = 0u;
    CHECK(watchy_package_install(&manager, &fs_api, &workspace,
                                 wpk, WATCHY_PACKAGE_WPK_BYTES_MAX + 1u,
                                 &crypto, installed_ref) == WATCHY_PACKAGE_ERR_LIMIT);
    CHECK(fs.operations == 0u);
    return 0;
}

static int test_install_validates_the_exclusive_stage_readback(void) {
    uint8_t wpk[1024];
    uint8_t normalized[1024];
    uint8_t stage_readback[1024];
    digest_probe_t probe = {.normalized = normalized};
    watchy_crypto_api_t crypto = {.sha256 = digest_probe_sha256, .context = &probe};
    fake_index_store_t store = {0};
    watchy_package_index_store_t store_api = fake_store_api(&store);
    watchy_package_index_manager_t manager;
    fake_package_fs_t fs = {.unpacked_files_read_only = true, .tamper_stage = true};
    watchy_package_fs_api_t fs_api = fake_fs_api(&fs);
    watchy_package_install_workspace_t workspace = {
        .stage_bytes = stage_readback, .stage_capacity = sizeof(stage_readback),
    };
    char installed_ref[WATCHY_PACKAGE_REF_MAX + 1u];
    const size_t wpk_size = make_valid_wpk(wpk, sizeof(wpk), &probe);

    CHECK(wpk_size != 0u);
    memcpy(normalized, wpk, wpk_size);
    memset(normalized + offsetof(wpk_header_t, package_sha256), 0, WATCHY_PACKAGE_DIGEST_SIZE);
    probe.normalized_size = wpk_size;
    CHECK(watchy_package_index_init(&manager, &store_api) == WATCHY_PACKAGE_OK);
    CHECK(watchy_package_install(&manager, &fs_api, &workspace,
                                 wpk, wpk_size, &crypto, installed_ref) ==
          WATCHY_PACKAGE_ERR_CRYPTO);
    CHECK(!fs.stage_present && !fs.temp_present && !fs.final_present);
    return 0;
}

int main(void) {
    CHECK(test_package_id_accepts_only_bounded_lowercase_ascii() == 0);
    CHECK(test_package_digest_hashes_the_complete_wpk_with_zeroed_digest() == 0);
    CHECK(test_software_sha256_matches_known_vector_across_regions() == 0);
    CHECK(test_manifest_accepts_the_canonical_required_schema() == 0);
    CHECK(test_manifest_rejects_noncanonical_duplicate_or_unsafe_content() == 0);
    CHECK(test_elf_validator_accepts_only_sane_xtensa_shared_objects() == 0);
    CHECK(test_elf_validator_rejects_files_larger_than_64_kib() == 0);
    CHECK(test_elf_validator_rejects_relocations_to_undefined_symbols() == 0);
    CHECK(test_elf_validator_rejects_loader_consumed_hostile_structures() == 0);
    CHECK(test_elf_rejects_section_offset_beyond_its_load_file_range() == 0);
    CHECK(test_elf_requires_exactly_the_literal_single_export() == 0);
    CHECK(test_elf_rejects_duplicate_and_thousands_of_defined_exports() == 0);
    CHECK(test_elf_runtime_accounts_for_thousands_of_undefined_global_functions() == 0);
    CHECK(test_reconciliation_distinguishes_valid_new_versions_from_transactions() == 0);
    CHECK(test_async_policy_waits_for_terminal_events_and_rolls_back_cancel_or_timeout() == 0);
    CHECK(test_runner_post_policy_requires_cleanup_for_exit_or_refresh_failure() == 0);
    CHECK(test_watchface_cycle_requires_a_successful_render_and_refresh() == 0);
    CHECK(test_upload_finalize_distinguishes_incomplete_content_from_storage_failures() == 0);
    CHECK(test_callback_budget_stops_repeated_sleeps_and_watchdog_feeds() == 0);
    CHECK(test_transition_latch_copies_one_request_and_consumes_it_once() == 0);
    CHECK(test_transition_latch_rejects_invalid_requests_without_occupying() == 0);
    CHECK(test_transition_callback_admits_only_readable_active_requests() == 0);
    CHECK(test_transition_failed_runner_and_deinit_cleanup_is_idempotent() == 0);
    CHECK(test_transition_present_consumes_once_and_only_falls_back_on_rejection() == 0);
    CHECK(test_watchdog_enrollment_adapter_fails_closed() == 0);
    CHECK(test_state_pointer_and_canvas_policies_fail_closed_at_boundaries() == 0);
    CHECK(test_absolute_wpk_limit_is_80_kib_before_parsing() == 0);
    CHECK(test_complete_wpk_validation_rejects_asset_mismatch_and_trailing_data() == 0);
    CHECK(test_complete_wpk_validation_rejects_unsorted_assets() == 0);
    CHECK(test_selection_is_transactional_when_persistence_fails() == 0);
    CHECK(test_unregister_removes_health_and_all_selection_references_transactionally() == 0);
    CHECK(test_empty_index_pruning_and_last_version_state_decisions_are_transactional() == 0);
    CHECK(test_pending_watchface_promotes_after_render_and_rolls_back_on_failure() == 0);
    CHECK(test_three_incomplete_attempts_quarantine_persistently_and_safe_mode_bypasses() == 0);
    CHECK(test_index_rejects_corrupt_persisted_counts_and_strings() == 0);
    CHECK(test_index_wire_is_fixed_width_and_rejects_corruption_or_bad_selection() == 0);
    CHECK(test_lifecycle_closes_the_handle_after_every_callback_failure() == 0);
    CHECK(test_returning_over_budget_callback_fails_and_closes_the_session() == 0);
    CHECK(test_session_enforces_one_handle_and_bypasses_safe_or_quarantined_packages() == 0);
    CHECK(test_session_global_owner_reinit_and_pointer_policy() == 0);
    CHECK(test_install_transaction_reuses_one_buffer_and_rolls_back_nvs_failure() == 0);
    CHECK(test_install_duplicate_and_unindexed_final_are_never_deleted() == 0);
    CHECK(test_install_validates_the_exclusive_stage_readback() == 0);
    CHECK(test_dlclose_failure_poison_keeps_global_owner() == 0);
    puts("PASS 45 package tests");
    return 0;
}
