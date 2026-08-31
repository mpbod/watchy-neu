#ifndef WATCHY_WATCHFACE_ACTION_H
#define WATCHY_WATCHFACE_ACTION_H

#include <stdbool.h>

#include "watchy/settings.h"
#include "watchy/shell.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    watchy_package_status_t (*select_builtin)(void *context);
    watchy_package_status_t (*select_watchface)(void *context, const char *package_ref);
    bool (*run_watchface)(void *context, bool safe_mode);
    watchy_package_status_t (*snapshot)(void *context,
                                        watchy_package_catalog_t *out_catalog);
    watchy_status_t (*save_settings)(void *context,
                                     const watchy_settings_t *settings);
    void *context;
} watchy_watchface_action_ops_t;

watchy_status_t watchy_watchface_action_apply(
    const watchy_shell_action_request_t *request,
    bool safe_mode,
    watchy_settings_t *settings,
    watchy_package_catalog_t *catalog,
    const watchy_watchface_action_ops_t *operations,
    bool *out_package_rendered);

#ifdef __cplusplus
}
#endif

#endif
