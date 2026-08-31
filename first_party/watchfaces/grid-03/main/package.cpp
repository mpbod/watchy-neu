#include "watchy_first_party/package.hpp"

extern "C" watchy_status_t grid03_render(void *, watchy_canvas_t *, watchy_refresh_mode_t *) noexcept;

WATCHY_FIRST_PARTY_FACE_NAMED(grid03_package_entry, "watchy.firstparty.grid03", "Grid 03", 3u,
                              grid03_render)
#ifndef WATCHY_HOST_TEST
extern "C" __attribute__((visibility("default")))
const watchy_package_descriptor_v1_t *watchy_package_entry(void) noexcept {
    return grid03_package_entry();
}
#endif
#ifndef WATCHY_HOST_TEST
extern "C" __attribute__((visibility("hidden"))) void app_main() {}
#endif
