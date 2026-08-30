#ifndef WATCHY_PACKAGE_RUNTIME_H
#define WATCHY_PACKAGE_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "watchy/packages.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif
