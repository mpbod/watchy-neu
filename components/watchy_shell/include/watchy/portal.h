#ifndef WATCHY_PORTAL_H
#define WATCHY_PORTAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "watchy/packages.h"
#include "watchy/settings.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHY_PORTAL_TOKEN_HEX_SIZE 32u
#define WATCHY_PORTAL_IDLE_TIMEOUT_MS (10u * 60u * 1000u)
#define WATCHY_PORTAL_INSTALL_RESERVE_BYTES (64u * 1024u)

typedef enum {
    WATCHY_PORTAL_METHOD_GET = 0,
    WATCHY_PORTAL_METHOD_POST,
    WATCHY_PORTAL_METHOD_DELETE,
} watchy_portal_method_t;

typedef enum {
    WATCHY_PORTAL_ROUTE_NONE = 0,
    WATCHY_PORTAL_ROUTE_PAGE,
    WATCHY_PORTAL_ROUTE_STATUS,
    WATCHY_PORTAL_ROUTE_PACKAGES,
    WATCHY_PORTAL_ROUTE_UPLOAD,
    WATCHY_PORTAL_ROUTE_ACTIVATE,
    WATCHY_PORTAL_ROUTE_REMOVE,
} watchy_portal_route_action_t;

typedef struct {
    watchy_portal_route_action_t action;
    char identifier[WATCHY_PACKAGE_ID_MAX + 1u];
    char version[WATCHY_PACKAGE_VERSION_MAX + 1u];
} watchy_portal_route_t;

typedef enum {
    WATCHY_PORTAL_OK = 0,
    WATCHY_PORTAL_ERR_UNAUTHORIZED,
    WATCHY_PORTAL_ERR_CONTENT_TYPE,
    WATCHY_PORTAL_ERR_LENGTH_REQUIRED,
    WATCHY_PORTAL_ERR_TOO_LARGE,
    WATCHY_PORTAL_ERR_LOW_BATTERY,
    WATCHY_PORTAL_ERR_STORAGE,
    WATCHY_PORTAL_ERR_STORAGE_SPACE,
    WATCHY_PORTAL_ERR_UPLOAD_BUSY,
    WATCHY_PORTAL_ERR_INVALID_ROUTE,
} watchy_portal_policy_status_t;

typedef struct {
    const char *content_type;
    size_t content_length;
    bool content_length_known;
    bool chunked;
    uint16_t battery_mv;
    bool storage_available;
    size_t free_bytes;
    bool upload_in_progress;
} watchy_portal_upload_request_t;

typedef struct {
    uint16_t http_status;
    const char *code;
} watchy_portal_error_response_t;

typedef enum {
    WATCHY_PORTAL_NETWORK_AP = 0,
    WATCHY_PORTAL_NETWORK_CLIENT,
} watchy_portal_network_mode_t;

typedef struct {
    char network_name[33];
    char network_secret[65];
    char address[16];
    char token[WATCHY_PORTAL_TOKEN_HEX_SIZE + 1u];
    bool client_mode;
} watchy_portal_session_info_t;

typedef struct {
    bool (*enable)(void *context);
    bool (*fill)(void *context, uint8_t *bytes, size_t size);
    void (*disable)(void *context);
    void *context;
} watchy_portal_entropy_api_t;

typedef enum {
    WATCHY_PORTAL_UPLOAD_IO_CLIENT = 0,
    WATCHY_PORTAL_UPLOAD_IO_PACKAGE,
} watchy_portal_upload_io_failure_t;

bool watchy_portal_token_authorized(const char *session_token,
                                    const char *request_token);
bool watchy_portal_parse_route(watchy_portal_method_t method,
                               const char *path,
                               watchy_portal_route_t *out_route);
watchy_portal_policy_status_t watchy_portal_check_upload(
    const watchy_portal_upload_request_t *request);
watchy_portal_error_response_t watchy_portal_error_from_policy(
    watchy_portal_policy_status_t status);
watchy_portal_error_response_t watchy_portal_map_package_error(
    watchy_package_status_t status);
watchy_portal_error_response_t watchy_portal_map_upload_io_error(
    watchy_portal_upload_io_failure_t failure,
    watchy_package_status_t package_status);
bool watchy_portal_generate_ap_password(const watchy_portal_entropy_api_t *entropy,
                                        char *out_password,
                                        size_t out_size);
bool watchy_portal_fill_guaranteed_entropy(const watchy_portal_entropy_api_t *entropy,
                                           uint8_t *out_bytes,
                                           size_t size);
bool watchy_portal_idle_expired(uint64_t last_activity_ms, uint64_t now_ms);

watchy_status_t watchy_portal_prepare_ap_password(void);
watchy_status_t watchy_portal_start(watchy_portal_network_mode_t mode,
                                    const watchy_settings_t *settings,
                                    watchy_portal_session_info_t *out_info);
bool watchy_portal_active(void);
bool watchy_portal_timed_out(void);
watchy_status_t watchy_portal_stop(void);

#ifdef __cplusplus
}
#endif

#endif
