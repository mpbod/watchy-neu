#include "watchy_first_party/package.hpp"

extern "C" watchy_status_t slab_render(void *, watchy_canvas_t *, watchy_refresh_mode_t *) noexcept;

WATCHY_FIRST_PARTY_FACE_NAMED(slab_package_entry, "watchy.firstparty.slab", "Slab", 19u,
                              slab_render)
#ifndef WATCHY_HOST_TEST
extern "C" __attribute__((visibility("default")))
const watchy_package_descriptor_v1_t *watchy_package_entry(void) noexcept {
    return slab_package_entry();
}
extern "C" __attribute__((visibility("hidden"))) void app_main() {}
#endif
