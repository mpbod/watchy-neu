#include "watchy/radios.h"

bool watchy_wifi_should_reconnect(watchy_wifi_state_t state) {
    return state == WATCHY_WIFI_STA_STARTING || state == WATCHY_WIFI_STA_CONNECTED;
}
