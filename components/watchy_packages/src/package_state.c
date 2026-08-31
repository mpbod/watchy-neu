#include "watchy/package_runtime.h"

#include <stddef.h>
#include <string.h>

enum {
    WIRE_HEADER_SIZE = 12u,
    WIRE_REF_SIZE = WATCHY_PACKAGE_REF_MAX + 1u,
    WIRE_INSTALLED_SIZE = 2u + WIRE_REF_SIZE,
    WIRE_HEALTH_SIZE = 4u + WIRE_REF_SIZE,
    WIRE_SIZE = WIRE_HEADER_SIZE + 3u * WIRE_REF_SIZE +
                WATCHY_PACKAGE_INSTALLED_MAX * WIRE_INSTALLED_SIZE +
                WATCHY_PACKAGE_HEALTH_RECORD_MAX * WIRE_HEALTH_SIZE,
};

_Static_assert(WIRE_SIZE <= WATCHY_PACKAGE_INDEX_WIRE_MAX, "index wire buffer too small");

static uint16_t get_u16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u));
}

static uint32_t get_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

static void put_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
}

static void put_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
    bytes[2] = (uint8_t)(value >> 16u);
    bytes[3] = (uint8_t)(value >> 24u);
}

static bool package_ref_valid(const char *package_ref) {
    char identifier[WATCHY_PACKAGE_ID_MAX + 1u];
    char version[WATCHY_PACKAGE_VERSION_MAX + 1u];
    const char *separator;
    size_t identifier_length;
    size_t version_length;

    if (package_ref == NULL ||
        (separator = strchr(package_ref, '@')) == NULL || separator == package_ref ||
        strchr(separator + 1, '@') != NULL) {
        return false;
    }
    identifier_length = (size_t)(separator - package_ref);
    version_length = strlen(separator + 1);
    if (identifier_length > WATCHY_PACKAGE_ID_MAX || version_length == 0u ||
        version_length > WATCHY_PACKAGE_VERSION_MAX) {
        return false;
    }
    memcpy(identifier, package_ref, identifier_length);
    identifier[identifier_length] = '\0';
    memcpy(version, separator + 1u, version_length + 1u);
    return watchy_package_id_valid(identifier) && watchy_package_version_valid(version);
}

static bool stored_ref_valid(const char *package_ref, bool allow_empty) {
    const char *terminator = memchr(package_ref, '\0', WATCHY_PACKAGE_REF_MAX + 1u);
    if (terminator == NULL ||
        ((package_ref[0] != '\0' || !allow_empty) && !package_ref_valid(package_ref))) {
        return false;
    }
    for (const char *padding = terminator + 1u;
         padding < package_ref + WATCHY_PACKAGE_REF_MAX + 1u; ++padding) {
        if (*padding != '\0') {
            return false;
        }
    }
    return true;
}

static ptrdiff_t find_installed(const watchy_package_index_t *index, const char *package_ref) {
    for (size_t record = 0u; record < index->installed_count; ++record) {
        if (strcmp(index->installed[record], package_ref) == 0) {
            return (ptrdiff_t)record;
        }
    }
    return -1;
}

static bool installed_watchface(const watchy_package_index_t *index, const char *package_ref) {
    const ptrdiff_t record = find_installed(index, package_ref);
    return record >= 0 && index->installed_types[record] == WATCHY_PACKAGE_TYPE_WATCHFACE;
}

