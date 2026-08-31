#include "watchy_first_party/package.hpp"

extern "C" watchy_status_t grid01_render(void *, watchy_canvas_t *, watchy_refresh_mode_t *) noexcept;

WATCHY_FIRST_PARTY_FACE_NAMED(grid01_package_entry, "watchy.firstparty.grid01", "Grid 01", 787u,
                              grid01_render)
#ifndef WATCHY_HOST_TEST
extern "C" const watchy_package_descriptor_v1_t *watchy_package_entry(void) noexcept {
    return grid01_package_entry();
}
#endif
#ifndef WATCHY_HOST_TEST
extern "C" __attribute__((visibility("hidden"))) void app_main() {}
#endif
