#include "watchy/packages.h"

#include "watchy/wpk.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    const uint8_t *bytes;
    size_t size;
    size_t offset;
} manifest_cursor_t;

static bool utf8_valid(const uint8_t *bytes, size_t size) {
    size_t offset = 0u;

    while (offset < size) {
        const uint8_t first = bytes[offset++];
        if (first <= 0x7fu) {
            continue;
        }
        if (first >= 0xc2u && first <= 0xdfu) {
            if (offset >= size || (bytes[offset++] & 0xc0u) != 0x80u) {
                return false;
            }
            continue;
        }
        if (first >= 0xe0u && first <= 0xefu) {
            uint8_t second;
            if (size - offset < 2u) {
                return false;
            }
            second = bytes[offset++];
            if ((second & 0xc0u) != 0x80u ||
                (first == 0xe0u && second < 0xa0u) ||
                (first == 0xedu && second >= 0xa0u) ||
                (bytes[offset++] & 0xc0u) != 0x80u) {
                return false;
            }
            continue;
        }
        if (first >= 0xf0u && first <= 0xf4u) {
            uint8_t second;
            if (size - offset < 3u) {
                return false;
            }
            second = bytes[offset++];
            if ((second & 0xc0u) != 0x80u ||
                (first == 0xf0u && second < 0x90u) ||
                (first == 0xf4u && second >= 0x90u) ||
                (bytes[offset++] & 0xc0u) != 0x80u ||
                (bytes[offset++] & 0xc0u) != 0x80u) {
                return false;
            }
            continue;
        }
        return false;
    }
    return true;
}

static bool cursor_take(manifest_cursor_t *cursor, const char *literal) {
    const size_t length = strlen(literal);
    if (cursor->size - cursor->offset < length ||
        memcmp(cursor->bytes + cursor->offset, literal, length) != 0) {
        return false;
    }
    cursor->offset += length;
    return true;
}

static bool cursor_uint32(manifest_cursor_t *cursor, uint32_t *out_value) {
    uint32_t value = 0u;
    size_t digits = 0u;

    if (cursor->offset >= cursor->size ||
        cursor->bytes[cursor->offset] < '0' || cursor->bytes[cursor->offset] > '9') {
        return false;
    }
    if (cursor->bytes[cursor->offset] == '0' &&
        cursor->offset + 1u < cursor->size &&
        cursor->bytes[cursor->offset + 1u] >= '0' &&
        cursor->bytes[cursor->offset + 1u] <= '9') {
        return false;
    }
    while (cursor->offset < cursor->size &&
           cursor->bytes[cursor->offset] >= '0' && cursor->bytes[cursor->offset] <= '9') {
        const uint32_t digit = (uint32_t)(cursor->bytes[cursor->offset] - '0');
        if (value > (UINT32_MAX - digit) / 10u) {
            return false;
        }
        value = value * 10u + digit;
        ++cursor->offset;
        ++digits;
    }
    *out_value = value;
    return digits != 0u;
}

static bool cursor_string(manifest_cursor_t *cursor, char *out, size_t capacity) {
    size_t length = 0u;

    if (!cursor_take(cursor, "\"")) {
        return false;
    }
    while (cursor->offset < cursor->size) {
        uint8_t byte = cursor->bytes[cursor->offset++];
        if (byte == '"') {
            if (length >= capacity) {
                return false;
            }
            out[length] = '\0';
            return true;
        }
        if (byte < 0x20u) {
            return false;
        }
        if (byte == '\\') {
            if (cursor->offset >= cursor->size) {
                return false;
            }
            byte = cursor->bytes[cursor->offset++];
            if (byte != '"' && byte != '\\') {
                return false;
            }
        }
        if (length + 1u >= capacity) {
            return false;
        }
        out[length++] = (char)byte;
    }
    return false;
}

