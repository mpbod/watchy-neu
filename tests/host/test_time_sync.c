#include "watchy/time_sync.h"

#include <stdio.h>

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__,          \
                    #condition);                                                       \
            return 1;                                                                  \
        }                                                                              \
    } while (0)

static int test_ntp_ownership_distinguishes_watch_client_portal_and_ap(void) {
    CHECK(watchy_time_sync_decide(true, false, WATCHY_PORTAL_NETWORK_AP) ==
          WATCHY_TIME_SYNC_START_SAVED_WIFI);
    CHECK(watchy_time_sync_decide(true, true, WATCHY_PORTAL_NETWORK_CLIENT) ==
          WATCHY_TIME_SYNC_BORROW_CONNECTED_WIFI);
    CHECK(watchy_time_sync_decide(true, true, WATCHY_PORTAL_NETWORK_AP) ==
          WATCHY_TIME_SYNC_REJECT_CLIENT_MODE_REQUIRED);
    CHECK(watchy_time_sync_decide(false, false, WATCHY_PORTAL_NETWORK_AP) ==
          WATCHY_TIME_SYNC_REJECT_NO_WIFI);
    return 0;
}

static int test_fixed_home_zones_convert_the_synchronized_utc_epoch(void) {
    watchy_settings_t settings;
    watchy_time_t local;

    watchy_settings_defaults(&settings);
    CHECK(watchy_settings_set_timezone_offset(&settings, 420));
    CHECK(watchy_time_sync_local_from_epoch(&settings, INT64_C(1704067200), &local) ==
          WATCHY_STATUS_OK);
    CHECK(local.year == 2024);
    CHECK(local.month == 1u);
    CHECK(local.day == 1u);
    CHECK(local.hour == 7u);
    CHECK(local.minute == 0u);
    CHECK(local.second == 0u);
    CHECK(local.utc_offset_minutes == 420);

    CHECK(watchy_settings_set_timezone_offset(&settings, -210));
    CHECK(watchy_time_sync_local_from_epoch(&settings, INT64_C(1704067200), &local) ==
          WATCHY_STATUS_OK);
    CHECK(local.year == 2023);
    CHECK(local.month == 12u);
    CHECK(local.day == 31u);
    CHECK(local.hour == 20u);
    CHECK(local.minute == 30u);
    CHECK(local.second == 0u);
    CHECK(local.utc_offset_minutes == -210);
    return 0;
}

int main(void) {
    int failures = 0;
    failures += test_ntp_ownership_distinguishes_watch_client_portal_and_ap();
    failures += test_fixed_home_zones_convert_the_synchronized_utc_epoch();
    if (failures == 0) {
        puts("time sync tests passed");
    }
    return failures == 0 ? 0 : 1;
}