static bool persisted_index_valid(const watchy_package_index_t *index) {
    if (index->magic != WATCHY_PACKAGE_INDEX_MAGIC ||
        index->version != WATCHY_PACKAGE_INDEX_VERSION || index->reserved != 0u ||
        index->installed_count > WATCHY_PACKAGE_INSTALLED_MAX ||
        index->health_count > WATCHY_PACKAGE_HEALTH_RECORD_MAX ||
        !stored_ref_valid(index->active_watchface, true) ||
        !stored_ref_valid(index->pending_watchface, true) ||
        !stored_ref_valid(index->prior_watchface, true)) {
        return false;
    }
    for (size_t installed = 0u; installed < index->installed_count; ++installed) {
        if (!stored_ref_valid(index->installed[installed], false) ||
            (index->installed_types[installed] != WATCHY_PACKAGE_TYPE_WATCHFACE &&
             index->installed_types[installed] != WATCHY_PACKAGE_TYPE_APP)) {
            return false;
        }
        for (size_t prior = 0u; prior < installed; ++prior) {
            if (strcmp(index->installed[prior], index->installed[installed]) == 0) {
                return false;
            }
        }
    }
    if ((index->active_watchface[0] != '\0' &&
         !installed_watchface(index, index->active_watchface)) ||
        (index->pending_watchface[0] != '\0' &&
         !installed_watchface(index, index->pending_watchface)) ||
        (index->prior_watchface[0] != '\0' &&
         !installed_watchface(index, index->prior_watchface)) ||
        (index->pending_watchface[0] == '\0' && index->prior_watchface[0] != '\0') ||
        (index->pending_watchface[0] != '\0' &&
         strcmp(index->prior_watchface, index->active_watchface) != 0)) {
        return false;
    }
    for (size_t health = 0u; health < index->health_count; ++health) {
        const watchy_package_health_t *record = &index->health[health];
        if (!stored_ref_valid(record->package_ref, false) ||
            find_installed(index, record->package_ref) < 0 ||
            record->incomplete_attempts > 3u ||
            (record->quarantined && record->incomplete_attempts != 3u)) {
            return false;
        }
        for (size_t prior = 0u; prior < health; ++prior) {
            if (strcmp(index->health[prior].package_ref, record->package_ref) == 0) {
                return false;
            }
        }
    }
    return true;
}

watchy_package_status_t watchy_package_index_encode(const watchy_package_index_t *index,
                                                    uint8_t *wire,
                                                    size_t capacity,
                                                    size_t *out_size) {
    size_t offset = WIRE_HEADER_SIZE;
    if (index == NULL || wire == NULL || out_size == NULL || capacity < WIRE_SIZE ||
        !persisted_index_valid(index)) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    memset(wire, 0, WIRE_SIZE);
    put_u32(wire, WATCHY_PACKAGE_INDEX_MAGIC);
    put_u16(wire + 4u, WATCHY_PACKAGE_INDEX_VERSION);
    put_u16(wire + 8u, (uint16_t)index->installed_count);
    put_u16(wire + 10u, (uint16_t)index->health_count);
    memcpy(wire + offset, index->active_watchface, WIRE_REF_SIZE);
    offset += WIRE_REF_SIZE;
    memcpy(wire + offset, index->pending_watchface, WIRE_REF_SIZE);
    offset += WIRE_REF_SIZE;
    memcpy(wire + offset, index->prior_watchface, WIRE_REF_SIZE);
    offset += WIRE_REF_SIZE;
    for (size_t record = 0u; record < WATCHY_PACKAGE_INSTALLED_MAX; ++record) {
        if (record < index->installed_count) {
            wire[offset] = (uint8_t)index->installed_types[record];
            memcpy(wire + offset + 2u, index->installed[record], WIRE_REF_SIZE);
        }
        offset += WIRE_INSTALLED_SIZE;
    }
    for (size_t record = 0u; record < WATCHY_PACKAGE_HEALTH_RECORD_MAX; ++record) {
        if (record < index->health_count) {
            wire[offset] = index->health[record].incomplete_attempts;
            wire[offset + 1u] = index->health[record].quarantined ? 1u : 0u;
            memcpy(wire + offset + 4u, index->health[record].package_ref, WIRE_REF_SIZE);
        }
        offset += WIRE_HEALTH_SIZE;
    }
    *out_size = WIRE_SIZE;
    return WATCHY_PACKAGE_OK;
}

