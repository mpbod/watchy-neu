#include "watchy/factory_seed.h"

#include <string.h>

static uint16_t get_u16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u));
}

static bool filename_byte_valid(uint8_t byte) {
    return (byte >= (uint8_t)'a' && byte <= (uint8_t)'z') ||
           (byte >= (uint8_t)'0' && byte <= (uint8_t)'9') ||
           byte == (uint8_t)'-' || byte == (uint8_t)'_' || byte == (uint8_t)'.';
}

static bool decode_filename(const uint8_t wire[WATCHY_FACTORY_SEED_FILENAME_BYTES],
                            char output[WATCHY_FACTORY_SEED_FILENAME_BYTES]) {
    const uint8_t *terminator = memchr(wire, '\0', WATCHY_FACTORY_SEED_FILENAME_BYTES);
    if (terminator == NULL || terminator == wire || wire[0] == (uint8_t)'.') {
        return false;
    }
    const size_t length = (size_t)(terminator - wire);
    if (length < 5u || memcmp(wire + length - 4u, ".wpk", 4u) != 0) {
        return false;
    }
    for (size_t index = 0u; index < length; ++index) {
        if (!filename_byte_valid(wire[index])) return false;
    }
    for (size_t index = length; index < WATCHY_FACTORY_SEED_FILENAME_BYTES; ++index) {
        if (wire[index] != 0u) return false;
    }
    memcpy(output, wire, length);
    output[length] = '\0';
    return true;
}

watchy_package_status_t watchy_factory_seed_parse(
    const uint8_t *bytes,
    size_t size,
    watchy_factory_seed_catalog_t *out_catalog) {
    watchy_factory_seed_catalog_t parsed;
    if (bytes == NULL || out_catalog == NULL) return WATCHY_PACKAGE_ERR_ARGUMENT;
    memset(out_catalog, 0, sizeof(*out_catalog));
    if (size != WATCHY_FACTORY_SEED_CATALOG_BYTES || memcmp(bytes, "WFS1", 4u) != 0 ||
        get_u16(bytes + 4u) != WATCHY_FACTORY_SEED_VERSION ||
        get_u16(bytes + 6u) != WATCHY_FACTORY_SEED_COUNT) {
        return WATCHY_PACKAGE_ERR_WPK;
    }
    memset(&parsed, 0, sizeof(parsed));
    parsed.version = get_u16(bytes + 4u);
    parsed.count = get_u16(bytes + 6u);
    for (size_t index = 0u; index < WATCHY_FACTORY_SEED_COUNT; ++index) {
        const uint8_t *record = bytes + WATCHY_FACTORY_SEED_HEADER_BYTES +
                                index * WATCHY_FACTORY_SEED_RECORD_BYTES;
        if (!decode_filename(record, parsed.entries[index].filename) ||
            (index != 0u && strcmp(parsed.entries[index - 1u].filename,
                                   parsed.entries[index].filename) >= 0)) {
            return WATCHY_PACKAGE_ERR_PATH;
        }
        memcpy(parsed.entries[index].sha256,
               record + WATCHY_FACTORY_SEED_FILENAME_BYTES,
               WATCHY_PACKAGE_DIGEST_SIZE);
    }
    memcpy(out_catalog, &parsed, sizeof(parsed));
    return WATCHY_PACKAGE_OK;
}

static bool operations_valid(const watchy_factory_seed_ops_t *operations) {
    return operations != NULL && operations->read_marker != NULL &&
           operations->write_marker != NULL && operations->read_catalog != NULL &&
           operations->read_package != NULL && operations->package_installed != NULL &&
           operations->sha256 != NULL && operations->install_package != NULL &&
           operations->remove_package != NULL && operations->acquire_workspace != NULL &&
           operations->release_workspace != NULL;
}

