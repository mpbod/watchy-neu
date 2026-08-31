#include "watchy_first_party/package.hpp"

namespace watchy_first_party {
/* Mirrors first_party/watchfaces/common/src/face.cpp so the fixture links as a
 * self-contained package image (no undefined symbols). */
__attribute__((visibility("hidden")))
face_context &state() noexcept {
    static face_context value{nullptr, false};
    return value;
}
}  // namespace watchy_first_party

extern "C" watchy_status_t fixture_render(void *, watchy_canvas_t *,
                                           watchy_refresh_mode_t *) noexcept {
    return WATCHY_STATUS_OK;
}

WATCHY_FIRST_PARTY_FACE_NAMED(fixture_package_entry, "fixture.face", "Fixture", 3u,
                              fixture_render)

extern "C" __attribute__((visibility("default")))
const watchy_package_descriptor_v1_t *watchy_package_entry(void) noexcept {
    return fixture_package_entry();
}

extern "C" __attribute__((visibility("hidden"))) void app_main() {}
