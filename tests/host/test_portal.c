#include <stdio.h>
#include <string.h>

#include "watchy/portal.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static int test_mutating_routes_require_the_exact_session_token(void) {
    CHECK(!watchy_portal_token_authorized("abc123", NULL));
    CHECK(!watchy_portal_token_authorized("abc123", "abc12"));
    CHECK(!watchy_portal_token_authorized("abc123", "abc1234"));
    CHECK(!watchy_portal_token_authorized("abc123", "ABC123"));
    CHECK(watchy_portal_token_authorized("abc123", "abc123"));
    return 0;
}

static int test_route_parser_accepts_only_exact_valid_components(void) {
    watchy_portal_route_t route;

    CHECK(watchy_portal_parse_route(WATCHY_PORTAL_METHOD_GET,
                                    "/api/v1/status", &route));
    CHECK(route.action == WATCHY_PORTAL_ROUTE_STATUS);
    CHECK(watchy_portal_parse_route(WATCHY_PORTAL_METHOD_GET,
                                    "/api/v1/packages", &route));
    CHECK(route.action == WATCHY_PORTAL_ROUTE_PACKAGES);
    CHECK(watchy_portal_parse_route(WATCHY_PORTAL_METHOD_POST,
                                    "/api/v1/packages", &route));
    CHECK(route.action == WATCHY_PORTAL_ROUTE_UPLOAD);
    CHECK(watchy_portal_parse_route(WATCHY_PORTAL_METHOD_POST,
                                    "/api/v1/watchface/clock.simple/1.2.3/activate", &route));
    CHECK(route.action == WATCHY_PORTAL_ROUTE_ACTIVATE);
    CHECK(strcmp(route.identifier, "clock.simple") == 0);
    CHECK(strcmp(route.version, "1.2.3") == 0);
    CHECK(watchy_portal_parse_route(WATCHY_PORTAL_METHOD_DELETE,
                                    "/api/v1/packages/app.timer/2/remove", &route) == false);
    CHECK(watchy_portal_parse_route(WATCHY_PORTAL_METHOD_DELETE,
                                    "/api/v1/packages/app.timer/2", &route));
    CHECK(route.action == WATCHY_PORTAL_ROUTE_REMOVE);

    CHECK(!watchy_portal_parse_route(WATCHY_PORTAL_METHOD_GET,
                                     "/api/v1/status?password=x", &route));
    CHECK(!watchy_portal_parse_route(WATCHY_PORTAL_METHOD_POST,
                                     "/api/v1/watchface/Clock/1/activate", &route));
    CHECK(!watchy_portal_parse_route(WATCHY_PORTAL_METHOD_POST,
                                     "/api/v1/watchface/a/%2e%2e/activate", &route));
    CHECK(!watchy_portal_parse_route(WATCHY_PORTAL_METHOD_POST,
                                     "/api/v1/watchface/a/version with space/activate", &route));
    CHECK(!watchy_portal_parse_route(WATCHY_PORTAL_METHOD_DELETE,
                                     "/api/v1/packages/a/1%22", &route));
    CHECK(!watchy_portal_parse_route(WATCHY_PORTAL_METHOD_DELETE,
                                     "/api/v1/packages/a/1\"", &route));
    CHECK(!watchy_portal_parse_route(WATCHY_PORTAL_METHOD_DELETE,
                                     "/api/v1/packages/a/1/extra", &route));
    CHECK(!watchy_portal_parse_route(WATCHY_PORTAL_METHOD_GET,
                                     "/api/v1/packages/a/1", &route));
    return 0;
}

static watchy_portal_upload_request_t valid_upload(void) {
    watchy_portal_upload_request_t request = {
        .content_type = "application/octet-stream",
        .content_length = 1024u,
        .content_length_known = true,
        .chunked = false,
        .battery_mv = 3550u,
        .free_bytes = 1024u + WATCHY_PORTAL_INSTALL_RESERVE_BYTES,
        .upload_in_progress = false,
    };
    return request;
}