watchy_package_status_t watchy_package_index_decode(const uint8_t *wire,
                                                    size_t size,
                                                    watchy_package_index_t *out_index) {
    static const uint8_t zero_installed[WIRE_INSTALLED_SIZE] = {0};
    static const uint8_t zero_health[WIRE_HEALTH_SIZE] = {0};
    size_t offset = WIRE_HEADER_SIZE;
    if (wire == NULL || out_index == NULL || size != WIRE_SIZE ||
        get_u32(wire) != WATCHY_PACKAGE_INDEX_MAGIC ||
        get_u16(wire + 4u) != WATCHY_PACKAGE_INDEX_VERSION || get_u16(wire + 6u) != 0u) {
        return WATCHY_PACKAGE_ERR_STORE;
    }
    memset(out_index, 0, sizeof(*out_index));
    out_index->magic = WATCHY_PACKAGE_INDEX_MAGIC;
    out_index->version = WATCHY_PACKAGE_INDEX_VERSION;
    out_index->installed_count = get_u16(wire + 8u);
    out_index->health_count = get_u16(wire + 10u);
    memcpy(out_index->active_watchface, wire + offset, WIRE_REF_SIZE);
    offset += WIRE_REF_SIZE;
    memcpy(out_index->pending_watchface, wire + offset, WIRE_REF_SIZE);
    offset += WIRE_REF_SIZE;
    memcpy(out_index->prior_watchface, wire + offset, WIRE_REF_SIZE);
    offset += WIRE_REF_SIZE;
    for (size_t record = 0u; record < WATCHY_PACKAGE_INSTALLED_MAX; ++record) {
        const bool used = record < out_index->installed_count;
        if (wire[offset + 1u] != 0u || (!used && memcmp(wire + offset,
                                                       zero_installed,
                                                       WIRE_INSTALLED_SIZE) != 0)) {
            return WATCHY_PACKAGE_ERR_STORE;
        }
        if (used) {
            out_index->installed_types[record] = (watchy_package_type_t)wire[offset];
            memcpy(out_index->installed[record], wire + offset + 2u, WIRE_REF_SIZE);
        }
        offset += WIRE_INSTALLED_SIZE;
    }
    for (size_t record = 0u; record < WATCHY_PACKAGE_HEALTH_RECORD_MAX; ++record) {
        const bool used = record < out_index->health_count;
        if (wire[offset + 2u] != 0u || wire[offset + 3u] != 0u ||
            (used && wire[offset + 1u] > 1u) ||
            (!used && memcmp(wire + offset, zero_health, WIRE_HEALTH_SIZE) != 0)) {
            return WATCHY_PACKAGE_ERR_STORE;
        }
        if (used) {
            out_index->health[record].incomplete_attempts = wire[offset];
            out_index->health[record].quarantined = wire[offset + 1u] == 1u;
            memcpy(out_index->health[record].package_ref, wire + offset + 4u, WIRE_REF_SIZE);
        }
        offset += WIRE_HEALTH_SIZE;
    }
    return persisted_index_valid(out_index) ? WATCHY_PACKAGE_OK : WATCHY_PACKAGE_ERR_STORE;
}

static watchy_package_status_t commit_index(watchy_package_index_manager_t *manager,
                                            const watchy_package_index_t *next) {
    if (!persisted_index_valid(next) || !manager->store.save(manager->store.context, next)) {
        return WATCHY_PACKAGE_ERR_STORE;
    }
    manager->index = *next;
    return WATCHY_PACKAGE_OK;
}

static ptrdiff_t find_health(const watchy_package_index_t *index, const char *package_ref) {
    for (size_t record = 0u; record < index->health_count; ++record) {
        if (strcmp(index->health[record].package_ref, package_ref) == 0) {
            return (ptrdiff_t)record;
        }
    }
    return -1;
}

static watchy_package_status_t check_manager(const watchy_package_index_manager_t *manager) {
    return manager != NULL && manager->initialized ? WATCHY_PACKAGE_OK : WATCHY_PACKAGE_ERR_STATE;
}

watchy_package_status_t watchy_package_index_init(watchy_package_index_manager_t *manager,
                                                  const watchy_package_index_store_t *store) {
    watchy_index_store_result_t result;
    if (manager == NULL || store == NULL || store->load == NULL || store->save == NULL) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    memset(manager, 0, sizeof(*manager));
    result = store->load(store->context, &manager->index);
    if (result == WATCHY_INDEX_STORE_ERROR ||
        (result == WATCHY_INDEX_STORE_OK && !persisted_index_valid(&manager->index))) {
        return WATCHY_PACKAGE_ERR_STORE;
    }
    if (result == WATCHY_INDEX_STORE_NOT_FOUND) {
        memset(&manager->index, 0, sizeof(manager->index));
        manager->index.magic = WATCHY_PACKAGE_INDEX_MAGIC;
        manager->index.version = WATCHY_PACKAGE_INDEX_VERSION;
        if (!store->save(store->context, &manager->index)) {
            memset(manager, 0, sizeof(*manager));
            return WATCHY_PACKAGE_ERR_STORE;
        }
    }
    manager->store = *store;
    manager->initialized = true;
    return WATCHY_PACKAGE_OK;
}

const watchy_package_index_t *watchy_package_index_snapshot(
    const watchy_package_index_manager_t *manager) {
    return manager != NULL && manager->initialized ? &manager->index : NULL;
}

