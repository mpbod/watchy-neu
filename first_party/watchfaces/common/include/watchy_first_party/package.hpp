#ifndef WATCHY_FIRST_PARTY_PACKAGE_HPP
#define WATCHY_FIRST_PARTY_PACKAGE_HPP

#include "watchy/sdk.hpp"
#include <cstddef>

namespace watchy_first_party {
/* These assertions are the C++ side of the append-only package ABI contract.
 * Keep the uint32 size prefix and callback order byte-for-byte stable. */
constexpr std::size_t abi_align_up(std::size_t value, std::size_t alignment) noexcept {
    return (value + alignment - 1u) / alignment * alignment;
}
static_assert(offsetof(watchy_package_descriptor_v1_t, size) == 0, "descriptor size prefix changed");
static_assert(offsetof(watchy_package_descriptor_v1_t, metadata) ==
                  abi_align_up(sizeof(uint32_t), alignof(watchy_package_metadata_t)),
              "descriptor metadata prefix changed");
static_assert(offsetof(watchy_package_descriptor_v1_t, callbacks) ==
                  abi_align_up(offsetof(watchy_package_descriptor_v1_t, metadata) +
                                   sizeof(watchy_package_metadata_t),
                               alignof(watchy_package_callbacks_t)),
              "descriptor callbacks moved");
static_assert(offsetof(watchy_system_api_v1_t, request_transition) ==
                  sizeof(void *) + sizeof(uint32_t (*)(void *)) +
                  sizeof(void (*)(void *, uint32_t)) + sizeof(void (*)(void *, const char *)) +
                  sizeof(watchy_status_t (*)(void *)) +
                  sizeof(watchy_status_t (*)(void *, watchy_refresh_mode_t)),
              "ABI 1.1 system prefix changed");
static_assert(sizeof(watchy_abi_version_t) == 4, "ABI version layout changed");
using RenderFn = watchy_status_t (*)(void *, watchy_canvas_t *, watchy_refresh_mode_t *);
struct face_context {
    const watchy_host_caps_v1_t *host;
    bool loaded;
};
inline const watchy_host_caps_v1_t *host(const void *user_data) noexcept {
    return user_data == nullptr ? nullptr : static_cast<const face_context *>(user_data)->host;
}
/* One state object per package image: defined out-of-line in face.cpp so the
 * load (package.cpp TU) and render (renderer.cpp TU) paths share the same
 * face_context. A header-local static here would duplicate state per TU and
 * defeat the load/render handoff. */
face_context &state() noexcept;

inline void set_routine_refresh(void *user_data, watchy_refresh_mode_t *mode) noexcept {
    if (mode != nullptr && user_data == &state() && state().loaded) {
        *mode = WATCHY_REFRESH_PARTIAL;
    }
}

inline watchy_status_t load(const watchy_host_caps_v1_t *caps, void **user_data) noexcept {
    if (user_data == nullptr) return WATCHY_STATUS_INVALID_ARGUMENT;
    *user_data = nullptr;
    if (caps == nullptr) return WATCHY_STATUS_INVALID_ARGUMENT;
    if (state().loaded) return WATCHY_STATUS_INVALID_STATE;
    state().host = caps;
    state().loaded = true;
    *user_data = &state();
    return WATCHY_STATUS_OK;
}
inline void unload(void *user_data) noexcept {
    (void)user_data;
    state().host = nullptr;
    state().loaded = false;
}
inline watchy_status_t start(void *user_data) noexcept {
    return user_data == &state() && state().loaded ? WATCHY_STATUS_OK
                                                    : WATCHY_STATUS_INVALID_STATE;
}
inline void stop(void *) noexcept {}
inline watchy_status_t event(void *, const watchy_event_t *) noexcept { return WATCHY_STATUS_OK; }

}  // namespace watchy_first_party

#define WATCHY_FIRST_PARTY_FACE_NAMED(ENTRY, ID, NAME, CAPS, RENDER_FN) \
extern "C" __attribute__((visibility("hidden"))) \
const watchy_package_descriptor_v1_t *ENTRY(void) { \
    static watchy_package_descriptor_v1_t descriptor = { \
        sizeof(watchy_package_descriptor_v1_t), {ID, NAME, "1.0.0", {1u, 2u}, CAPS}, \
        {watchy_first_party::load, watchy_first_party::unload, watchy_first_party::start, \
         watchy_first_party::stop, watchy_first_party::event, RENDER_FN} }; \
    return &descriptor; \
}

#define WATCHY_FIRST_PARTY_FACE(ID, NAME, CAPS, RENDER_FN) \
WATCHY_FIRST_PARTY_FACE_NAMED(watchy_package_entry, ID, NAME, CAPS, RENDER_FN)

#endif
