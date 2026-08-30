#include "watchy/portal.h"

#include <string.h>

static bool copy_component(const char *start, size_t length, char *out, size_t capacity) {
    if (length == 0u || length >= capacity) {
        return false;
    }
    memcpy(out, start, length);
    out[length] = '\0';
    return true;
}

static bool url_component_safe(const char *component) {
    for (size_t index = 0u; component[index] != '\0'; ++index) {
        const unsigned char value = (unsigned char)component[index];
        if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '.' || value == '_' ||
              value == '-')) {
            return false;
        }
    }
    return component[0] != '\0';
}

bool watchy_portal_token_authorized(const char *session_token,
                                    const char *request_token) {
    unsigned char difference = 0u;
    if (session_token == NULL || request_token == NULL ||
        strnlen(session_token, WATCHY_PORTAL_TOKEN_HEX_SIZE + 1u) !=
            WATCHY_PORTAL_TOKEN_HEX_SIZE ||
        strnlen(request_token, WATCHY_PORTAL_TOKEN_HEX_SIZE + 1u) !=
            WATCHY_PORTAL_TOKEN_HEX_SIZE) {
        return false;
    }
    for (size_t index = 0u; index < WATCHY_PORTAL_TOKEN_HEX_SIZE; ++index) {
        const char session = session_token[index];
        const char request = request_token[index];
        const bool session_hex = (session >= '0' && session <= '9') ||
                                 (session >= 'a' && session <= 'f') ||
                                 (session >= 'A' && session <= 'F');
        const bool request_hex = (request >= '0' && request <= '9') ||
                                 (request >= 'a' && request <= 'f') ||
                                 (request >= 'A' && request <= 'F');
        if (!session_hex || !request_hex) return false;
        difference |= (unsigned char)session_token[index] ^ (unsigned char)request_token[index];
    }
    return difference == 0u;
}

bool watchy_portal_parse_route(watchy_portal_method_t method,
                               const char *path,
                               watchy_portal_route_t *out_route) {
    static const char activate_prefix[] = "/api/v1/watchface/";
    static const char package_prefix[] = "/api/v1/packages/";
    const char *first;
    const char *second;
    const char *suffix;
    if (path == NULL || out_route == NULL || strchr(path, '?') != NULL ||
        strchr(path, '#') != NULL || strchr(path, '%') != NULL) {
        return false;
    }
    memset(out_route, 0, sizeof(*out_route));
    if (method == WATCHY_PORTAL_METHOD_GET && strcmp(path, "/") == 0) {
        out_route->action = WATCHY_PORTAL_ROUTE_PAGE;
        return true;
    }
    if (method == WATCHY_PORTAL_METHOD_GET && strcmp(path, "/api/v1/status") == 0) {
        out_route->action = WATCHY_PORTAL_ROUTE_STATUS;
        return true;
    }
    if (strcmp(path, "/api/v1/packages") == 0) {
        if (method == WATCHY_PORTAL_METHOD_GET) {
            out_route->action = WATCHY_PORTAL_ROUTE_PACKAGES;
            return true;
        }
        if (method == WATCHY_PORTAL_METHOD_POST) {
            out_route->action = WATCHY_PORTAL_ROUTE_UPLOAD;
            return true;
        }
        return false;
    }
    if (method == WATCHY_PORTAL_METHOD_POST &&
        strncmp(path, activate_prefix, sizeof(activate_prefix) - 1u) == 0) {
        first = path + sizeof(activate_prefix) - 1u;
        second = strchr(first, '/');
        if (second == NULL) {
            return false;
        }
        suffix = strchr(second + 1u, '/');
        if (suffix == NULL || strcmp(suffix, "/activate") != 0 ||
            !copy_component(first, (size_t)(second - first), out_route->identifier,
                            sizeof(out_route->identifier)) ||
            !copy_component(second + 1u, (size_t)(suffix - second - 1u), out_route->version,
                            sizeof(out_route->version)) ||
            !watchy_package_id_valid(out_route->identifier) ||
            !watchy_package_version_valid(out_route->version) ||
            !url_component_safe(out_route->version)) {
            return false;
        }
        out_route->action = WATCHY_PORTAL_ROUTE_ACTIVATE;
        return true;
    }
    if (method == WATCHY_PORTAL_METHOD_DELETE &&
        strncmp(path, package_prefix, sizeof(package_prefix) - 1u) == 0) {
        first = path + sizeof(package_prefix) - 1u;
        second = strchr(first, '/');
        if (second == NULL || strchr(second + 1u, '/') != NULL ||
            !copy_component(first, (size_t)(second - first), out_route->identifier,
                            sizeof(out_route->identifier)) ||
            !copy_component(second + 1u, strlen(second + 1u), out_route->version,
                            sizeof(out_route->version)) ||
            !watchy_package_id_valid(out_route->identifier) ||
            !watchy_package_version_valid(out_route->version) ||
            !url_component_safe(out_route->version)) {
            return false;
        }
        out_route->action = WATCHY_PORTAL_ROUTE_REMOVE;
        return true;
    }
    return false;
}