bool watchy_package_is_installed(const watchy_package_index_manager_t *manager,
                                 const char *package_ref,
                                 watchy_package_type_t *out_type) {
    const ptrdiff_t record = manager != NULL && manager->initialized && package_ref != NULL
                                 ? find_installed(&manager->index, package_ref)
                                 : -1;
    if (record < 0) {
        return false;
    }
    if (out_type != NULL) {
        *out_type = manager->index.installed_types[record];
    }
    return true;
}

watchy_package_status_t watchy_package_select_watchface(watchy_package_index_manager_t *manager,
                                                        const char *package_ref) {
    watchy_package_index_t *next;
    if (check_manager(manager) != WATCHY_PACKAGE_OK || !package_ref_valid(package_ref)) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    if (!installed_watchface(&manager->index, package_ref)) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    if (watchy_package_is_quarantined(manager, package_ref)) {
        return WATCHY_PACKAGE_ERR_QUARANTINED;
    }
    next = &manager->scratch;
    *next = manager->index;
    memcpy(next->prior_watchface, next->active_watchface, sizeof(next->prior_watchface));
    memset(next->pending_watchface, 0, sizeof(next->pending_watchface));
    memcpy(next->pending_watchface, package_ref, strlen(package_ref) + 1u);
    return commit_index(manager, next);
}

watchy_package_status_t watchy_package_select_builtin(watchy_package_index_manager_t *manager) {
    watchy_package_index_t *next;
    if (check_manager(manager) != WATCHY_PACKAGE_OK) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    next = &manager->scratch;
    *next = manager->index;
    memset(next->active_watchface, 0, sizeof(next->active_watchface));
    memset(next->pending_watchface, 0, sizeof(next->pending_watchface));
    memset(next->prior_watchface, 0, sizeof(next->prior_watchface));
    return commit_index(manager, next);
}

static bool manifest_matches_installed(const watchy_package_manifest_t *manifest,
                                       const char *package_ref,
                                       watchy_package_type_t type) {
    const size_t id_length = strnlen(manifest->id, sizeof(manifest->id));
    const size_t name_length = strnlen(manifest->name, sizeof(manifest->name));
    const size_t version_length = strnlen(manifest->version, sizeof(manifest->version));
    return id_length != 0u && id_length <= WATCHY_PACKAGE_ID_MAX &&
           name_length != 0u && name_length <= WATCHY_PACKAGE_NAME_MAX &&
           version_length != 0u && version_length <= WATCHY_PACKAGE_VERSION_MAX &&
           manifest->type == type && watchy_package_id_valid(manifest->id) &&
           watchy_package_version_valid(manifest->version) &&
           strncmp(package_ref, manifest->id, id_length) == 0 &&
           package_ref[id_length] == '@' &&
           strcmp(package_ref + id_length + 1u, manifest->version) == 0;
}

watchy_package_status_t watchy_package_catalog_snapshot(
    const watchy_package_index_manager_t *manager,
    watchy_package_manifest_reader_fn_t read_manifest,
    void *read_context,
    watchy_package_manifest_t *manifest_workspace,
    watchy_package_catalog_t *out_catalog) {
    const watchy_package_index_t *index;
    watchy_package_status_t status;
    if (out_catalog == NULL) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    memset(out_catalog, 0, sizeof(*out_catalog));
    if (check_manager(manager) != WATCHY_PACKAGE_OK || read_manifest == NULL ||
        manifest_workspace == NULL) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    index = &manager->index;
    for (size_t record = 0u; record < index->installed_count; ++record) {
        watchy_package_info_t *info = &out_catalog->packages[record];
        const char *package_ref = index->installed[record];
        memset(manifest_workspace, 0, sizeof(*manifest_workspace));
        status = read_manifest(read_context, package_ref, manifest_workspace);
        if (status != WATCHY_PACKAGE_OK ||
            !manifest_matches_installed(manifest_workspace, package_ref,
                                        index->installed_types[record])) {
            memset(out_catalog, 0, sizeof(*out_catalog));
            return status != WATCHY_PACKAGE_OK ? status : WATCHY_PACKAGE_ERR_MANIFEST;
        }
        memcpy(info->package_ref, package_ref, strlen(package_ref));
        info->package_ref[strlen(package_ref)] = '\0';
        memcpy(info->name, manifest_workspace->name, strlen(manifest_workspace->name));
        info->name[strlen(manifest_workspace->name)] = '\0';
        memcpy(info->version, manifest_workspace->version,
               strlen(manifest_workspace->version));
        info->version[strlen(manifest_workspace->version)] = '\0';
        info->type = index->installed_types[record];
        info->active = strcmp(index->active_watchface, package_ref) == 0;
        info->pending = strcmp(index->pending_watchface, package_ref) == 0;
        info->quarantined = watchy_package_is_quarantined(manager, package_ref);
        ++out_catalog->count;
    }
    return WATCHY_PACKAGE_OK;
}

