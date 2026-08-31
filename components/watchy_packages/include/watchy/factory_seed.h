#ifndef WATCHY_FACTORY_SEED_H
#define WATCHY_FACTORY_SEED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "watchy/packages.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHY_FACTORY_SEED_VERSION 1u
#define WATCHY_FACTORY_SEED_COUNT 8u
#define WATCHY_FACTORY_SEED_HEADER_BYTES 8u
#define WATCHY_FACTORY_SEED_FILENAME_BYTES 32u
#define WATCHY_FACTORY_SEED_RECORD_BYTES \
    (WATCHY_FACTORY_SEED_FILENAME_BYTES + WATCHY_PACKAGE_DIGEST_SIZE)
#define WATCHY_FACTORY_SEED_CATALOG_BYTES \
    (WATCHY_FACTORY_SEED_HEADER_BYTES + \
     WATCHY_FACTORY_SEED_COUNT * WATCHY_FACTORY_SEED_RECORD_BYTES)

typedef struct {
    char filename[WATCHY_FACTORY_SEED_FILENAME_BYTES];
    uint8_t sha256[WATCHY_PACKAGE_DIGEST_SIZE];
} watchy_factory_seed_entry_t;

typedef struct {
    uint16_t version;
    uint16_t count;
    watchy_factory_seed_entry_t entries[WATCHY_FACTORY_SEED_COUNT];
} watchy_factory_seed_catalog_t;

typedef enum {
    WATCHY_FACTORY_SEED_MARKER_ERROR = -1,
    WATCHY_FACTORY_SEED_MARKER_NOT_FOUND = 0,
    WATCHY_FACTORY_SEED_MARKER_FOUND = 1,
} watchy_factory_seed_marker_result_t;

typedef enum {
    WATCHY_FACTORY_SEED_FILE_ERROR = -1,
    WATCHY_FACTORY_SEED_FILE_NOT_FOUND = 0,
    WATCHY_FACTORY_SEED_FILE_OK = 1,
} watchy_factory_seed_file_result_t;

typedef struct {
    watchy_factory_seed_marker_result_t (*read_marker)(void *context,
                                                       uint16_t *out_version);
    bool (*write_marker)(void *context, uint16_t version);
    watchy_factory_seed_file_result_t (*read_catalog)(void *context,
                                                      uint8_t *bytes,
                                                      size_t capacity,
                                                      size_t *out_size);
    watchy_factory_seed_file_result_t (*read_package)(void *context,
                                                      const char *filename,
                                                      uint8_t *bytes,
                                                      size_t capacity,
                                                      size_t *out_size);
    bool (*package_installed)(void *context, const char *filename);
    bool (*sha256)(void *context,
                   const uint8_t *bytes,
                   size_t size,
                   uint8_t out_digest[WATCHY_PACKAGE_DIGEST_SIZE]);
    watchy_package_status_t (*install_package)(void *context,
                                               uint8_t *bytes,
                                               size_t size);
    bool (*remove_package)(void *context, const char *filename);
    bool (*acquire_workspace)(void *context, uint8_t **out_bytes,
                              size_t *out_capacity);
    void (*release_workspace)(void *context, uint8_t *bytes);
    void *context;
} watchy_factory_seed_ops_t;

watchy_package_status_t watchy_factory_seed_parse(
    const uint8_t *bytes,
    size_t size,
    watchy_factory_seed_catalog_t *out_catalog);

watchy_package_status_t watchy_factory_seed_import(
    bool safe_mode,
    const watchy_factory_seed_ops_t *operations,
    uint8_t *catalog_wire,
    size_t catalog_capacity,
    watchy_factory_seed_catalog_t *catalog_workspace);

#ifdef __cplusplus
}
#endif

#endif