watchy_portal_policy_status_t watchy_portal_check_upload(
    const watchy_portal_upload_request_t *request) {
    if (request == NULL) {
        return WATCHY_PORTAL_ERR_LENGTH_REQUIRED;
    }
    if (request->content_type == NULL ||
        strcmp(request->content_type, "application/octet-stream") != 0) {
        return WATCHY_PORTAL_ERR_CONTENT_TYPE;
    }
    if (!request->content_length_known || request->chunked || request->content_length == 0u) {
        return WATCHY_PORTAL_ERR_LENGTH_REQUIRED;
    }
    if (request->content_length > WATCHY_PACKAGE_WPK_BYTES_MAX) {
        return WATCHY_PORTAL_ERR_TOO_LARGE;
    }
    if (request->upload_in_progress) {
        return WATCHY_PORTAL_ERR_UPLOAD_BUSY;
    }
    if (request->battery_mv < 3550u) {
        return WATCHY_PORTAL_ERR_LOW_BATTERY;
    }
    if (!request->storage_available) {
        return WATCHY_PORTAL_ERR_STORAGE;
    }
    if (request->free_bytes < request->content_length ||
        request->free_bytes - request->content_length < WATCHY_PORTAL_INSTALL_RESERVE_BYTES) {
        return WATCHY_PORTAL_ERR_STORAGE_SPACE;
    }
    return WATCHY_PORTAL_OK;
}

watchy_portal_error_response_t watchy_portal_error_from_policy(
    watchy_portal_policy_status_t status) {
    switch (status) {
    case WATCHY_PORTAL_ERR_UNAUTHORIZED:
        return (watchy_portal_error_response_t){401u, "unauthorized"};
    case WATCHY_PORTAL_ERR_CONTENT_TYPE:
        return (watchy_portal_error_response_t){415u, "content_type"};
    case WATCHY_PORTAL_ERR_LENGTH_REQUIRED:
        return (watchy_portal_error_response_t){411u, "content_length"};
    case WATCHY_PORTAL_ERR_TOO_LARGE:
        return (watchy_portal_error_response_t){413u, "package_too_large"};
    case WATCHY_PORTAL_ERR_LOW_BATTERY:
        return (watchy_portal_error_response_t){409u, "low_battery"};
    case WATCHY_PORTAL_ERR_STORAGE:
        return (watchy_portal_error_response_t){507u, "storage_error"};
    case WATCHY_PORTAL_ERR_STORAGE_SPACE:
        return (watchy_portal_error_response_t){507u, "insufficient_storage"};
    case WATCHY_PORTAL_ERR_UPLOAD_BUSY:
        return (watchy_portal_error_response_t){409u, "upload_busy"};
    case WATCHY_PORTAL_ERR_INVALID_ROUTE:
        return (watchy_portal_error_response_t){404u, "not_found"};
    case WATCHY_PORTAL_OK:
        break;
    }
    return (watchy_portal_error_response_t){500u, "internal_error"};
}

