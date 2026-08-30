#include "watchy/runtime.h"

#include <stddef.h>

void watchy_refresh_policy_reset(watchy_refresh_policy_t *policy, uint16_t partial_limit) {
    if (policy == NULL) {
        return;
    }

    policy->partial_limit = partial_limit;
    policy->partial_count = 0;
}

watchy_refresh_mode_t watchy_refresh_decide(watchy_refresh_policy_t *policy, watchy_refresh_mode_t requested) {
    uint32_t next_count = 0;

    if (policy == NULL) {
        return WATCHY_REFRESH_FULL;
    }
    if (requested == WATCHY_REFRESH_FULL) {
        policy->partial_count = 0;
        return WATCHY_REFRESH_FULL;
    }
    if (policy->partial_limit == 0) {
        policy->partial_count = 0;
        return WATCHY_REFRESH_FULL;
    }

    next_count = (uint32_t)policy->partial_count + 1u;
    if (next_count >= policy->partial_limit) {
        policy->partial_count = 0;
        return WATCHY_REFRESH_FULL;
    }

    policy->partial_count = (uint16_t)next_count;
    return WATCHY_REFRESH_PARTIAL;
}
