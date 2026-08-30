#ifndef WATCHY_WIFI_CREDENTIALS_H
#define WATCHY_WIFI_CREDENTIALS_H

/* The built-in settings service and package-network host must share the same
 * NVS records so credentials provisioned through the AP portal are reusable. */
#define WATCHY_WIFI_CREDENTIAL_NAMESPACE "watchy_cfg"
#define WATCHY_WIFI_CREDENTIAL_SSID_KEY "ssid"
#define WATCHY_WIFI_CREDENTIAL_PASSWORD_KEY "wifi_pass"

#endif
