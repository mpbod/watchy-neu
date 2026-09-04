#include "watchy/timezone_action.h"

watchy_status_t watchy_timezone_action_apply(
    watchy_settings_t *settings,
    const watchy_settings_t *candidate,
    watchy_time_t *time,
    const watchy_timezone_action_ops_t *ops) {
    watchy_settings_t prior;
    watchy_settings_t next;
    int16_t prior_offset;
    int16_t candidate_offset;
    bool offset_changed;
    watchy_status_t status;

    if (settings == NULL || candidate == NULL || time == NULL || ops == NULL ||
        ops->set_rtc_offset == NULL || ops->save_settings == NULL ||
        ops->read_local == NULL) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    prior = *settings;
    next = *candidate;
    if (!watchy_settings_valid(&next) ||
        !watchy_settings_timezone_offset(&next, &candidate_offset)) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    offset_changed = !watchy_settings_timezone_offset(&prior, &prior_offset) ||
                     prior_offset != candidate_offset;
    if (offset_changed) {
        status = ops->set_rtc_offset(ops->context, candidate_offset);
        if (status != WATCHY_STATUS_OK) {
            return status;
        }
    }
    status = ops->save_settings(ops->context, &next);
    if (status != WATCHY_STATUS_OK) {
        if (offset_changed &&
            ops->set_rtc_offset(ops->context, time->utc_offset_minutes) !=
                WATCHY_STATUS_OK) {
            return WATCHY_STATUS_INVALID_STATE;
        }
        *settings = prior;
        return status;
    }
    *settings = next;
    return ops->read_local(ops->context, time);
}
