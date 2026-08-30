#ifndef WATCHY_SHELL_RENDER_H
#define WATCHY_SHELL_RENDER_H

#include "watchy/package_runtime.h"
#include "watchy/settings.h"
#include "watchy/shell.h"
#include "watchy/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

void watchy_shell_render(watchy_canvas_t *canvas,
                         const watchy_shell_t *shell,
                         const watchy_settings_t *settings,
                         const watchy_time_t *time,
                         const watchy_battery_state_t *battery,
                         const watchy_package_catalog_t *catalog,
                         const char *detail);

#ifdef __cplusplus
}
#endif

#endif