static int test_upload_policy_enforces_content_size_battery_storage_and_exclusion(void) {
    watchy_portal_upload_request_t request = valid_upload();
    CHECK(watchy_portal_check_upload(&request) == WATCHY_PORTAL_OK);

    request.content_type = "Application/Octet-Stream";
    CHECK(watchy_portal_check_upload(&request) == WATCHY_PORTAL_ERR_CONTENT_TYPE);
    request = valid_upload();
    request.chunked = true;
    CHECK(watchy_portal_check_upload(&request) == WATCHY_PORTAL_ERR_LENGTH_REQUIRED);
    request = valid_upload();
    request.content_length_known = false;
    CHECK(watchy_portal_check_upload(&request) == WATCHY_PORTAL_ERR_LENGTH_REQUIRED);
    request = valid_upload();
    request.content_length = 0u;
    CHECK(watchy_portal_check_upload(&request) == WATCHY_PORTAL_ERR_LENGTH_REQUIRED);
    request = valid_upload();
    request.content_length = WATCHY_PACKAGE_WPK_BYTES_MAX + 1u;
    CHECK(watchy_portal_check_upload(&request) == WATCHY_PORTAL_ERR_TOO_LARGE);
    request = valid_upload();
    request.battery_mv = 3549u;
    CHECK(watchy_portal_check_upload(&request) == WATCHY_PORTAL_ERR_LOW_BATTERY);
    request = valid_upload();
    request.free_bytes--;
    CHECK(watchy_portal_check_upload(&request) == WATCHY_PORTAL_ERR_STORAGE_SPACE);
    request = valid_upload();
    request.upload_in_progress = true;
    CHECK(watchy_portal_check_upload(&request) == WATCHY_PORTAL_ERR_UPLOAD_BUSY);
    return 0;
}

static int test_package_failures_map_to_stable_public_errors(void) {
    watchy_portal_error_response_t response;

    response = watchy_portal_map_package_error(WATCHY_PACKAGE_ERR_MANIFEST);
    CHECK(response.http_status == 422u);
    CHECK(strcmp(response.code, "invalid_package") == 0);
    response = watchy_portal_map_package_error(WATCHY_PACKAGE_ERR_FILESYSTEM);
    CHECK(response.http_status == 507u);
    CHECK(strcmp(response.code, "storage_error") == 0);
    response = watchy_portal_map_package_error(WATCHY_PACKAGE_ERR_QUARANTINED);
    CHECK(response.http_status == 409u);
    CHECK(strcmp(response.code, "quarantined") == 0);
    response = watchy_portal_map_package_error(WATCHY_PACKAGE_ERR_LIMIT);
    CHECK(response.http_status == 413u);
    CHECK(strcmp(response.code, "package_too_large") == 0);
    CHECK(watchy_portal_error_from_policy(WATCHY_PORTAL_ERR_UNAUTHORIZED).http_status == 401u);
    return 0;
}

static int test_idle_deadline_is_activity_relative_and_overflow_safe(void) {
    CHECK(!watchy_portal_idle_expired(1000u, 1000u + WATCHY_PORTAL_IDLE_TIMEOUT_MS - 1u));
    CHECK(watchy_portal_idle_expired(1000u, 1000u + WATCHY_PORTAL_IDLE_TIMEOUT_MS));
    CHECK(!watchy_portal_idle_expired(UINT64_MAX - 10u, 5u));
    return 0;
}

int main(void) {
    int failures = 0;
    failures += test_mutating_routes_require_the_exact_session_token();
    failures += test_route_parser_accepts_only_exact_valid_components();
    failures += test_upload_policy_enforces_content_size_battery_storage_and_exclusion();
    failures += test_package_failures_map_to_stable_public_errors();
    failures += test_idle_deadline_is_activity_relative_and_overflow_safe();
    if (failures == 0) {
        puts("portal tests passed");
    }
    return failures == 0 ? 0 : 1;
}