watchy_package_status_t watchy_package_promote_pending(watchy_package_index_manager_t *manager,
                                                       const char *package_ref) {
    watchy_package_index_t *next;
    if (check_manager(manager) != WATCHY_PACKAGE_OK || package_ref == NULL ||
        strcmp(manager->index.pending_watchface, package_ref) != 0) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    next = &manager->scratch;
    *next = manager->index;
    memcpy(next->active_watchface, next->pending_watchface, sizeof(next->active_watchface));
    memset(next->pending_watchface, 0, sizeof(next->pending_watchface));
    memset(next->prior_watchface, 0, sizeof(next->prior_watchface));
    return commit_index(manager, next);
}

watchy_package_status_t watchy_package_rollback_pending(watchy_package_index_manager_t *manager,
                                                        const char *package_ref) {
    watchy_package_index_t *next;
    if (check_manager(manager) != WATCHY_PACKAGE_OK || package_ref == NULL ||
        strcmp(manager->index.pending_watchface, package_ref) != 0) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    next = &manager->scratch;
    *next = manager->index;
    memcpy(next->active_watchface, next->prior_watchface, sizeof(next->active_watchface));
    memset(next->pending_watchface, 0, sizeof(next->pending_watchface));
    memset(next->prior_watchface, 0, sizeof(next->prior_watchface));
    return commit_index(manager, next);
}

watchy_package_status_t watchy_package_begin_attempt(watchy_package_index_manager_t *manager,
                                                     const char *package_ref,
                                                     bool safe_mode) {
    watchy_package_index_t *next;
    ptrdiff_t record;
    if (check_manager(manager) != WATCHY_PACKAGE_OK || !package_ref_valid(package_ref)) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    if (safe_mode) {
        return WATCHY_PACKAGE_ERR_SAFE_MODE;
    }
    if (!watchy_package_is_installed(manager, package_ref, NULL)) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    next = &manager->scratch;
    *next = manager->index;
    record = find_health(next, package_ref);
    if (record >= 0 && next->health[record].quarantined) {
        return WATCHY_PACKAGE_ERR_QUARANTINED;
    }
    if (record < 0) {
        if (next->health_count >= WATCHY_PACKAGE_HEALTH_RECORD_MAX) {
            return WATCHY_PACKAGE_ERR_LIMIT;
        }
        record = (ptrdiff_t)next->health_count++;
        memcpy(next->health[record].package_ref, package_ref, strlen(package_ref) + 1u);
    }
    if (next->health[record].incomplete_attempts >= 3u) {
        next->health[record].quarantined = true;
        return commit_index(manager, next) == WATCHY_PACKAGE_OK
                   ? WATCHY_PACKAGE_ERR_QUARANTINED : WATCHY_PACKAGE_ERR_STORE;
    }
    ++next->health[record].incomplete_attempts;
    return commit_index(manager, next);
}

watchy_package_status_t watchy_package_finish_attempt(watchy_package_index_manager_t *manager,
                                                      const char *package_ref,
                                                      bool clean_stop) {
    watchy_package_index_t *next;
    ptrdiff_t record;
    if (check_manager(manager) != WATCHY_PACKAGE_OK || package_ref == NULL) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    next = &manager->scratch;
    *next = manager->index;
    record = find_health(next, package_ref);
    if (record < 0 || next->health[record].incomplete_attempts == 0u) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    if (clean_stop) {
        next->health[record].incomplete_attempts = 0u;
    } else if (next->health[record].incomplete_attempts >= 3u) {
        next->health[record].quarantined = true;
    }
    return commit_index(manager, next);
}

bool watchy_package_is_quarantined(const watchy_package_index_manager_t *manager,
                                   const char *package_ref) {
    const ptrdiff_t record = manager != NULL && manager->initialized && package_ref != NULL
                                 ? find_health(&manager->index, package_ref)
                                 : -1;
    return record >= 0 && manager->index.health[record].quarantined;
}