static bool paths_collide(const char *lhs, const char *rhs) {
    const size_t lhs_length = strlen(lhs);
    const size_t rhs_length = strlen(rhs);

    if (strcmp(lhs, rhs) == 0) {
        return true;
    }
    return (lhs_length < rhs_length && memcmp(lhs, rhs, lhs_length) == 0 && rhs[lhs_length] == '/') ||
           (rhs_length < lhs_length && memcmp(lhs, rhs, rhs_length) == 0 && lhs[rhs_length] == '/');
}

static bool asset_path_reserved(const char *path) {
    static const char *const reserved[] = {
        "package.so", "manifest.json", "state", ".new",
    };
    const char *separator = strchr(path, '/');
    const size_t first_length = separator == NULL ? strlen(path) : (size_t)(separator - path);

    if (first_length == 0u || path[0] == '.') {
        return true;
    }
    for (size_t index = 0u; index < sizeof(reserved) / sizeof(reserved[0]); ++index) {
        const size_t length = strlen(reserved[index]);
        if (first_length == length && memcmp(path, reserved[index], length) == 0) {
            return true;
        }
    }
    return first_length >= 7u && memcmp(path, "watchy-", 7u) == 0;
}

bool watchy_package_version_valid(const char *version) {
    const size_t length = version == NULL ? 0u : strnlen(version, WATCHY_PACKAGE_VERSION_MAX + 1u);
    if (length == 0u || length > WATCHY_PACKAGE_VERSION_MAX || version[0] == '.' ||
        strncmp(version, "watchy-", 7u) == 0) {
        return false;
    }
    for (size_t index = 0u; index < length; ++index) {
        const unsigned char byte = (unsigned char)version[index];
        if (byte < 0x21u || byte == 0x7fu || byte == '/' || byte == '\\' || byte == '@') {
            return false;
        }
    }
    return true;
}

bool watchy_package_id_valid(const char *identifier) {
    size_t length = 0;

    if (identifier == NULL) {
        return false;
    }
    while (identifier[length] != '\0') {
        const char character = identifier[length];
        const bool alpha = character >= 'a' && character <= 'z';
        const bool digit = character >= '0' && character <= '9';
        const bool punctuation = character == '.' || character == '_' || character == '-';

        if ((!alpha && !digit && !punctuation) || (length == 0u && !alpha && !digit)) {
            return false;
        }
        ++length;
        if (length > WATCHY_PACKAGE_ID_MAX) {
            return false;
        }
    }
    return length != 0u;
}

bool watchy_package_relative_path_valid(const char *path) {
    const char *segment;
    const char *cursor;
    size_t length;

    if (path == NULL) {
        return false;
    }
    length = strlen(path);
    if (length == 0u || length > WATCHY_PACKAGE_ASSET_PATH_MAX || path[0] == '/' ||
        path[length - 1u] == '/') {
        return false;
    }
    segment = path;
    for (cursor = path;; ++cursor) {
        const unsigned char byte = (unsigned char)*cursor;
        if (byte != '\0' && (byte == '\\' || byte < 0x20u || byte == 0x7fu)) {
            return false;
        }
        if (byte == '/' || byte == '\0') {
            const size_t segment_length = (size_t)(cursor - segment);
            if (segment_length == 0u ||
                (segment_length == 1u && segment[0] == '.') ||
                (segment_length == 2u && segment[0] == '.' && segment[1] == '.') ||
                (segment_length >= 8u && memcmp(segment, ".watchy-", 8u) == 0)) {
                return false;
            }
            if (byte == '\0') {
                return true;
            }
            segment = cursor + 1;
        }
    }
}

