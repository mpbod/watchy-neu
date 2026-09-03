#include "watchy/watchface_action.h"

#include <string.h>

static watchy_button_t package_app_button_from_mask(watchy_button_mask_t mask) {
    if ((mask & WATCHY_BUTTON_MASK_MENU) != 0u) return WATCHY_BUTTON_CONFIRM;
    if ((mask & WATCHY_BUTTON_MASK_BACK) != 0u) return WATCHY_BUTTON_BACK;
    if ((mask & WATCHY_BUTTON_MASK_UP) != 0u) return WATCHY_BUTTON_UP;
    return WATCHY_BUTTON_DOWN;
}

static void package_app_take_cancelled_buttons(
    const watchy_package_app_loop_ops_t *operations,
    watchy_button_mask_t *pending_buttons) {
    watchy_button_mask_t cancelled_buttons = 0u;

    if (operations->take_cancelled_buttons(operations->context,
                                            &cancelled_buttons)) {
        *pending_buttons |= cancelled_buttons;
    }
}

watchy_package_app_run_result_t watchy_package_app_run_loop(
    const char *package_ref,
    const watchy_package_app_loop_ops_t *operations,
    uint32_t poll_timeout_ms,
    uint32_t idle_timeout_ms) {
    watchy_package_app_run_result_t result = {0};
    watchy_button_mask_t pending_buttons = 0u;
    watchy_package_status_t status;
    uint64_t last_activity;
    watchy_package_app_runner_t runner;

    if (package_ref == NULL || package_ref[0] == '\0' || operations == NULL ||
        operations->start == NULL || operations->event == NULL ||
        operations->active == NULL || operations->render == NULL ||
        operations->stop == NULL || operations->take_press == NULL ||
        operations->overflowed == NULL ||
        operations->take_cancelled_buttons == NULL ||
        operations->milliseconds == NULL) {
        return result;
    }
    runner = (watchy_package_app_runner_t){
        .event = operations->event,
        .active = operations->active,
        .render = operations->render,
        .stop = operations->stop,
        .context = operations->context,
    };
    status = operations->start(operations->context, package_ref);
    package_app_take_cancelled_buttons(operations, &pending_buttons);
    if (status != WATCHY_PACKAGE_OK) {
        result.cancelled_buttons = pending_buttons;
        return result;
    }
    if (!operations->active(operations->context)) {
        result.succeeded = true;
        result.cancelled_buttons = pending_buttons;
        return result;
    }
    status = operations->render(operations->context);
    package_app_take_cancelled_buttons(operations, &pending_buttons);
    last_activity = operations->milliseconds(operations->context);
    while (status == WATCHY_PACKAGE_OK &&
           operations->active(operations->context)) {
        watchy_button_mask_t pressed = pending_buttons;
        pending_buttons = 0u;
        if (pressed == 0u) {
            watchy_button_press_event_t event;
            if (operations->take_press(operations->context, &event,
                                       poll_timeout_ms)) {
                pressed = event.mask;
            }
        }
        if (operations->milliseconds(operations->context) - last_activity >=
            idle_timeout_ms) {
            pending_buttons |= pressed;
            status = operations->stop(operations->context);
            break;
        }
        if (pressed == 0u && operations->overflowed(operations->context)) {
            status = WATCHY_PACKAGE_ERR_STATE;
            break;
        }
        if (pressed != 0u) {
            last_activity = operations->milliseconds(operations->context);
            status = watchy_package_dispatch_app_button(
                &runner, package_app_button_from_mask(pressed));
            package_app_take_cancelled_buttons(operations, &pending_buttons);
        }
    }
    if (operations->active(operations->context)) {
        (void)operations->stop(operations->context);
    }
    package_app_take_cancelled_buttons(operations, &pending_buttons);
    result.succeeded = status == WATCHY_PACKAGE_OK;
    result.cancelled_buttons = pending_buttons;
    return result;
}

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
    void *context,
    watchy_package_app_run_result_t *out_result) {
    if (out_result == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    *out_result = (watchy_package_app_run_result_t){0};
    if (package_ref == NULL || package_ref[0] == '\0' || run_app == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (safe_mode || package_execution_blocked) {
        return WATCHY_STATUS_UNSUPPORTED;
    }
    *out_result = run_app(context, package_ref);
    return out_result->succeeded ? WATCHY_STATUS_OK : WATCHY_STATUS_INVALID_STATE;
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
