#include "watchy_first_party/package.hpp"

extern "C" watchy_status_t fixture_render(void *, watchy_canvas_t *,
                                           watchy_refresh_mode_t *) noexcept {
    return WATCHY_STATUS_OK;
}

WATCHY_FIRST_PARTY_FACE_NAMED(fixture_package_entry, "fixture.face", "Fixture", 515u,
                              fixture_render)

extern "C" __attribute__((visibility("default")))
const watchy_package_descriptor_v1_t *watchy_package_entry(void) noexcept {
    return fixture_package_entry();
}

extern "C" __attribute__((visibility("hidden"))) void app_main() {}