static watchy_package_status_t parse_assets(manifest_cursor_t *cursor,
                                            watchy_package_manifest_t *manifest) {
    uint32_t total_size = 0u;

    if (!cursor_take(cursor, "[")) {
        return WATCHY_PACKAGE_ERR_MANIFEST;
    }
    if (cursor_take(cursor, "]")) {
        return WATCHY_PACKAGE_OK;
    }
    for (;;) {
        watchy_package_asset_t *asset;
        if (manifest->asset_count >= WATCHY_PACKAGE_ASSET_COUNT_MAX) {
            return WATCHY_PACKAGE_ERR_LIMIT;
        }
        asset = &manifest->assets[manifest->asset_count];
        if (!cursor_take(cursor, "{\"path\":") ||
            !cursor_string(cursor, asset->path, sizeof(asset->path)) ||
            !cursor_take(cursor, ",\"size\":") ||
            !cursor_uint32(cursor, &asset->size) ||
            !cursor_take(cursor, "}")) {
            return WATCHY_PACKAGE_ERR_MANIFEST;
        }
        if (!watchy_package_relative_path_valid(asset->path) || asset_path_reserved(asset->path)) {
            return WATCHY_PACKAGE_ERR_PATH;
        }
        for (size_t prior = 0u; prior < manifest->asset_count; ++prior) {
            if (paths_collide(manifest->assets[prior].path, asset->path)) {
                return WATCHY_PACKAGE_ERR_COLLISION;
            }
        }
        if (asset->size > WATCHY_PACKAGE_ASSETS_BYTES_MAX - total_size) {
            return WATCHY_PACKAGE_ERR_LIMIT;
        }
        total_size += asset->size;
        ++manifest->asset_count;
        if (cursor_take(cursor, "]")) {
            return WATCHY_PACKAGE_OK;
        }
        if (!cursor_take(cursor, ",")) {
            return WATCHY_PACKAGE_ERR_MANIFEST;
        }
    }
}

watchy_package_status_t watchy_package_manifest_parse(const uint8_t *json,
                                                      size_t json_size,
                                                      watchy_package_manifest_t *out_manifest) {
    manifest_cursor_t cursor = {.bytes = json, .size = json_size, .offset = 0u};
    uint32_t integer = 0u;
    watchy_package_status_t status;
    char type[10];

    if (json == NULL || out_manifest == NULL || json_size == 0u) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    if (!utf8_valid(json, json_size)) {
        return WATCHY_PACKAGE_ERR_UTF8;
    }
    memset(out_manifest, 0, sizeof(*out_manifest));

    if (!cursor_take(&cursor, "{\"abi_major\":") ||
        !cursor_uint32(&cursor, &integer) || integer > UINT16_MAX) {
        return WATCHY_PACKAGE_ERR_MANIFEST;
    }
    out_manifest->abi_major = (uint16_t)integer;
    if (!cursor_take(&cursor, ",\"abi_minor\":") ||
        !cursor_uint32(&cursor, &integer) || integer > UINT16_MAX) {
        return WATCHY_PACKAGE_ERR_MANIFEST;
    }
    out_manifest->abi_minor = (uint16_t)integer;
    if (!cursor_take(&cursor, ",\"assets\":")) {
        return WATCHY_PACKAGE_ERR_MANIFEST;
    }
    status = parse_assets(&cursor, out_manifest);
    if (status != WATCHY_PACKAGE_OK) {
        return status;
    }
    if (!cursor_take(&cursor, ",\"capabilities\":") ||
        !cursor_uint32(&cursor, &out_manifest->capabilities)) {
        return WATCHY_PACKAGE_ERR_MANIFEST;
    }
    if ((out_manifest->capabilities & ~WATCHY_CAP_KNOWN_MASK) != 0u) {
        return WATCHY_PACKAGE_ERR_CAPABILITY;
    }
    if (!cursor_take(&cursor, ",\"id\":") ||
        !cursor_string(&cursor, out_manifest->id, sizeof(out_manifest->id))) {
        return WATCHY_PACKAGE_ERR_MANIFEST;
    }
    if (!watchy_package_id_valid(out_manifest->id)) {
        return WATCHY_PACKAGE_ERR_MANIFEST;
    }
    if (!cursor_take(&cursor, ",\"max_runtime_bytes\":") ||
        !cursor_uint32(&cursor, &out_manifest->max_runtime_bytes)) {
        return WATCHY_PACKAGE_ERR_MANIFEST;
    }
    if (out_manifest->max_runtime_bytes == 0u ||
        out_manifest->max_runtime_bytes > WATCHY_PACKAGE_RUNTIME_BYTES_MAX) {
        return WATCHY_PACKAGE_ERR_LIMIT;
    }
    if (!cursor_take(&cursor, ",\"name\":") ||
        !cursor_string(&cursor, out_manifest->name, sizeof(out_manifest->name)) ||
        out_manifest->name[0] == '\0' ||
        !cursor_take(&cursor, ",\"type\":") ||
        !cursor_string(&cursor, type, sizeof(type))) {
        return WATCHY_PACKAGE_ERR_MANIFEST;
    }
    if (strcmp(type, "watchface") == 0) {
        out_manifest->type = WATCHY_PACKAGE_TYPE_WATCHFACE;
    } else if (strcmp(type, "app") == 0) {
        out_manifest->type = WATCHY_PACKAGE_TYPE_APP;
    } else {
        return WATCHY_PACKAGE_ERR_MANIFEST;
    }
    if (!cursor_take(&cursor, ",\"version\":") ||
        !cursor_string(&cursor, out_manifest->version, sizeof(out_manifest->version)) ||
        out_manifest->version[0] == '\0' || !cursor_take(&cursor, "}") ||
        cursor.offset != cursor.size) {
        return WATCHY_PACKAGE_ERR_MANIFEST;
    }
    if (!watchy_package_version_valid(out_manifest->version)) {
        return WATCHY_PACKAGE_ERR_PATH;
    }

    return WATCHY_PACKAGE_OK;
}

