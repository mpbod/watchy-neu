#include "watchy_first_party/package.hpp"

extern "C" watchy_status_t grid02_render(void *, watchy_canvas_t *, watchy_refresh_mode_t *) noexcept;

WATCHY_FIRST_PARTY_FACE_NAMED(grid02_package_entry, "watchy.firstparty.grid02", "Grid 02", 515u,
                              grid02_render)
#ifndef WATCHY_HOST_TEST
extern "C" __attribute__((visibility("default")))
const watchy_package_descriptor_v1_t *watchy_package_entry(void) noexcept {
    return grid02_package_entry();
}
#endif
#ifndef WATCHY_HOST_TEST
extern "C" __attribute__((visibility("hidden"))) void app_main() {}
#endif
