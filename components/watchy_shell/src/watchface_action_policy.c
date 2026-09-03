#include "watchy/watchface_action.h"

#include <string.h>

bool watchy_watchface_boot_should_defer(watchy_wake_cause_t wake_cause,
                                        bool safe_mode) {
    return wake_cause == WATCHY_WAKE_BUTTON && !safe_mode;
}

bool watchy_watchface_catalog_needed_before_input(
    const watchy_shell_t *shell,
    watchy_shell_input_t input) {
    if (shell == NULL) return false;
    if (shell->screen == WATCHY_SHELL_LAUNCHER) {
        if (input == WATCHY_SHELL_INPUT_BACK || input == WATCHY_SHELL_INPUT_IDLE) {
            return true;
        }
        return input == WATCHY_SHELL_INPUT_MENU && shell->selection < 2u;
    }
    return shell->screen == WATCHY_SHELL_WATCHFACE_SELECTOR ||
           shell->screen == WATCHY_SHELL_PACKAGE_APPS ||
           shell->screen == WATCHY_SHELL_SAFE_MODE;
}

watchy_status_t watchy_watchface_reconcile_settings(
    watchy_settings_t *settings,
    const watchy_package_catalog_t *catalog,
    watchy_status_t (*save_settings)(void *context,
                                     const watchy_settings_t *settings),
    void *context) {
    const char *selected = "";
    if (settings == NULL || catalog == NULL || save_settings == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    for (size_t index = 0u; index < catalog->count; ++index) {
        if (catalog->packages[index].active) {
            selected = catalog->packages[index].package_ref;
            break;
        }
    }
    if (strcmp(settings->active_watchface, selected) == 0) {
        return WATCHY_STATUS_OK;
    }
    memset(settings->active_watchface, 0, sizeof(settings->active_watchface));
    memcpy(settings->active_watchface, selected, strlen(selected) + 1u);
    return save_settings(context, settings);
}

watchy_status_t watchy_watchface_boot_prepare(
    bool safe_mode,
    watchy_package_catalog_t *catalog,
    const watchy_watchface_boot_ops_t *operations,
    watchy_watchface_boot_result_t *out_result) {
    if (catalog == NULL || operations == NULL ||
        operations->import_factory_seed == NULL ||
        operations->recover_stale_pending == NULL || operations->snapshot == NULL ||
        out_result == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    *out_result = (watchy_watchface_boot_result_t){0};
    if (!safe_mode) {
        out_result->seed_import_attempted = true;
        out_result->seed_import_failed =
            operations->import_factory_seed(operations->context) != WATCHY_PACKAGE_OK;
        out_result->stale_pending_recovery_failed =
            operations->recover_stale_pending(operations->context) != WATCHY_PACKAGE_OK;
    }
    out_result->catalog_readable =
        operations->snapshot(operations->context, catalog) == WATCHY_PACKAGE_OK;
    out_result->package_warning =
        !safe_mode && (out_result->seed_import_failed ||
                       out_result->stale_pending_recovery_failed ||
                       !out_result->catalog_readable);
    return WATCHY_STATUS_OK;
}

bool watchy_watchface_boot_allows_package_execution(
    bool safe_mode,
    const watchy_watchface_boot_result_t *result) {
    return !safe_mode && result != NULL && !result->seed_import_failed &&
           !result->stale_pending_recovery_failed && result->catalog_readable;
}

watchy_status_t watchy_watchface_reconcile_catalog_mutation(
    watchy_package_mutation_result_t mutation,
    watchy_settings_t *settings,
    watchy_package_catalog_t *catalog,
    watchy_package_status_t (*snapshot)(void *context,
                                        watchy_package_catalog_t *out_catalog),
    watchy_status_t (*save_settings)(void *context,
                                     const watchy_settings_t *settings),
    void *context,
    watchy_watchface_catalog_mutation_result_t *out_result) {
    if (settings == NULL || catalog == NULL || snapshot == NULL ||
        save_settings == NULL || out_result == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    *out_result = (watchy_watchface_catalog_mutation_result_t){
        .package_failed = mutation.status != WATCHY_PACKAGE_OK,
    };
    if (!mutation.index_mutated) {
        return WATCHY_STATUS_OK;
    }
    if (snapshot(context, catalog) != WATCHY_PACKAGE_OK) {
        out_result->package_failed = true;
        return WATCHY_STATUS_OK;
    }
    out_result->catalog_refreshed = true;
    if (watchy_watchface_reconcile_settings(settings, catalog, save_settings,
                                             context) != WATCHY_STATUS_OK) {
        out_result->settings_save_failed = true;
    }
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_watchface_portal_exit(
    watchy_settings_t *settings,
    watchy_package_catalog_t *catalog,
    const watchy_watchface_portal_exit_ops_t *operations,
    watchy_watchface_portal_exit_result_t *out_result) {
    watchy_settings_t reloaded_settings;
    watchy_status_t stop_status;
    watchy_status_t load_status;
    watchy_package_status_t snapshot_status;
    watchy_status_t save_status = WATCHY_STATUS_OK;
    if (settings == NULL || catalog == NULL || operations == NULL ||
        operations->stop_portal == NULL || operations->load_settings == NULL ||
        operations->snapshot == NULL || operations->save_settings == NULL ||
        out_result == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    *out_result = (watchy_watchface_portal_exit_result_t){0};
    stop_status = operations->stop_portal(operations->context);
    reloaded_settings = *settings;
    load_status = operations->load_settings(operations->context,
                                             &reloaded_settings);
    if (load_status == WATCHY_STATUS_OK) {
        *settings = reloaded_settings;
        out_result->settings_reloaded = true;
    }
    snapshot_status = operations->snapshot(operations->context, catalog);
    if (snapshot_status == WATCHY_PACKAGE_OK) {
        out_result->catalog_refreshed = true;
        save_status = watchy_watchface_reconcile_settings(
            settings, catalog, operations->save_settings, operations->context);
    }
    if (save_status != WATCHY_STATUS_OK) {
        out_result->error = WATCHY_SHELL_ERROR_SETTINGS_SAVE;
    } else if (snapshot_status != WATCHY_PACKAGE_OK) {
        out_result->error = WATCHY_SHELL_ERROR_PACKAGE;
    } else if (load_status != WATCHY_STATUS_OK) {
        out_result->error = WATCHY_SHELL_ERROR_SETTINGS_LOAD;
    } else if (stop_status != WATCHY_STATUS_OK) {
        out_result->error = WATCHY_SHELL_ERROR_PORTAL;
    }
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_package_app_action_apply(
    bool safe_mode,
    bool package_execution_blocked,
    const char *package_ref,
    watchy_package_app_run_fn_t run_app,
    void *context) {
    if (package_ref == NULL || package_ref[0] == '\0' || run_app == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (safe_mode || package_execution_blocked) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    return run_app(context, package_ref) ? WATCHY_STATUS_OK
                                         : WATCHY_STATUS_INVALID_STATE;
}

watchy_status_t watchy_watchface_action_apply(
    const watchy_shell_action_request_t *request,
    bool safe_mode,
    bool package_execution_blocked,
    watchy_settings_t *settings,
    watchy_package_catalog_t *catalog,
    const watchy_watchface_action_ops_t *operations,
    watchy_watchface_run_result_t *out_result) {
    watchy_package_status_t package_status;
    if (out_result == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    *out_result = (watchy_watchface_run_result_t){0};
    if (request == NULL || settings == NULL || catalog == NULL || operations == NULL ||
        operations->select_builtin == NULL || operations->select_watchface == NULL ||
        operations->run_watchface == NULL || operations->force_full_refresh == NULL ||
        operations->snapshot == NULL ||
        operations->save_settings == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (request->action == WATCHY_SHELL_ACTION_SELECT_BUILTIN) {
        package_status = operations->select_builtin(operations->context);
        if (package_status == WATCHY_PACKAGE_OK) {
            operations->force_full_refresh(operations->context);
        }
    } else if (request->action == WATCHY_SHELL_ACTION_SELECT_WATCHFACE) {
        if (!request->has_package || request->package_ref[0] == '\0' ||
            memchr(request->package_ref, '\0', sizeof(request->package_ref)) == NULL) {
            return WATCHY_STATUS_INVALID_ARGUMENT;
        }
        if (safe_mode || package_execution_blocked) {
            return WATCHY_STATUS_UNSUPPORTED;
        }
        package_status = operations->select_watchface(operations->context,
                                                       request->package_ref);
        if (package_status == WATCHY_PACKAGE_OK) {
            operations->force_full_refresh(operations->context);
            *out_result = operations->run_watchface(operations->context, safe_mode, true);
        }
    } else {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (package_status != WATCHY_PACKAGE_OK ||
        operations->snapshot(operations->context, catalog) != WATCHY_PACKAGE_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    const watchy_status_t settings_status =
        watchy_watchface_reconcile_settings(settings, catalog,
                                            operations->save_settings,
                                            operations->context);
    if (settings_status != WATCHY_STATUS_OK) {
        out_result->settings_save_failed = true;
        return settings_status;
    }
    if (request->action == WATCHY_SHELL_ACTION_SELECT_WATCHFACE &&
        out_result->rendered) {
        /* The selector's activation press belongs to the completed selection.
         * Never replay it into the newly presented watchface. */
        out_result->cancelled_buttons = 0u;
    } else if (out_result->cancelled_buttons != 0u) {
        return WATCHY_STATUS_CANCELLED;
    }
    if (request->action == WATCHY_SHELL_ACTION_SELECT_WATCHFACE &&
        !out_result->rendered) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_watchface_run_active(
    bool safe_mode,
    bool package_execution_blocked,
    bool force_full_refresh,
    const watchy_package_catalog_t *catalog,
    const watchy_watchface_action_ops_t *operations,
    watchy_watchface_run_result_t *out_result) {
    bool selected = false;
    if (catalog == NULL || operations == NULL || operations->run_watchface == NULL ||
        operations->force_full_refresh == NULL || out_result == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    *out_result = (watchy_watchface_run_result_t){0};
    if (safe_mode || package_execution_blocked) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    for (size_t index = 0u; index < catalog->count; ++index) {
        selected = selected || catalog->packages[index].active ||
                   catalog->packages[index].pending;
    }
    if (!selected) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    if (force_full_refresh) {
        operations->force_full_refresh(operations->context);
    }
    *out_result = operations->run_watchface(operations->context, safe_mode,
                                            force_full_refresh);
    if (out_result->cancelled_buttons != 0u) {
        return WATCHY_STATUS_CANCELLED;
    }
    return out_result->rendered ? WATCHY_STATUS_OK : WATCHY_STATUS_INVALID_STATE;
}

watchy_status_t watchy_watchface_return_selected(
    bool safe_mode,
    bool package_execution_blocked,
    bool force_full_refresh,
    watchy_settings_t *settings,
    watchy_package_catalog_t *catalog,
    const watchy_watchface_action_ops_t *operations,
    watchy_watchface_run_result_t *out_result) {
    watchy_status_t run_status;
    watchy_status_t settings_status;
    bool pending_attempted = false;
    if (settings == NULL || catalog == NULL || operations == NULL ||
        operations->snapshot == NULL || operations->save_settings == NULL ||
        out_result == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    for (size_t index = 0u; index < catalog->count; ++index) {
        pending_attempted = pending_attempted || catalog->packages[index].pending;
    }
    run_status = watchy_watchface_run_active(
        safe_mode, package_execution_blocked, force_full_refresh,
        catalog, operations, out_result);
    if (run_status == WATCHY_STATUS_UNSUPPORTED ||
        run_status == WATCHY_STATUS_INVALID_ARGUMENT) {
        return run_status;
    }
    /* A lifecycle attempt may have promoted or rolled back a portal-created
     * pending face. Always refresh the controller's catalog and persisted
     * active-only setting before returning its render/cancellation status. */
    if (operations->snapshot(operations->context, catalog) != WATCHY_PACKAGE_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (run_status == WATCHY_STATUS_INVALID_STATE && pending_attempted) {
        bool active_restored = false;
        for (size_t index = 0u; index < catalog->count; ++index) {
            active_restored = active_restored || catalog->packages[index].active;
        }
        if (active_restored) {
            /* The runner rolled a failed portal candidate back. Render the
             * restored active package once before considering Hairline. */
            run_status = watchy_watchface_run_active(
                safe_mode, package_execution_blocked, true,
                catalog, operations, out_result);
            if (operations->snapshot(operations->context, catalog) != WATCHY_PACKAGE_OK) {
                return WATCHY_STATUS_INVALID_STATE;
            }
        }
    }
    settings_status = watchy_watchface_reconcile_settings(
        settings, catalog, operations->save_settings, operations->context);
    if (settings_status != WATCHY_STATUS_OK) {
        out_result->settings_save_failed = true;
        return settings_status;
    }
    return run_status;
}
