#ifndef WATCHY_WATCHDOG_H
#define WATCHY_WATCHDOG_H

#include <stdbool.h>

#include "watchy/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WATCHY_WATCHDOG_NOT_ENROLLED = 0,
    WATCHY_WATCHDOG_ENROLLED,
    WATCHY_WATCHDOG_MEMBERSHIP_ERROR,
} watchy_watchdog_membership_t;

typedef struct {
    watchy_watchdog_membership_t (*status)(void *context);
    bool (*enroll)(void *context);
    bool (*feed)(void *context);
    bool (*unenroll)(void *context);
    void *context;
} watchy_watchdog_ops_t;

typedef struct {
    const watchy_watchdog_ops_t *ops;
    bool active;
    bool owns_enrollment;
} watchy_watchdog_scope_t;

watchy_status_t watchy_watchdog_scope_begin(watchy_watchdog_scope_t *scope,
                                            const watchy_watchdog_ops_t *ops);
watchy_status_t watchy_watchdog_scope_feed(watchy_watchdog_scope_t *scope);
watchy_status_t watchy_watchdog_scope_end(watchy_watchdog_scope_t *scope);

#ifdef __cplusplus
}
#endif

#endif
