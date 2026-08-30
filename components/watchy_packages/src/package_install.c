#include "watchy/packages.h"

#include "watchy/wpk.h"

#include <stdio.h>
#include <string.h>

#define WATCHY_PACKAGE_PATH_BUFFER 256u

static bool format_path(char *buffer, size_t capacity, const char *format,
                        const char *first, const char *second) {
    const int written = snprintf(buffer, capacity, format, first, second);
    return written >= 0 && (size_t)written < capacity;
}

static bool remove_if_present(const watchy_package_fs_api_t *filesystem, const char *path) {
    return filesystem->remove_tree(filesystem->context, path);
}

static bool prepare_asset_parent(const watchy_package_fs_api_t *filesystem,
                                 char path[WATCHY_PACKAGE_PATH_BUFFER]) {
    char *separator = strrchr(path, '/');
    bool result;
    if (separator == NULL) {
        return false;
    }
    *separator = '\0';
    result = filesystem->mkdirs(filesystem->context, path);
    *separator = '/';
    return result;
}

watchy_package_status_t watchy_package_install(watchy_package_index_manager_t *manager,
                                               const watchy_package_fs_api_t *filesystem,
                                               const uint8_t *wpk,
                                               size_t wpk_size,
                                               const watchy_crypto_api_t *crypto,
                                               char out_package_ref[WATCHY_PACKAGE_REF_MAX + 1u]) {
    watchy_validated_package_t package;
    watchy_package_status_t status;
    wpk_view_t view;
    char stage_path[WATCHY_PACKAGE_PATH_BUFFER];
    char package_parent[WATCHY_PACKAGE_PATH_BUFFER];
    char temporary_path[WATCHY_PACKAGE_PATH_BUFFER];
    char final_path[WATCHY_PACKAGE_PATH_BUFFER];
    char file_path[WATCHY_PACKAGE_PATH_BUFFER];
    char package_ref[WATCHY_PACKAGE_REF_MAX + 1u];
    size_t asset_offset = 0u;
    int written;

    if (manager == NULL || filesystem == NULL || wpk == NULL || crypto == NULL ||
        out_package_ref == NULL || filesystem->write_file == NULL || filesystem->mkdirs == NULL ||
        filesystem->sync_tree == NULL || filesystem->rename_atomic == NULL ||
        filesystem->remove_tree == NULL || wpk_size < WPK_HEADER_SIZE) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    written = snprintf(stage_path, sizeof(stage_path),
                       "/data/staging/upload-%02x%02x%02x%02x%02x%02x%02x%02x.wpk",
                       wpk[offsetof(wpk_header_t, package_sha256) + 0u],
                       wpk[offsetof(wpk_header_t, package_sha256) + 1u],
                       wpk[offsetof(wpk_header_t, package_sha256) + 2u],
                       wpk[offsetof(wpk_header_t, package_sha256) + 3u],
                       wpk[offsetof(wpk_header_t, package_sha256) + 4u],
                       wpk[offsetof(wpk_header_t, package_sha256) + 5u],
                       wpk[offsetof(wpk_header_t, package_sha256) + 6u],
                       wpk[offsetof(wpk_header_t, package_sha256) + 7u]);
    if (written < 0 || (size_t)written >= sizeof(stage_path)) {
        return WATCHY_PACKAGE_ERR_LIMIT;
    }
    if (!filesystem->mkdirs(filesystem->context, "/data/staging") ||
        !filesystem->write_file(filesystem->context, stage_path, wpk, wpk_size, false) ||
        !filesystem->sync_tree(filesystem->context, stage_path)) {
        (void)remove_if_present(filesystem, stage_path);
        return WATCHY_PACKAGE_ERR_FILESYSTEM;
    }

    status = watchy_package_validate(wpk, wpk_size, crypto, &package);
    if (status != WATCHY_PACKAGE_OK) {
        if (!remove_if_present(filesystem, stage_path)) {
            return WATCHY_PACKAGE_ERR_FILESYSTEM;
        }
        return status;
    }
    if (wpk_parse(wpk, wpk_size, &view) != WPK_OK ||
        snprintf(package_ref, sizeof(package_ref), "%s@%s",
                 package.manifest.id, package.manifest.version) < 0 ||
        !format_path(package_parent, sizeof(package_parent), "/data/packages/%s%s",
                     package.manifest.id, "") ||
        !format_path(temporary_path, sizeof(temporary_path), "/data/packages/%s/%s.new",
                     package.manifest.id, package.manifest.version) ||
        !format_path(final_path, sizeof(final_path), "/data/packages/%s/%s",
                     package.manifest.id, package.manifest.version)) {
        (void)remove_if_present(filesystem, stage_path);
        return WATCHY_PACKAGE_ERR_LIMIT;
    }

    (void)remove_if_present(filesystem, temporary_path);
    if (!filesystem->mkdirs(filesystem->context, package_parent) ||
        !filesystem->mkdirs(filesystem->context, temporary_path) ||
        !format_path(file_path, sizeof(file_path), "%s/%s", temporary_path, "manifest.json") ||
        !filesystem->write_file(filesystem->context,
                                file_path,
                                view.manifest,
                                view.manifest_size,
                                true) ||
        !format_path(file_path, sizeof(file_path), "%s/%s", temporary_path, "package.so") ||
        !filesystem->write_file(filesystem->context,
                                file_path,
                                package.elf,
                                package.elf_size,
                                true)) {
        (void)remove_if_present(filesystem, temporary_path);
        (void)remove_if_present(filesystem, stage_path);
        return WATCHY_PACKAGE_ERR_FILESYSTEM;
    }
    for (size_t asset = 0u; asset < package.manifest.asset_count; ++asset) {
        const watchy_package_asset_t *entry = &package.manifest.assets[asset];
        if (!format_path(file_path, sizeof(file_path), "%s/%s", temporary_path, entry->path) ||
            !prepare_asset_parent(filesystem, file_path) ||
            !filesystem->write_file(filesystem->context,
                                    file_path,
                                    package.assets + asset_offset,
                                    entry->size,
                                    true)) {
            (void)remove_if_present(filesystem, temporary_path);
            (void)remove_if_present(filesystem, stage_path);
            return WATCHY_PACKAGE_ERR_FILESYSTEM;
        }
        asset_offset += entry->size;
    }
    if (!filesystem->sync_tree(filesystem->context, temporary_path) ||
        !filesystem->rename_atomic(filesystem->context, temporary_path, final_path) ||
        !remove_if_present(filesystem, stage_path)) {
        (void)remove_if_present(filesystem, temporary_path);
        (void)remove_if_present(filesystem, final_path);
        (void)remove_if_present(filesystem, stage_path);
        return WATCHY_PACKAGE_ERR_FILESYSTEM;
    }
    status = watchy_package_register_installed(manager, package_ref);
    if (status != WATCHY_PACKAGE_OK) {
        (void)remove_if_present(filesystem, final_path);
        return status;
    }
    memcpy(out_package_ref, package_ref, strlen(package_ref) + 1u);
    return WATCHY_PACKAGE_OK;
}
