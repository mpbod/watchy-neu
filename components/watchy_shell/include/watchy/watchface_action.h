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
    watchy_watchface_run_result_t (*run_watchface)(void *context, bool safe_mode);
    void (*force_full_refresh)(void *context);
    watchy_package_status_t (*snapshot)(void *context,
                                        watchy_package_catalog_t *out_catalog);
    watchy_status_t (*save_settings)(void *context,
                                     const watchy_settings_t *settings);
    void *context;
} watchy_watchface_action_ops_t;

typedef struct {
    watchy_package_status_t (*import_factory_seed)(void *context);
    watchy_package_status_t (*snapshot)(void *context,
                                        watchy_package_catalog_t *out_catalog);
    void *context;
} watchy_watchface_boot_ops_t;

typedef struct {
    bool seed_import_attempted;
    bool seed_import_failed;
    bool catalog_readable;
    bool package_warning;
} watchy_watchface_boot_result_t;

watchy_status_t watchy_watchface_reconcile_settings(
    watchy_settings_t *settings,
    const watchy_package_catalog_t *catalog,
    watchy_status_t (*save_settings)(void *context,
                                     const watchy_settings_t *settings),
    void *context);

watchy_status_t watchy_watchface_boot_prepare(
    bool safe_mode,
    watchy_package_catalog_t *catalog,
    const watchy_watchface_boot_ops_t *operations,
    watchy_watchface_boot_result_t *out_result);

watchy_status_t watchy_watchface_action_apply(
    const watchy_shell_action_request_t *request,
    bool safe_mode,
    watchy_settings_t *settings,
    watchy_package_catalog_t *catalog,
    const watchy_watchface_action_ops_t *operations,
    watchy_watchface_run_result_t *out_result);

watchy_status_t watchy_watchface_run_active(
    bool safe_mode,
    bool force_full_refresh,
    const watchy_package_catalog_t *catalog,
    const watchy_watchface_action_ops_t *operations,
    watchy_watchface_run_result_t *out_result);

#ifdef __cplusplus
}
#endif

#endif
