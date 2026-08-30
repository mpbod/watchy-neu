#include "watchy/packages.h"

#include "watchy/runtime.h"
#include "watchy/wpk.h"

#include <stdint.h>
#include <string.h>

watchy_package_status_t watchy_package_validate(const uint8_t *wpk,
                                                size_t wpk_size,
                                                const watchy_crypto_api_t *crypto,
                                                watchy_validated_package_t *out_package) {
    wpk_view_t view;
    watchy_validated_package_t package;
    watchy_package_status_t status;
    uint32_t asset_bytes = 0u;

    if (wpk == NULL || crypto == NULL || out_package == NULL) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    memset(&package, 0, sizeof(package));
    if (wpk_parse(wpk, wpk_size, &view) != WPK_OK) {
        return WATCHY_PACKAGE_ERR_WPK;
    }
    if (view.manifest_size == 0u || view.manifest_size > WATCHY_PACKAGE_MANIFEST_BYTES_MAX ||
        view.elf_size == 0u || view.elf_size > WATCHY_PACKAGE_ELF_BYTES_MAX ||
        view.assets_size > WATCHY_PACKAGE_ASSETS_BYTES_MAX) {
        return WATCHY_PACKAGE_ERR_LIMIT;
    }
    status = watchy_package_digest_matches(wpk, wpk_size, crypto);
    if (status != WATCHY_PACKAGE_OK) {
        return status;
    }
    status = watchy_package_manifest_parse(view.manifest, view.manifest_size, &package.manifest);
    if (status != WATCHY_PACKAGE_OK) {
        return status;
    }
    if (!watchy_abi_compatible(package.manifest.abi_major,
                               package.manifest.abi_minor,
                               WATCHY_ABI_V1_MAJOR,
                               WATCHY_ABI_V1_MINOR)) {
        return WATCHY_PACKAGE_ERR_ABI;
    }
    status = watchy_package_elf_validate(view.elf,
                                         view.elf_size,
                                         package.manifest.max_runtime_bytes,
                                         &package.runtime_bytes);
    if (status != WATCHY_PACKAGE_OK) {
        return status;
    }
    for (size_t index = 0u; index < package.manifest.asset_count; ++index) {
        const uint32_t size = package.manifest.assets[index].size;
        if (size > UINT32_MAX - asset_bytes) {
            return WATCHY_PACKAGE_ERR_LIMIT;
        }
        asset_bytes += size;
    }
    if (asset_bytes != view.assets_size) {
        return WATCHY_PACKAGE_ERR_ASSETS;
    }

    package.elf = view.elf;
    package.elf_size = view.elf_size;
    package.assets = view.assets;
    package.assets_size = view.assets_size;
    *out_package = package;
    return WATCHY_PACKAGE_OK;
}