watchy_portal_error_response_t watchy_portal_map_upload_io_error(
    watchy_portal_upload_io_failure_t failure,
    watchy_package_status_t package_status) {
    if (failure == WATCHY_PORTAL_UPLOAD_IO_CLIENT) {
        return (watchy_portal_error_response_t){400u, "upload_incomplete"};
    }
    return watchy_portal_map_package_error(package_status);
}

bool watchy_portal_fill_guaranteed_entropy(const watchy_portal_entropy_api_t *entropy,
                                           uint8_t *out_bytes,
                                           size_t size) {
    bool filled;
    if (out_bytes == NULL || size == 0u || entropy == NULL || entropy->enable == NULL ||
        entropy->fill == NULL || entropy->disable == NULL ||
        !entropy->enable(entropy->context)) {
        return false;
    }
    filled = entropy->fill(entropy->context, out_bytes, size);
    entropy->disable(entropy->context);
    if (!filled) memset(out_bytes, 0, size);
    return filled;
}

bool watchy_portal_generate_ap_password(const watchy_portal_entropy_api_t *entropy,
                                        char *out_password,
                                        size_t out_size) {
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    uint8_t bytes[16];
    if (out_password == NULL || out_size < sizeof(bytes) + 1u) return false;
    out_password[0] = '\0';
    if (!watchy_portal_fill_guaranteed_entropy(entropy, bytes, sizeof(bytes))) return false;
    for (size_t index = 0u; index < sizeof(bytes); ++index) {
        out_password[index] = alphabet[bytes[index] % (sizeof(alphabet) - 1u)];
    }
    out_password[sizeof(bytes)] = '\0';
    memset(bytes, 0, sizeof(bytes));
    return true;
}

watchy_portal_error_response_t watchy_portal_map_package_error(
    watchy_package_status_t status) {
    switch (status) {
    case WATCHY_PACKAGE_ERR_DIGEST:
    case WATCHY_PACKAGE_ERR_CRYPTO:
    case WATCHY_PACKAGE_ERR_MANIFEST:
    case WATCHY_PACKAGE_ERR_UTF8:
    case WATCHY_PACKAGE_ERR_CAPABILITY:
    case WATCHY_PACKAGE_ERR_PATH:
    case WATCHY_PACKAGE_ERR_ELF:
    case WATCHY_PACKAGE_ERR_WPK:
    case WATCHY_PACKAGE_ERR_ASSETS:
    case WATCHY_PACKAGE_ERR_ABI:
    case WATCHY_PACKAGE_ERR_DESCRIPTOR:
        return (watchy_portal_error_response_t){422u, "invalid_package"};
    case WATCHY_PACKAGE_ERR_LIMIT:
        return (watchy_portal_error_response_t){413u, "package_too_large"};
    case WATCHY_PACKAGE_ERR_STORE:
    case WATCHY_PACKAGE_ERR_FILESYSTEM:
        return (watchy_portal_error_response_t){507u, "storage_error"};
    case WATCHY_PACKAGE_ERR_QUARANTINED:
        return (watchy_portal_error_response_t){409u, "quarantined"};
    case WATCHY_PACKAGE_ERR_COLLISION:
    case WATCHY_PACKAGE_ERR_STATE:
    case WATCHY_PACKAGE_ERR_SAFE_MODE:
        return (watchy_portal_error_response_t){409u, "conflict"};
    case WATCHY_PACKAGE_ERR_ARGUMENT:
        return (watchy_portal_error_response_t){400u, "invalid_request"};
    case WATCHY_PACKAGE_OK:
        return (watchy_portal_error_response_t){200u, "ok"};
    default:
        return (watchy_portal_error_response_t){500u, "internal_error"};
    }
}

bool watchy_portal_idle_expired(uint64_t last_activity_ms, uint64_t now_ms) {
    return now_ms - last_activity_ms >= WATCHY_PORTAL_IDLE_TIMEOUT_MS;
}
