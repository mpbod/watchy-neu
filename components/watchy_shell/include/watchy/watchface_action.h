#ifndef WATCHY_WATCHFACE_ACTION_H
#define WATCHY_WATCHFACE_ACTION_H

#include <stdbool.h>

#include "watchy/settings.h"
#include "watchy/shell.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool rendered;
    bool settings_save_failed;
    watchy_button_mask_t cancelled_buttons;
} watchy_watchface_run_result_t;

typedef struct {
    watchy_package_status_t (*select_builtin)(void *context);
    watchy_package_status_t (*select_watchface)(void *context, const char *package_ref);
    watchy_watchface_run_result_t (*run_watchface)(void *context,
                                                   bool safe_mode,
                                                   bool force_full_refresh);
    void (*force_full_refresh)(void *context);
    watchy_package_status_t (*snapshot)(void *context,
                                        watchy_package_catalog_t *out_catalog);
    watchy_status_t (*save_settings)(void *context,
                                     const watchy_settings_t *settings);
    void *context;
} watchy_watchface_action_ops_t;

typedef struct {
    watchy_package_status_t (*import_factory_seed)(void *context);
    watchy_package_status_t (*recover_stale_pending)(void *context);
    watchy_package_status_t (*snapshot)(void *context,
                                        watchy_package_catalog_t *out_catalog);
    void *context;
} watchy_watchface_boot_ops_t;

typedef struct {
    bool seed_import_attempted;
    bool seed_import_failed;
    bool stale_pending_recovery_failed;
    bool catalog_readable;
    bool package_warning;
} watchy_watchface_boot_result_t;

typedef struct {
    bool catalog_refreshed;
    bool package_failed;
    bool settings_save_failed;
} watchy_watchface_catalog_mutation_result_t;

typedef struct {
    watchy_status_t (*stop_portal)(void *context);
    watchy_status_t (*load_settings)(void *context,
                                     watchy_settings_t *out_settings);
    watchy_package_status_t (*snapshot)(void *context,
                                        watchy_package_catalog_t *out_catalog);
    watchy_status_t (*save_settings)(void *context,
                                     const watchy_settings_t *settings);
    void *context;
} watchy_watchface_portal_exit_ops_t;

typedef struct {
    bool settings_reloaded;
    bool catalog_refreshed;
    watchy_shell_error_t error;
} watchy_watchface_portal_exit_result_t;

typedef bool (*watchy_package_app_run_fn_t)(void *context,
                                            const char *package_ref);

watchy_status_t watchy_watchface_reconcile_settings(
    watchy_settings_t *settings,
    const watchy_package_catalog_t *catalog,
    watchy_status_t (*save_settings)(void *context,
                                     const watchy_settings_t *settings),
    void *context);

bool watchy_watchface_boot_allows_package_execution(
    bool safe_mode,
    const watchy_watchface_boot_result_t *result);

watchy_status_t watchy_watchface_reconcile_catalog_mutation(
    watchy_package_mutation_result_t mutation,
    watchy_settings_t *settings,
    watchy_package_catalog_t *catalog,
    watchy_package_status_t (*snapshot)(void *context,
                                        watchy_package_catalog_t *out_catalog),
    watchy_status_t (*save_settings)(void *context,
                                     const watchy_settings_t *settings),
    void *context,
    watchy_watchface_catalog_mutation_result_t *out_result);

watchy_status_t watchy_watchface_portal_exit(
    watchy_settings_t *settings,
    watchy_package_catalog_t *catalog,
    const watchy_watchface_portal_exit_ops_t *operations,
    watchy_watchface_portal_exit_result_t *out_result);

watchy_status_t watchy_package_app_action_apply(
    bool safe_mode,
    bool package_execution_blocked,
    const char *package_ref,
    watchy_package_app_run_fn_t run_app,
    void *context);

watchy_status_t watchy_watchface_boot_prepare(
    bool safe_mode,
    watchy_package_catalog_t *catalog,
    const watchy_watchface_boot_ops_t *operations,
    watchy_watchface_boot_result_t *out_result);

watchy_status_t watchy_watchface_action_apply(
    const watchy_shell_action_request_t *request,
    bool safe_mode,
    bool package_execution_blocked,
    watchy_settings_t *settings,
    watchy_package_catalog_t *catalog,
    const watchy_watchface_action_ops_t *operations,
    watchy_watchface_run_result_t *out_result);

watchy_status_t watchy_watchface_run_active(
    bool safe_mode,
    bool package_execution_blocked,
    bool force_full_refresh,
    const watchy_package_catalog_t *catalog,
    const watchy_watchface_action_ops_t *operations,
    watchy_watchface_run_result_t *out_result);

watchy_status_t watchy_watchface_return_selected(
    bool safe_mode,
    bool package_execution_blocked,
    bool force_full_refresh,
    watchy_settings_t *settings,
    watchy_package_catalog_t *catalog,
    const watchy_watchface_action_ops_t *operations,
    watchy_watchface_run_result_t *out_result);

#ifdef __cplusplus
}
#endif

#endif