watchy_package_status_t watchy_factory_seed_import(
    bool safe_mode,
    const watchy_factory_seed_ops_t *operations,
    uint8_t *catalog_wire,
    size_t catalog_capacity,
    watchy_factory_seed_catalog_t *catalog_workspace) {
    uint8_t *package_workspace = NULL;
    size_t package_capacity = 0u;
    size_t catalog_size = 0u;
    uint16_t marker_version = 0u;
    watchy_package_status_t status = WATCHY_PACKAGE_OK;
    if (safe_mode) return WATCHY_PACKAGE_OK;
    if (!operations_valid(operations) || catalog_wire == NULL ||
        catalog_capacity < WATCHY_FACTORY_SEED_CATALOG_BYTES || catalog_workspace == NULL) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    const watchy_factory_seed_marker_result_t marker =
        operations->read_marker(operations->context, &marker_version);
    if (marker == WATCHY_FACTORY_SEED_MARKER_ERROR) return WATCHY_PACKAGE_ERR_STORE;
    if (marker == WATCHY_FACTORY_SEED_MARKER_FOUND) {
        return marker_version == WATCHY_FACTORY_SEED_VERSION
                   ? WATCHY_PACKAGE_OK : WATCHY_PACKAGE_ERR_STATE;
    }
    if (operations->read_catalog(operations->context, catalog_wire, catalog_capacity,
                                 &catalog_size) != WATCHY_FACTORY_SEED_FILE_OK) {
        return WATCHY_PACKAGE_ERR_FILESYSTEM;
    }
    status = watchy_factory_seed_parse(catalog_wire, catalog_size, catalog_workspace);
    if (status != WATCHY_PACKAGE_OK) return status;
    if (!operations->acquire_workspace(operations->context, &package_workspace,
                                       &package_capacity) || package_workspace == NULL ||
        package_capacity == 0u || package_capacity > WATCHY_PACKAGE_WPK_BYTES_MAX) {
        if (package_workspace != NULL) {
            operations->release_workspace(operations->context, package_workspace);
        }
        return WATCHY_PACKAGE_ERR_LIMIT;
    }
    for (size_t index = 0u; index < WATCHY_FACTORY_SEED_COUNT; ++index) {
        const watchy_factory_seed_entry_t *entry = &catalog_workspace->entries[index];
        uint8_t digest[WATCHY_PACKAGE_DIGEST_SIZE];
        size_t package_size = 0u;
        const watchy_factory_seed_file_result_t read_result =
            operations->read_package(operations->context, entry->filename,
                                     package_workspace, package_capacity, &package_size);
        if (read_result == WATCHY_FACTORY_SEED_FILE_NOT_FOUND &&
            operations->package_installed(operations->context, entry->filename)) {
            continue;
        }
        if (read_result != WATCHY_FACTORY_SEED_FILE_OK || package_size == 0u ||
            package_size > package_capacity) {
            status = WATCHY_PACKAGE_ERR_FILESYSTEM;
            break;
        }
        if (!operations->sha256(operations->context, package_workspace, package_size, digest)) {
            status = WATCHY_PACKAGE_ERR_CRYPTO;
            break;
        }
        if (memcmp(digest, entry->sha256, sizeof(digest)) != 0) {
            status = WATCHY_PACKAGE_ERR_DIGEST;
            break;
        }
        status = operations->install_package(operations->context, package_workspace,
                                             package_size);
        if (status == WATCHY_PACKAGE_ERR_STATE &&
            operations->package_installed(operations->context, entry->filename)) {
            status = WATCHY_PACKAGE_OK;
        }
        if (status != WATCHY_PACKAGE_OK) break;
        if (!operations->remove_package(operations->context, entry->filename)) {
            status = WATCHY_PACKAGE_ERR_FILESYSTEM;
            break;
        }
        status = WATCHY_PACKAGE_OK;
    }
    operations->release_workspace(operations->context, package_workspace);
    if (status != WATCHY_PACKAGE_OK) return status;
    return operations->write_marker(operations->context, WATCHY_FACTORY_SEED_VERSION)
               ? WATCHY_PACKAGE_OK : WATCHY_PACKAGE_ERR_STORE;
}
