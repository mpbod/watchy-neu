#include "watchy/packages.h"

#include <stddef.h>
#include <string.h>

static bool package_ref_valid(const char *package_ref) {
    char identifier[WATCHY_PACKAGE_ID_MAX + 1u];
    const char *separator;
    size_t identifier_length;
    size_t version_length;

    if (package_ref == NULL) {
        return false;
    }
    separator = strchr(package_ref, '@');
    if (separator == NULL || separator == package_ref || strchr(separator + 1, '@') != NULL) {
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
    if (!watchy_package_id_valid(identifier)) {
        return false;
    }
    for (size_t index = 0u; index < version_length; ++index) {
        const unsigned char byte = (unsigned char)separator[1u + index];
        if (byte < 0x21u || byte == 0x7fu || byte == '/' || byte == '\\') {
            return false;
        }
    }
    return true;
}

static bool stored_ref_valid(const char *package_ref, bool allow_empty) {
    if (memchr(package_ref, '\0', WATCHY_PACKAGE_REF_MAX + 1u) == NULL) {
        return false;
    }
    return (allow_empty && package_ref[0] == '\0') || package_ref_valid(package_ref);
}

static bool persisted_index_valid(const watchy_package_index_t *index) {
    if (index->magic != WATCHY_PACKAGE_INDEX_MAGIC ||
        index->version != WATCHY_PACKAGE_INDEX_VERSION ||
        index->installed_count > WATCHY_PACKAGE_INSTALLED_MAX ||
        index->health_count > WATCHY_PACKAGE_HEALTH_RECORD_MAX ||
        !stored_ref_valid(index->active_watchface, true) ||
        !stored_ref_valid(index->pending_watchface, true) ||
        !stored_ref_valid(index->prior_watchface, true)) {
        return false;
    }
    for (size_t installed = 0u; installed < index->installed_count; ++installed) {
        if (!stored_ref_valid(index->installed[installed], false)) {
            return false;
        }
        for (size_t prior = 0u; prior < installed; ++prior) {
            if (strcmp(index->installed[prior], index->installed[installed]) == 0) {
                return false;
            }
        }
    }
    for (size_t health = 0u; health < index->health_count; ++health) {
        if (!stored_ref_valid(index->health[health].package_ref, false) ||
            index->health[health].incomplete_attempts > 3u) {
            return false;
        }
        for (size_t prior = 0u; prior < health; ++prior) {
            if (strcmp(index->health[prior].package_ref, index->health[health].package_ref) == 0) {
                return false;
            }
        }
    }
    return true;
}

static watchy_package_status_t commit_index(watchy_package_index_manager_t *manager,
                                            const watchy_package_index_t *next) {
    if (!manager->store.save(manager->store.context, next)) {
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
    watchy_package_index_t index;
    watchy_index_store_result_t result;

    if (manager == NULL || store == NULL || store->load == NULL || store->save == NULL) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    memset(manager, 0, sizeof(*manager));
    memset(&index, 0, sizeof(index));
    result = store->load(store->context, &index);
    if (result == WATCHY_INDEX_STORE_ERROR) {
        return WATCHY_PACKAGE_ERR_STORE;
    }
    if (result == WATCHY_INDEX_STORE_NOT_FOUND) {
        memset(&index, 0, sizeof(index));
    } else if (!persisted_index_valid(&index)) {
        return WATCHY_PACKAGE_ERR_STORE;
    }
    index.magic = WATCHY_PACKAGE_INDEX_MAGIC;
    index.version = WATCHY_PACKAGE_INDEX_VERSION;
    manager->index = index;
    manager->store = *store;
    manager->initialized = true;
    return WATCHY_PACKAGE_OK;
}

const watchy_package_index_t *watchy_package_index_snapshot(
    const watchy_package_index_manager_t *manager) {
    return manager != NULL && manager->initialized ? &manager->index : NULL;
}

watchy_package_status_t watchy_package_select_watchface(watchy_package_index_manager_t *manager,
                                                        const char *package_ref) {
    watchy_package_index_t next;

    if (check_manager(manager) != WATCHY_PACKAGE_OK || !package_ref_valid(package_ref)) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    if (watchy_package_is_quarantined(manager, package_ref)) {
        return WATCHY_PACKAGE_ERR_QUARANTINED;
    }
    next = manager->index;
    memcpy(next.prior_watchface, next.active_watchface, sizeof(next.prior_watchface));
    memset(next.pending_watchface, 0, sizeof(next.pending_watchface));
    memcpy(next.pending_watchface, package_ref, strlen(package_ref) + 1u);
    return commit_index(manager, &next);
}

watchy_package_status_t watchy_package_promote_pending(watchy_package_index_manager_t *manager,
                                                       const char *package_ref) {
    watchy_package_index_t next;

    if (check_manager(manager) != WATCHY_PACKAGE_OK || package_ref == NULL ||
        strcmp(manager->index.pending_watchface, package_ref) != 0) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    next = manager->index;
    memcpy(next.active_watchface, next.pending_watchface, sizeof(next.active_watchface));
    memset(next.pending_watchface, 0, sizeof(next.pending_watchface));
    return commit_index(manager, &next);
}

watchy_package_status_t watchy_package_rollback_pending(watchy_package_index_manager_t *manager,
                                                        const char *package_ref) {
    watchy_package_index_t next;

    if (check_manager(manager) != WATCHY_PACKAGE_OK || package_ref == NULL ||
        strcmp(manager->index.pending_watchface, package_ref) != 0) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    next = manager->index;
    memcpy(next.active_watchface, next.prior_watchface, sizeof(next.active_watchface));
    memset(next.pending_watchface, 0, sizeof(next.pending_watchface));
    memset(next.prior_watchface, 0, sizeof(next.prior_watchface));
    return commit_index(manager, &next);
}

watchy_package_status_t watchy_package_begin_attempt(watchy_package_index_manager_t *manager,
                                                     const char *package_ref,
                                                     bool safe_mode) {
    watchy_package_index_t next;
    ptrdiff_t record;

    if (check_manager(manager) != WATCHY_PACKAGE_OK || !package_ref_valid(package_ref)) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    if (safe_mode) {
        return WATCHY_PACKAGE_ERR_SAFE_MODE;
    }
    next = manager->index;
    record = find_health(&next, package_ref);
    if (record >= 0 && next.health[record].quarantined) {
        return WATCHY_PACKAGE_ERR_QUARANTINED;
    }
    if (record < 0) {
        if (next.health_count >= WATCHY_PACKAGE_HEALTH_RECORD_MAX) {
            return WATCHY_PACKAGE_ERR_LIMIT;
        }
        record = (ptrdiff_t)next.health_count++;
        memcpy(next.health[record].package_ref, package_ref, strlen(package_ref) + 1u);
    }
    if (next.health[record].incomplete_attempts >= 3u) {
        next.health[record].quarantined = true;
        if (commit_index(manager, &next) != WATCHY_PACKAGE_OK) {
            return WATCHY_PACKAGE_ERR_STORE;
        }
        return WATCHY_PACKAGE_ERR_QUARANTINED;
    }
    ++next.health[record].incomplete_attempts;
    return commit_index(manager, &next);
}

watchy_package_status_t watchy_package_finish_attempt(watchy_package_index_manager_t *manager,
                                                      const char *package_ref,
                                                      bool clean_stop) {
    watchy_package_index_t next;
    ptrdiff_t record;

    if (check_manager(manager) != WATCHY_PACKAGE_OK || package_ref == NULL) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    next = manager->index;
    record = find_health(&next, package_ref);
    if (record < 0 || next.health[record].incomplete_attempts == 0u) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    if (clean_stop) {
        next.health[record].incomplete_attempts = 0u;
    } else if (next.health[record].incomplete_attempts >= 3u) {
        next.health[record].quarantined = true;
    }
    return commit_index(manager, &next);
}

bool watchy_package_is_quarantined(const watchy_package_index_manager_t *manager,
                                   const char *package_ref) {
    ptrdiff_t record;
    if (manager == NULL || !manager->initialized || package_ref == NULL) {
        return false;
    }
    record = find_health(&manager->index, package_ref);
    return record >= 0 && manager->index.health[record].quarantined;
}

watchy_package_status_t watchy_package_register_installed(
    watchy_package_index_manager_t *manager,
    const char *package_ref) {
    watchy_package_index_t next;

    if (check_manager(manager) != WATCHY_PACKAGE_OK || !package_ref_valid(package_ref)) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    for (size_t index = 0u; index < manager->index.installed_count; ++index) {
        if (strcmp(manager->index.installed[index], package_ref) == 0) {
            return WATCHY_PACKAGE_ERR_STATE;
        }
    }
    if (manager->index.installed_count >= WATCHY_PACKAGE_INSTALLED_MAX) {
        return WATCHY_PACKAGE_ERR_LIMIT;
    }
    next = manager->index;
    memcpy(next.installed[next.installed_count], package_ref, strlen(package_ref) + 1u);
    ++next.installed_count;
    return commit_index(manager, &next);
}
