#ifndef WATCHY_PACKAGE_RUNTIME_H
#define WATCHY_PACKAGE_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "watchy/packages.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char package_ref[WATCHY_PACKAGE_REF_MAX + 1u];
    char name[WATCHY_PACKAGE_NAME_MAX + 1u];
    char version[WATCHY_PACKAGE_VERSION_MAX + 1u];
    watchy_package_type_t type;
    bool active;
    bool pending;
    bool quarantined;
} watchy_package_info_t;

typedef struct {
    watchy_package_info_t packages[WATCHY_PACKAGE_INSTALLED_MAX];
    size_t count;
} watchy_package_catalog_t;

typedef struct {
    watchy_package_status_t status;
    bool index_mutated;
} watchy_package_mutation_result_t;

typedef watchy_package_status_t (*watchy_package_manifest_reader_fn_t)(
    void *context,
    const char *package_ref,
    watchy_package_manifest_t *out_manifest);

typedef enum {
    WATCHY_PACKAGE_INIT_INDEX_ONLY = 0,
    WATCHY_PACKAGE_INIT_FULL = 1,
} watchy_package_runtime_init_mode_t;

typedef struct {
    bool index_ready;
    bool storage_reconciled;
} watchy_package_runtime_init_state_t;

typedef struct {
    watchy_package_status_t (*initialize_index)(void *context);
    watchy_package_status_t (*reconcile_storage)(void *context);
    watchy_package_status_t (*select_builtin)(void *context);
    void *context;
} watchy_package_runtime_init_ops_t;

typedef struct {
    watchy_package_status_t (*start)(void *context);
    bool (*active)(void *context);
    watchy_package_status_t (*render)(void *context);
    watchy_package_status_t (*stop)(void *context);
    void *context;
} watchy_package_watchface_runner_t;

typedef struct {
    watchy_package_status_t (*event)(void *context, const watchy_event_t *event);
    bool (*active)(void *context);
    watchy_package_status_t (*render)(void *context);
    watchy_package_status_t (*stop)(void *context);
    void *context;
} watchy_package_app_runner_t;

bool watchy_package_run_watchface_cycle(const watchy_package_watchface_runner_t *runner);
watchy_package_status_t watchy_package_dispatch_app_button(
    const watchy_package_app_runner_t *runner,
    watchy_button_t button);
watchy_package_status_t watchy_package_runtime_prepare(
    watchy_package_runtime_init_state_t *state,
    watchy_package_runtime_init_mode_t mode,
    const watchy_package_runtime_init_ops_t *operations);
watchy_package_status_t watchy_package_runtime_select_builtin(
    watchy_package_runtime_init_state_t *state,
    const watchy_package_runtime_init_ops_t *operations);

watchy_package_status_t watchy_packages_runtime_init(void);
watchy_package_status_t watchy_packages_import_factory_seed(bool safe_mode);
bool watchy_packages_run_watchface(bool safe_mode, bool force_full_refresh);
watchy_package_status_t watchy_packages_runner_start(const char *package_ref, bool safe_mode);
watchy_package_status_t watchy_packages_runner_event(const watchy_event_t *event);
watchy_package_status_t watchy_packages_runner_render(void);
watchy_package_status_t watchy_packages_runner_stop(void);
bool watchy_packages_runner_active(void);
bool watchy_packages_link_smoke(void);
watchy_package_status_t watchy_packages_install_blob(
    uint8_t *wpk,
    size_t wpk_size,
    char out_package_ref[WATCHY_PACKAGE_REF_MAX + 1u]);
watchy_package_status_t watchy_packages_select_watchface(const char *package_ref);
watchy_package_status_t watchy_packages_select_builtin(void);
watchy_package_status_t watchy_packages_recover_stale_pending(void);
watchy_package_status_t watchy_packages_snapshot(watchy_package_catalog_t *out_catalog);
watchy_package_status_t watchy_package_catalog_snapshot(
    const watchy_package_index_manager_t *manager,
    watchy_package_manifest_reader_fn_t read_manifest,
    void *read_context,
    watchy_package_manifest_t *manifest_workspace,
    watchy_package_catalog_t *out_catalog);
watchy_package_status_t watchy_packages_remove(const char *package_ref);
watchy_package_status_t watchy_packages_safe_mode_purge(void);
watchy_package_mutation_result_t watchy_packages_remove_observed(
    const char *package_ref);
watchy_package_mutation_result_t watchy_packages_safe_mode_purge_observed(void);
watchy_package_status_t watchy_packages_upload_begin(size_t expected_size);
watchy_package_status_t watchy_packages_upload_write(const uint8_t *bytes, size_t size);
watchy_package_status_t watchy_packages_upload_finish(
    char out_package_ref[WATCHY_PACKAGE_REF_MAX + 1u]);
void watchy_packages_upload_abort(void);
bool watchy_packages_upload_active(void);
watchy_package_status_t watchy_package_upload_finalize_status(bool active,
                                                              bool output_valid,
                                                              bool content_complete,
                                                              bool sync_ok,
                                                              bool close_ok);

#ifdef __cplusplus
}
#endif

#endif
