#ifndef WATCHY_RUNTIME_H
#define WATCHY_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "watchy/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WATCHY_RUNTIME_EMPTY = 0,
    WATCHY_RUNTIME_LOADED = 1,
    WATCHY_RUNTIME_STARTED = 2,
    WATCHY_RUNTIME_STOPPED = 3
} watchy_runtime_state_t;

typedef struct {
    watchy_runtime_state_t state;
    watchy_abi_version_t package_abi;
    watchy_abi_version_t host_abi;
} watchy_runtime_t;

typedef struct {
    uint16_t partial_limit;
    uint16_t partial_count;
} watchy_refresh_policy_t;

bool watchy_abi_compatible(uint16_t required_major,
                           uint16_t required_minor,
                           uint16_t provided_major,
                           uint16_t provided_minor);
watchy_status_t watchy_runtime_negotiate_abi(uint16_t required_major,
                                             uint16_t required_minor,
                                             uint16_t provided_major,
                                             uint16_t provided_minor,
                                             watchy_abi_version_t *negotiated);
void watchy_runtime_reset(watchy_runtime_t *runtime);
watchy_status_t watchy_runtime_transition(watchy_runtime_t *runtime, watchy_runtime_state_t next_state);

void watchy_refresh_policy_reset(watchy_refresh_policy_t *policy, uint16_t partial_limit);
watchy_refresh_mode_t watchy_refresh_decide(watchy_refresh_policy_t *policy, watchy_refresh_mode_t requested);

#ifdef __cplusplus
}
#endif

#endif