watchy_package_status_t watchy_package_calculate_digest(
    const uint8_t *wpk,
    size_t wpk_size,
    const watchy_crypto_api_t *crypto,
    uint8_t out_digest[WATCHY_PACKAGE_DIGEST_SIZE]) {
    static const uint8_t zero_digest[WATCHY_PACKAGE_DIGEST_SIZE] = {0};
    const size_t digest_offset = offsetof(wpk_header_t, package_sha256);
    watchy_byte_region_t regions[3];

    if (wpk == NULL || crypto == NULL || crypto->sha256 == NULL || out_digest == NULL ||
        wpk_size < digest_offset + WATCHY_PACKAGE_DIGEST_SIZE) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }

    regions[0] = (watchy_byte_region_t){.bytes = wpk, .size = digest_offset};
    regions[1] = (watchy_byte_region_t){.bytes = zero_digest, .size = sizeof(zero_digest)};
    regions[2] = (watchy_byte_region_t){
        .bytes = wpk + digest_offset + WATCHY_PACKAGE_DIGEST_SIZE,
        .size = wpk_size - digest_offset - WATCHY_PACKAGE_DIGEST_SIZE,
    };
    if (!crypto->sha256(crypto->context, regions, 3u, out_digest)) {
        memset(out_digest, 0, WATCHY_PACKAGE_DIGEST_SIZE);
        return WATCHY_PACKAGE_ERR_CRYPTO;
    }
    return WATCHY_PACKAGE_OK;
}

watchy_package_status_t watchy_package_digest_matches(const uint8_t *wpk,
                                                      size_t wpk_size,
                                                      const watchy_crypto_api_t *crypto) {
    const size_t digest_offset = offsetof(wpk_header_t, package_sha256);
    uint8_t calculated[WATCHY_PACKAGE_DIGEST_SIZE];
    uint8_t difference = 0u;
    watchy_package_status_t status;

    if (wpk == NULL || wpk_size < digest_offset + WATCHY_PACKAGE_DIGEST_SIZE) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    status = watchy_package_calculate_digest(wpk, wpk_size, crypto, calculated);
    if (status != WATCHY_PACKAGE_OK) {
        return status;
    }
    for (size_t index = 0u; index < sizeof(calculated); ++index) {
        difference |= (uint8_t)(calculated[index] ^ wpk[digest_offset + index]);
    }
    return difference == 0u ? WATCHY_PACKAGE_OK : WATCHY_PACKAGE_ERR_DIGEST;
}
