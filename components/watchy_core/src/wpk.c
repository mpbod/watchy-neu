#include "watchy/wpk.h"

#include <limits.h>
#include <string.h>

static int add_overflows_u32(uint32_t lhs, uint32_t rhs, uint32_t *sum) {
    if (UINT32_MAX - lhs < rhs) {
        return 1;
    }
    *sum = lhs + rhs;
    return 0;
}

static wpk_status_t validate_section(uint32_t offset,
                                     uint32_t size,
                                     uint32_t expected_offset,
                                     uint32_t *next_offset) {
    uint32_t end = 0;

    if (offset != expected_offset) {
        return WPK_ERR_LAYOUT;
    }
    if (add_overflows_u32(offset, size, &end)) {
        return WPK_ERR_LAYOUT;
    }

    *next_offset = end;
    return WPK_OK;
}

wpk_status_t wpk_parse(const void *bytes, size_t size, wpk_view_t *out_view) {
    const uint8_t *raw = (const uint8_t *)bytes;
    wpk_header_t header = {0};
    uint32_t next_offset = 0;
    wpk_status_t status = WPK_OK;

    if (raw == NULL || out_view == NULL) {
        return WPK_ERR_ARGUMENT;
    }
    if (size < sizeof(header)) {
        return WPK_ERR_TRUNCATED;
    }

    memcpy(&header, raw, sizeof(header));
    if (memcmp(header.magic, WPK_MAGIC, sizeof(header.magic)) != 0) {
        return WPK_ERR_MAGIC;
    }
    if (header.format_version != WPK_FORMAT_VERSION) {
        return WPK_ERR_VERSION;
    }
    if (header.header_size != WPK_HEADER_SIZE) {
        return WPK_ERR_LAYOUT;
    }
    if (header.total_size < header.header_size) {
        return WPK_ERR_LAYOUT;
    }
    if ((size_t)header.header_size > size || (size_t)header.total_size > size) {
        return WPK_ERR_TRUNCATED;
    }
    if ((size_t)header.total_size != size) {
        return WPK_ERR_LAYOUT;
    }

    status = validate_section(header.manifest_offset, header.manifest_size, header.header_size, &next_offset);
    if (status != WPK_OK) {
        return status;
    }
    status = validate_section(header.elf_offset, header.elf_size, next_offset, &next_offset);
    if (status != WPK_OK) {
        return status;
    }
    status = validate_section(header.assets_offset, header.assets_size, next_offset, &next_offset);
    if (status != WPK_OK) {
        return status;
    }
    if (header.total_size != next_offset) {
        return WPK_ERR_LAYOUT;
    }

    memset(out_view, 0, sizeof(*out_view));
    out_view->header = (const wpk_header_t *)raw;
    out_view->manifest = raw + header.manifest_offset;
    out_view->manifest_size = header.manifest_size;
    out_view->elf = raw + header.elf_offset;
    out_view->elf_size = header.elf_size;
    out_view->assets = raw + header.assets_offset;
    out_view->assets_size = header.assets_size;
    out_view->package_sha256 = ((const wpk_header_t *)raw)->package_sha256;
    return WPK_OK;
}
