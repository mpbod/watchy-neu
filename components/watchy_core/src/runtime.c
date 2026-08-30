#include "watchy/runtime.h"

#include <string.h>

bool watchy_abi_compatible(uint16_t required_major,
                           uint16_t required_minor,
                           uint16_t provided_major,
                           uint16_t provided_minor) {
    return required_major == provided_major && required_minor <= provided_minor;
}

watchy_status_t watchy_runtime_negotiate_abi(uint16_t required_major,
                                             uint16_t required_minor,
                                             uint16_t provided_major,
                                             uint16_t provided_minor,
                                             watchy_abi_version_t *negotiated) {
    if (negotiated == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (!watchy_abi_compatible(required_major, required_minor, provided_major, provided_minor)) {
        negotiated->major = 0;
        negotiated->minor = 0;
        return WATCHY_STATUS_INCOMPATIBLE_ABI;
    }

    negotiated->major = required_major;
    negotiated->minor = required_minor;
    return WATCHY_STATUS_OK;
}

void watchy_runtime_reset(watchy_runtime_t *runtime) {
    if (runtime == NULL) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->state = WATCHY_RUNTIME_EMPTY;
}

watchy_status_t watchy_runtime_transition(watchy_runtime_t *runtime, watchy_runtime_state_t next_state) {
    if (runtime == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }

    switch (runtime->state) {
        case WATCHY_RUNTIME_EMPTY:
            if (next_state == WATCHY_RUNTIME_LOADED) {
                runtime->state = next_state;
                return WATCHY_STATUS_OK;
            }
            break;
        case WATCHY_RUNTIME_LOADED:
            if (next_state == WATCHY_RUNTIME_STARTED) {
                runtime->state = next_state;
                return WATCHY_STATUS_OK;
            }
            break;
        case WATCHY_RUNTIME_STARTED:
            if (next_state == WATCHY_RUNTIME_STOPPED) {
                runtime->state = next_state;
                return WATCHY_STATUS_OK;
            }
            break;
        case WATCHY_RUNTIME_STOPPED:
            if (next_state == WATCHY_RUNTIME_EMPTY) {
                runtime->state = next_state;
                return WATCHY_STATUS_OK;
            }
            break;
    }

    return WATCHY_STATUS_INVALID_STATE;
}
