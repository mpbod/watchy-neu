#ifndef WATCHY_WPK_H
#define WATCHY_WPK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WPK_MAGIC "WPK1"
#define WPK_FORMAT_VERSION 1u

typedef enum {
    WPK_OK = 0,
    WPK_ERR_ARGUMENT = -1,
    WPK_ERR_TRUNCATED = -2,
    WPK_ERR_MAGIC = -3,
    WPK_ERR_VERSION = -4,
    WPK_ERR_LAYOUT = -5
} wpk_status_t;

typedef struct __attribute__((packed)) {
    uint8_t magic[4];
    uint16_t format_version;
    uint16_t header_size;
    uint32_t total_size;
    uint32_t manifest_offset;
    uint32_t manifest_size;
    uint32_t elf_offset;
    uint32_t elf_size;
    uint32_t assets_offset;
    uint32_t assets_size;
    uint8_t package_sha256[32];
} wpk_header_t;

#define WPK_HEADER_SIZE ((uint16_t)sizeof(wpk_header_t))

typedef struct {
    const wpk_header_t *header;
    const uint8_t *manifest;
    uint32_t manifest_size;
    const uint8_t *elf;
    uint32_t elf_size;
    const uint8_t *assets;
    uint32_t assets_size;
    const uint8_t *package_sha256;
} wpk_view_t;

wpk_status_t wpk_parse(const void *bytes, size_t size, wpk_view_t *out_view);

#ifdef __cplusplus
}
#endif

#endif
