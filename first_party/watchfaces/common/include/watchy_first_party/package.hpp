#ifndef WATCHY_FIRST_PARTY_PACKAGE_HPP
#define WATCHY_FIRST_PARTY_PACKAGE_HPP

#include "watchy/sdk.hpp"
#include <cstddef>

namespace watchy_first_party {
static_assert(offsetof(watchy_package_descriptor_v1_t, metadata) >= sizeof(uint32_t), "descriptor prefix changed");
static_assert(offsetof(watchy_package_descriptor_v1_t, callbacks) > offsetof(watchy_package_descriptor_v1_t, metadata), "descriptor callback order changed");
static_assert(sizeof(watchy_abi_version_t) == 4, "ABI version layout changed");
using RenderFn = watchy_status_t (*)(void *, watchy_canvas_t *, watchy_refresh_mode_t *);
struct face_context { const watchy_host_caps_v1_t *host; };
inline const watchy_host_caps_v1_t *host(const void *user_data) noexcept { return user_data == nullptr ? nullptr : static_cast<const face_context *>(user_data)->host; }
inline face_context &state() noexcept { static face_context value{nullptr}; return value; }

inline watchy_status_t load(const watchy_host_caps_v1_t *caps, void **user_data) noexcept { if (caps == nullptr || user_data == nullptr) return WATCHY_STATUS_INVALID_ARGUMENT; state().host = caps; *user_data = &state(); return WATCHY_STATUS_OK; }
inline void unload(void *) noexcept {}
inline watchy_status_t start(void *) noexcept { return WATCHY_STATUS_OK; }
inline void stop(void *) noexcept {}
inline watchy_status_t event(void *, const watchy_event_t *) noexcept { return WATCHY_STATUS_OK; }

}  // namespace watchy_first_party

#define WATCHY_FIRST_PARTY_FACE(ID, NAME, CAPS, RENDER_FN) \
extern "C" const watchy_package_descriptor_v1_t *watchy_package_entry(void) { \
    static const watchy_package_descriptor_v1_t descriptor = { \
        sizeof(watchy_package_descriptor_v1_t), {ID, NAME, "1.0.0", {1u, 2u}, 0u}, \
        {watchy_first_party::load, watchy_first_party::unload, watchy_first_party::start, \
         watchy_first_party::stop, watchy_first_party::event, RENDER_FN} }; \
    return &descriptor; \
}

#endif
