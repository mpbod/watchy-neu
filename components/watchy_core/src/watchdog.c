#include "watchy/watchdog.h"

#include <string.h>

watchy_status_t watchy_watchdog_scope_begin(watchy_watchdog_scope_t *scope,
                                            const watchy_watchdog_ops_t *ops) {
    watchy_watchdog_membership_t membership;
    bool owns_enrollment = false;

    if (scope == NULL || ops == NULL || ops->status == NULL || ops->enroll == NULL ||
        ops->feed == NULL || ops->unenroll == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    memset(scope, 0, sizeof(*scope));
    membership = ops->status(ops->context);
    if (membership == WATCHY_WATCHDOG_MEMBERSHIP_ERROR) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (membership == WATCHY_WATCHDOG_NOT_ENROLLED) {
        if (!ops->enroll(ops->context)) {
            return WATCHY_STATUS_INVALID_STATE;
        }
        owns_enrollment = true;
        if (ops->status(ops->context) != WATCHY_WATCHDOG_ENROLLED) {
            (void)ops->unenroll(ops->context);
            return WATCHY_STATUS_INVALID_STATE;
        }
    }

    scope->ops = ops;
    scope->active = true;
    scope->owns_enrollment = owns_enrollment;
    if (watchy_watchdog_scope_feed(scope) != WATCHY_STATUS_OK) {
        (void)watchy_watchdog_scope_end(scope);
        return WATCHY_STATUS_INVALID_STATE;
    }
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_watchdog_scope_feed(watchy_watchdog_scope_t *scope) {
    if (scope == NULL || !scope->active || scope->ops == NULL || scope->ops->feed == NULL) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    return scope->ops->feed(scope->ops->context) ? WATCHY_STATUS_OK
                                                 : WATCHY_STATUS_INVALID_STATE;
}

watchy_status_t watchy_watchdog_scope_end(watchy_watchdog_scope_t *scope) {
    watchy_status_t status = WATCHY_STATUS_OK;

    if (scope == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (!scope->active) {
        return WATCHY_STATUS_OK;
    }
    if (scope->ops == NULL ||
        (scope->owns_enrollment &&
         (scope->ops->unenroll == NULL || !scope->ops->unenroll(scope->ops->context)))) {
        status = WATCHY_STATUS_INVALID_STATE;
    }
    memset(scope, 0, sizeof(*scope));
    return status;
}