watchy_package_status_t watchy_package_register_installed_typed(
    watchy_package_index_manager_t *manager,
    const char *package_ref,
    watchy_package_type_t type) {
    watchy_package_index_t *next;
    if (check_manager(manager) != WATCHY_PACKAGE_OK || !package_ref_valid(package_ref) ||
        (type != WATCHY_PACKAGE_TYPE_WATCHFACE && type != WATCHY_PACKAGE_TYPE_APP)) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    if (find_installed(&manager->index, package_ref) >= 0) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    if (manager->index.installed_count >= WATCHY_PACKAGE_INSTALLED_MAX) {
        return WATCHY_PACKAGE_ERR_LIMIT;
    }
    next = &manager->scratch;
    *next = manager->index;
    memcpy(next->installed[next->installed_count], package_ref, strlen(package_ref) + 1u);
    next->installed_types[next->installed_count] = type;
    ++next->installed_count;
    return commit_index(manager, next);
}

watchy_package_status_t watchy_package_register_installed(
    watchy_package_index_manager_t *manager,
    const char *package_ref) {
    return watchy_package_register_installed_typed(manager,
                                                   package_ref,
                                                   WATCHY_PACKAGE_TYPE_WATCHFACE);
}

watchy_package_status_t watchy_package_unregister(watchy_package_index_manager_t *manager,
                                                  const char *package_ref) {
    watchy_package_index_t *next;
    ptrdiff_t installed_record;
    ptrdiff_t health_record;
    if (check_manager(manager) != WATCHY_PACKAGE_OK || !package_ref_valid(package_ref) ||
        (installed_record = find_installed(&manager->index, package_ref)) < 0) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    next = &manager->scratch;
    *next = manager->index;
    for (size_t record = (size_t)installed_record; record + 1u < next->installed_count; ++record) {
        memcpy(next->installed[record], next->installed[record + 1u],
               sizeof(next->installed[record]));
        next->installed_types[record] = next->installed_types[record + 1u];
    }
    --next->installed_count;
    memset(next->installed[next->installed_count], 0,
           sizeof(next->installed[next->installed_count]));
    next->installed_types[next->installed_count] = WATCHY_PACKAGE_TYPE_WATCHFACE;
    health_record = find_health(next, package_ref);
    if (health_record >= 0) {
        for (size_t record = (size_t)health_record; record + 1u < next->health_count; ++record) {
            next->health[record] = next->health[record + 1u];
        }
        --next->health_count;
        memset(&next->health[next->health_count], 0, sizeof(next->health[next->health_count]));
    }
    if (strcmp(next->active_watchface, package_ref) == 0 ||
        strcmp(next->prior_watchface, package_ref) == 0) {
        memset(next->active_watchface, 0, sizeof(next->active_watchface));
        memset(next->pending_watchface, 0, sizeof(next->pending_watchface));
        memset(next->prior_watchface, 0, sizeof(next->prior_watchface));
    } else if (strcmp(next->pending_watchface, package_ref) == 0) {
        memset(next->pending_watchface, 0, sizeof(next->pending_watchface));
        memset(next->prior_watchface, 0, sizeof(next->prior_watchface));
    }
    return commit_index(manager, next);
}

watchy_package_status_t watchy_package_index_clear(watchy_package_index_manager_t *manager) {
    watchy_package_index_t *next;
    if (check_manager(manager) != WATCHY_PACKAGE_OK) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    next = &manager->scratch;
    memset(next, 0, sizeof(*next));
    next->magic = WATCHY_PACKAGE_INDEX_MAGIC;
    next->version = WATCHY_PACKAGE_INDEX_VERSION;
    return commit_index(manager, next);
}

bool watchy_package_index_has_id(const watchy_package_index_manager_t *manager,
                                 const char *identifier) {
    size_t identifier_length;
    if (manager == NULL || !manager->initialized || !watchy_package_id_valid(identifier)) {
        return false;
    }
    identifier_length = strlen(identifier);
    for (size_t record = 0u; record < manager->index.installed_count; ++record) {
        if (strncmp(manager->index.installed[record], identifier, identifier_length) == 0 &&
            manager->index.installed[record][identifier_length] == '@') {
            return true;
        }
    }
    return false;
}
