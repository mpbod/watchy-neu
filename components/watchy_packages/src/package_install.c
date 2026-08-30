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

static watchy_package_status_t cleanup(const watchy_package_fs_api_t *filesystem,
                                       watchy_package_install_workspace_t *workspace,
                                       const char *stage,
                                       const char *temporary,
                                       const char *final,
                                       bool stage_created,
                                       bool temporary_created,
                                       bool final_created,
                                       watchy_package_status_t result) {
    bool ok = true;
    if (temporary_created) {
        ok = filesystem->remove_tree(filesystem->context, temporary) && ok;
    }
    if (final_created) {
        ok = filesystem->remove_tree(filesystem->context, final) && ok;
    }
    if (stage_created) {
        ok = filesystem->remove_tree(filesystem->context, stage) && ok;
    }
    workspace->in_use = false;
    memset(&workspace->package, 0, sizeof(workspace->package));
    return ok ? result : WATCHY_PACKAGE_ERR_FILESYSTEM;
}

watchy_package_status_t watchy_package_install(watchy_package_index_manager_t *manager,
                                               const watchy_package_fs_api_t *filesystem,
                                               watchy_package_install_workspace_t *workspace,
                                               const uint8_t *wpk,
                                               size_t wpk_size,
                                               const watchy_crypto_api_t *crypto,
                                               char out_package_ref[WATCHY_PACKAGE_REF_MAX + 1u]) {
    wpk_view_t caller_view;
    wpk_view_t staged_view;
    watchy_package_status_t status;
    char stage_path[WATCHY_PACKAGE_PATH_BUFFER] = {0};
    char package_parent[WATCHY_PACKAGE_PATH_BUFFER] = {0};
    char temporary_path[WATCHY_PACKAGE_PATH_BUFFER] = {0};
    char final_path[WATCHY_PACKAGE_PATH_BUFFER] = {0};
    char file_path[WATCHY_PACKAGE_PATH_BUFFER];
    char package_ref[WATCHY_PACKAGE_REF_MAX + 1u];
    char preflight_ref[WATCHY_PACKAGE_REF_MAX + 1u];
    size_t staged_size = 0u;
    size_t asset_offset = 0u;
    uint32_t unique;
    bool temporary_created = false;
    bool final_created = false;
    bool stage_created = false;
    int written;

    if (manager == NULL || filesystem == NULL || workspace == NULL || wpk == NULL ||
        crypto == NULL || out_package_ref == NULL || workspace->stage_bytes == NULL ||
        filesystem->write_file == NULL || filesystem->write_file_exclusive == NULL ||
        filesystem->read_file == NULL || filesystem->mkdirs == NULL ||
        filesystem->mkdir_exclusive == NULL || filesystem->sync_tree == NULL ||
        filesystem->rename_noreplace == NULL || filesystem->path_exists == NULL ||
        filesystem->remove_tree == NULL || filesystem->unique_id == NULL ||
        wpk_size < WPK_HEADER_SIZE) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    if (workspace->in_use) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    if (wpk_size > WATCHY_PACKAGE_WPK_BYTES_MAX || wpk_size > workspace->stage_capacity) {
        return WATCHY_PACKAGE_ERR_LIMIT;
    }
    workspace->in_use = true;
    memset(&workspace->package, 0, sizeof(workspace->package));

    /* Identity-only preflight makes installed duplicates a zero-mutation path.
     * Security validation and all unpacked bytes still come from the staged readback. */
    if (wpk_parse(wpk, wpk_size, &caller_view) != WPK_OK ||
        watchy_package_manifest_parse(caller_view.manifest,
                                     caller_view.manifest_size,
                                     &workspace->package.manifest) != WATCHY_PACKAGE_OK ||
        (written = snprintf(preflight_ref, sizeof(preflight_ref), "%s@%s",
                            workspace->package.manifest.id,
                            workspace->package.manifest.version)) < 0 ||
        (size_t)written >= sizeof(preflight_ref)) {
        workspace->in_use = false;
        return WATCHY_PACKAGE_ERR_WPK;
    }
    if (watchy_package_is_installed(manager, preflight_ref, NULL)) {
        workspace->in_use = false;
        return WATCHY_PACKAGE_ERR_STATE;
    }

    if (!filesystem->mkdirs(filesystem->context, "/data/staging")) {
        return cleanup(filesystem, workspace, stage_path, temporary_path, final_path,
                       false, false, false, WATCHY_PACKAGE_ERR_FILESYSTEM);
    }
    for (unsigned attempt = 0u; attempt < 8u && !stage_created; ++attempt) {
        unique = filesystem->unique_id(filesystem->context);
        written = snprintf(stage_path, sizeof(stage_path),
                           "/data/staging/upload-%08lx.wpk", (unsigned long)unique);
        if (written < 0 || (size_t)written >= sizeof(stage_path)) {
            return cleanup(filesystem, workspace, stage_path, temporary_path, final_path,
                           false, false, false, WATCHY_PACKAGE_ERR_LIMIT);
        }
        if (filesystem->write_file_exclusive(filesystem->context, stage_path, wpk, wpk_size)) {
            stage_created = true;
        } else if (!filesystem->path_exists(filesystem->context, stage_path)) {
            return cleanup(filesystem, workspace, stage_path, temporary_path, final_path,
                           false, false, false, WATCHY_PACKAGE_ERR_FILESYSTEM);
        }
    }
    if (!stage_created ||
        !filesystem->sync_tree(filesystem->context, stage_path) ||
        !filesystem->read_file(filesystem->context, stage_path, workspace->stage_bytes,
                               workspace->stage_capacity, &staged_size) ||
        staged_size != wpk_size) {
        return cleanup(filesystem, workspace, stage_path, temporary_path, final_path,
                       stage_created, false, false, WATCHY_PACKAGE_ERR_FILESYSTEM);
    }
    status = watchy_package_validate(workspace->stage_bytes,
                                     staged_size,
                                     crypto,
                                     &workspace->package);
    if (status != WATCHY_PACKAGE_OK) {
        return cleanup(filesystem, workspace, stage_path, temporary_path, final_path,
                       true, false, false, status);
    }
    if (wpk_parse(workspace->stage_bytes, staged_size, &staged_view) != WPK_OK ||
        (written = snprintf(package_ref, sizeof(package_ref), "%s@%s",
                            workspace->package.manifest.id,
                            workspace->package.manifest.version)) < 0 ||
        (size_t)written >= sizeof(package_ref) || strcmp(package_ref, preflight_ref) != 0 ||
        !format_path(package_parent, sizeof(package_parent), "/data/packages/%s%s",
                     workspace->package.manifest.id, "") ||
        (written = snprintf(temporary_path, sizeof(temporary_path),
                            "/data/packages/%s/%s.%08lx.new",
                            workspace->package.manifest.id,
                            workspace->package.manifest.version,
                            (unsigned long)unique)) < 0 ||
        (size_t)written >= sizeof(temporary_path) ||
        !format_path(final_path, sizeof(final_path), "/data/packages/%s/%s",
                     workspace->package.manifest.id, workspace->package.manifest.version)) {
        return cleanup(filesystem, workspace, stage_path, temporary_path, final_path,
                       true, false, false, WATCHY_PACKAGE_ERR_LIMIT);
    }
    if (watchy_package_is_installed(manager, package_ref, NULL) ||
        filesystem->path_exists(filesystem->context, final_path)) {
        return cleanup(filesystem, workspace, stage_path, temporary_path, final_path,
                       true, false, false, WATCHY_PACKAGE_ERR_STATE);
    }
    if (!filesystem->mkdirs(filesystem->context, package_parent) ||
        !filesystem->mkdir_exclusive(filesystem->context, temporary_path)) {
        return cleanup(filesystem, workspace, stage_path, temporary_path, final_path,
                       true, false, false, WATCHY_PACKAGE_ERR_FILESYSTEM);
    }
    temporary_created = true;
    if (!format_path(file_path, sizeof(file_path), "%s/%s", temporary_path, "manifest.json") ||
        !filesystem->write_file(filesystem->context, file_path, staged_view.manifest,
                                staged_view.manifest_size, true) ||
        !format_path(file_path, sizeof(file_path), "%s/%s", temporary_path, "package.so") ||
        !filesystem->write_file(filesystem->context, file_path, workspace->package.elf,
                                workspace->package.elf_size, true)) {
        return cleanup(filesystem, workspace, stage_path, temporary_path, final_path,
                       true, temporary_created, false, WATCHY_PACKAGE_ERR_FILESYSTEM);
    }
    for (size_t asset = 0u; asset < workspace->package.manifest.asset_count; ++asset) {
        const watchy_package_asset_t *entry = &workspace->package.manifest.assets[asset];
        if (!format_path(file_path, sizeof(file_path), "%s/%s", temporary_path, entry->path) ||
            !prepare_asset_parent(filesystem, file_path) ||
            !filesystem->write_file(filesystem->context, file_path,
                                    workspace->package.assets + asset_offset,
                                    entry->size, true)) {
            return cleanup(filesystem, workspace, stage_path, temporary_path, final_path,
                           true, temporary_created, false, WATCHY_PACKAGE_ERR_FILESYSTEM);
        }
        asset_offset += entry->size;
    }
    if (!filesystem->sync_tree(filesystem->context, temporary_path) ||
        !filesystem->rename_noreplace(filesystem->context, temporary_path, final_path)) {
        return cleanup(filesystem, workspace, stage_path, temporary_path, final_path,
                       true, temporary_created, false, WATCHY_PACKAGE_ERR_FILESYSTEM);
    }
    temporary_created = false;
    final_created = true;
    status = watchy_package_register_installed_typed(manager,
                                                     package_ref,
                                                     workspace->package.manifest.type);
    if (status != WATCHY_PACKAGE_OK) {
        return cleanup(filesystem, workspace, stage_path, temporary_path, final_path,
                       true, false, final_created, status);
    }
    /* The NVS-last commit is the transaction boundary. A stale stage is safe
     * and boot reconciliation removes it; never report a committed install as
     * failed merely because that final housekeeping unlink failed. */
    (void)filesystem->remove_tree(filesystem->context, stage_path);
    memcpy(out_package_ref, package_ref, strlen(package_ref) + 1u);
    workspace->in_use = false;
    memset(&workspace->package, 0, sizeof(workspace->package));
    return WATCHY_PACKAGE_OK;
}
