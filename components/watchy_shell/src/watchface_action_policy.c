#include "watchy/watchface_action.h"

#include <string.h>

static watchy_status_t sync_active_setting(
    watchy_settings_t *settings,
    const watchy_package_catalog_t *catalog,
    const watchy_watchface_action_ops_t *operations) {
    const char *selected = "";
    for (size_t index = 0u; index < catalog->count; ++index) {
        if (catalog->packages[index].pending) {
            selected = catalog->packages[index].package_ref;
            break;
        }
        if (catalog->packages[index].active) {
            selected = catalog->packages[index].package_ref;
        }
    }
    if (strcmp(settings->active_watchface, selected) == 0) {
        return WATCHY_STATUS_OK;
    }
    memset(settings->active_watchface, 0, sizeof(settings->active_watchface));
    memcpy(settings->active_watchface, selected, strlen(selected) + 1u);
    return operations->save_settings(operations->context, settings);
}

watchy_status_t watchy_watchface_action_apply(
    const watchy_shell_action_request_t *request,
    bool safe_mode,
    watchy_settings_t *settings,
    watchy_package_catalog_t *catalog,
    const watchy_watchface_action_ops_t *operations,
    bool *out_package_rendered) {
    watchy_package_status_t package_status;
    bool rendered = false;
    if (out_package_rendered == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    *out_package_rendered = false;
    if (request == NULL || settings == NULL || catalog == NULL || operations == NULL ||
        operations->select_builtin == NULL || operations->select_watchface == NULL ||
        operations->run_watchface == NULL || operations->snapshot == NULL ||
        operations->save_settings == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (request->action == WATCHY_SHELL_ACTION_SELECT_BUILTIN) {
        package_status = operations->select_builtin(operations->context);
    } else if (request->action == WATCHY_SHELL_ACTION_SELECT_WATCHFACE) {
        if (safe_mode || !request->has_package || request->package_ref[0] == '\0' ||
            memchr(request->package_ref, '\0', sizeof(request->package_ref)) == NULL) {
            return WATCHY_STATUS_INVALID_ARGUMENT;
        }
        package_status = operations->select_watchface(operations->context,
                                                       request->package_ref);
        if (package_status == WATCHY_PACKAGE_OK) {
            rendered = operations->run_watchface(operations->context, safe_mode);
        }
    } else {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (package_status != WATCHY_PACKAGE_OK ||
        operations->snapshot(operations->context, catalog) != WATCHY_PACKAGE_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    const watchy_status_t settings_status =
        sync_active_setting(settings, catalog, operations);
    if (settings_status != WATCHY_STATUS_OK) {
        return settings_status;
    }
    if (request->action == WATCHY_SHELL_ACTION_SELECT_WATCHFACE && !rendered) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    *out_package_rendered = rendered;
    return WATCHY_STATUS_OK;
}
