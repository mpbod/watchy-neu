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
    watchy_package_type_t type;
    bool active;
    bool pending;
    bool quarantined;
} watchy_package_info_t;

typedef struct {
    watchy_package_info_t packages[WATCHY_PACKAGE_INSTALLED_MAX];
    size_t count;
} watchy_package_catalog_t;

watchy_package_status_t watchy_packages_runtime_init(void);
bool watchy_packages_run_watchface(bool safe_mode);
watchy_package_status_t watchy_packages_runner_start(const char *package_ref, bool safe_mode);
watchy_package_status_t watchy_packages_runner_event(const watchy_event_t *event);
watchy_package_status_t watchy_packages_runner_render(void);
watchy_package_status_t watchy_packages_runner_stop(void);
bool watchy_packages_runner_active(void);
bool watchy_packages_link_smoke(void);
watchy_package_status_t watchy_packages_install_blob(
    const uint8_t *wpk,
    size_t wpk_size,
    char out_package_ref[WATCHY_PACKAGE_REF_MAX + 1u]);
watchy_package_status_t watchy_packages_select_watchface(const char *package_ref);
watchy_package_status_t watchy_packages_snapshot(watchy_package_catalog_t *out_catalog);
watchy_package_status_t watchy_packages_remove(const char *package_ref);
watchy_package_status_t watchy_packages_safe_mode_purge(void);
watchy_package_status_t watchy_packages_upload_begin(size_t expected_size);
watchy_package_status_t watchy_packages_upload_write(const uint8_t *bytes, size_t size);
watchy_package_status_t watchy_packages_upload_finish(
    char out_package_ref[WATCHY_PACKAGE_REF_MAX + 1u]);
void watchy_packages_upload_abort(void);
bool watchy_packages_upload_active(void);

#ifdef __cplusplus
}
#endif

#endif
