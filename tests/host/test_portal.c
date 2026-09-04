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
    static const char token[] = "00112233445566778899aabbccddeeff";
    CHECK(!watchy_portal_token_authorized(token, NULL));
    CHECK(!watchy_portal_token_authorized(token, "00112233445566778899aabbccddee"));
    CHECK(!watchy_portal_token_authorized(token, "00112233445566778899aabbccddeeff0"));
    CHECK(!watchy_portal_token_authorized(token, "00112233445566778899aabbccddeefg"));
    CHECK(!watchy_portal_token_authorized("short", "short"));
    CHECK(watchy_portal_token_authorized(token, token));
    return 0;
}

static int test_every_route_uses_an_out_of_band_basic_session_credential(void) {
    static const char token[] = "00112233445566778899aabbccddeeff";
    static const char authorization[] =
        "Basic d2F0Y2h5OjAwMTEyMjMzNDQ1NTY2Nzc4ODk5YWFiYmNjZGRlZWZm";
    CHECK(!watchy_portal_basic_authorized(token, NULL));
    CHECK(!watchy_portal_basic_authorized(token, "Basic"));
    CHECK(!watchy_portal_basic_authorized(token,
        "Basic d2F0Y2h5OjAwMTEyMjMzNDQ1NTY2Nzc4ODk5YWFiYmNjZGRlZWZh"));
    CHECK(!watchy_portal_basic_authorized(token,
        "Bearer 00112233445566778899aabbccddeeff"));
    CHECK(watchy_portal_basic_authorized(token, authorization));

    watchy_portal_session_info_t info = {
        .network_name = "Watchy-A1B2C3",
        .network_secret = "temporary-pass",
        .address = "192.168.4.1",
        .token = "00112233445566778899aabbccddeeff",
    };
    char instructions[192];
    CHECK(watchy_portal_format_watch_instructions(&info, instructions,
                                                   sizeof(instructions)));
    CHECK(strstr(instructions, "USER watchy\n") != NULL);
    CHECK(strstr(instructions, "AUTH 0011223344556677\n") != NULL);
    CHECK(strstr(instructions, "     8899aabbccddeeff\n") != NULL);
    for (const char *line = instructions; line != NULL && *line != '\0';) {
        const char *end = strchr(line, '\n');
        const size_t length = end == NULL ? strlen(line) : (size_t)(end - line);
        CHECK(length <= 32u);
        line = end == NULL ? NULL : end + 1u;
    }
    memset(info.network_name, 's', 32u);
    info.network_name[32] = '\0';
    info.network_secret[0] = '\0';
    info.client_mode = true;
    CHECK(watchy_portal_format_watch_instructions(&info, instructions,
                                                   sizeof(instructions)));
    CHECK(strstr(instructions, "SSID sssssssssssssssssssssssssss\n     sssss") != NULL);
    for (const char *line = instructions; line != NULL && *line != '\0';) {
        const char *end = strchr(line, '\n');
        const size_t length = end == NULL ? strlen(line) : (size_t)(end - line);
        CHECK(length <= 32u);
        line = end == NULL ? NULL : end + 1u;
    }
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
                                    "/api/v1/wifi", &route));
    CHECK(route.action == WATCHY_PORTAL_ROUTE_PROVISION_WIFI);
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
        .storage_available = true,
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
    request.storage_available = false;
    CHECK(watchy_portal_check_upload(&request) == WATCHY_PORTAL_ERR_STORAGE);
    CHECK(watchy_portal_error_from_policy(WATCHY_PORTAL_ERR_STORAGE).http_status == 507u);
    CHECK(strcmp(watchy_portal_error_from_policy(WATCHY_PORTAL_ERR_STORAGE).code,
                 "storage_error") == 0);
    request = valid_upload();
    request.free_bytes--;
    CHECK(watchy_portal_check_upload(&request) == WATCHY_PORTAL_ERR_STORAGE_SPACE);
    request = valid_upload();
    request.upload_in_progress = true;
    CHECK(watchy_portal_check_upload(&request) == WATCHY_PORTAL_ERR_UPLOAD_BUSY);
    return 0;
}

typedef struct {
    bool enable_ok;
    bool fill_ok;
    unsigned enable_calls;
    unsigned fill_calls;
    unsigned disable_calls;
} entropy_probe_t;

static bool entropy_enable(void *context) {
    entropy_probe_t *probe = context;
    ++probe->enable_calls;
    return probe->enable_ok;
}

static bool entropy_fill(void *context, uint8_t *bytes, size_t size) {
    entropy_probe_t *probe = context;
    ++probe->fill_calls;
    if (!probe->fill_ok) return false;
    for (size_t index = 0u; index < size; ++index) bytes[index] = (uint8_t)(index + 1u);
    return true;
}

static void entropy_disable(void *context) {
    ++((entropy_probe_t *)context)->disable_calls;
}

static int test_ap_password_generation_brackets_a_guaranteed_entropy_source(void) {
    entropy_probe_t probe = {.enable_ok = true, .fill_ok = true};
    const watchy_portal_entropy_api_t entropy = {
        .enable = entropy_enable, .fill = entropy_fill, .disable = entropy_disable,
        .context = &probe,
    };
    char password[65];
    CHECK(watchy_portal_generate_ap_password(&entropy, password, sizeof(password)));
    CHECK(strlen(password) == 8u);
    CHECK(probe.enable_calls == 1u && probe.fill_calls == 1u && probe.disable_calls == 1u);

    probe = (entropy_probe_t){.enable_ok = false, .fill_ok = true};
    CHECK(!watchy_portal_generate_ap_password(&entropy, password, sizeof(password)));
    CHECK(probe.enable_calls == 1u && probe.fill_calls == 0u && probe.disable_calls == 0u);
    probe = (entropy_probe_t){.enable_ok = true, .fill_ok = false};
    CHECK(!watchy_portal_generate_ap_password(&entropy, password, sizeof(password)));
    CHECK(probe.enable_calls == 1u && probe.fill_calls == 1u && probe.disable_calls == 1u);
    CHECK(password[0] == '\0');
    return 0;
}

static int test_ap_password_is_eight_unambiguous_characters(void) {
    entropy_probe_t probe = {.enable_ok = true, .fill_ok = true};
    const watchy_portal_entropy_api_t entropy = {
        .enable = entropy_enable, .fill = entropy_fill, .disable = entropy_disable,
        .context = &probe,
    };
    char password[WATCHY_PORTAL_AP_PASSWORD_SIZE + 1u];
    uint8_t digest[32] = {0};

    CHECK(WATCHY_PORTAL_AP_PASSWORD_SIZE == 8u);
    CHECK(watchy_portal_generate_ap_password(&entropy, password, sizeof(password)));
    CHECK(strlen(password) == 8u);
    CHECK(strpbrk(password, "01OIl") == NULL);
    CHECK(!watchy_portal_generate_ap_password(&entropy, password, sizeof(password) - 1u));
    CHECK(watchy_portal_password_from_digest(digest, password, sizeof(password)));
    CHECK(strlen(password) == WATCHY_PORTAL_AP_PASSWORD_SIZE);
    return 0;
}

static int test_upload_transport_and_storage_failures_have_distinct_public_errors(void) {
    watchy_portal_error_response_t response =
        watchy_portal_map_upload_io_error(WATCHY_PORTAL_UPLOAD_IO_CLIENT,
                                          WATCHY_PACKAGE_OK);
    CHECK(response.http_status == 400u);
    CHECK(strcmp(response.code, "upload_incomplete") == 0);
    response = watchy_portal_map_upload_io_error(WATCHY_PORTAL_UPLOAD_IO_PACKAGE,
                                                 WATCHY_PACKAGE_ERR_FILESYSTEM);
    CHECK(response.http_status == 507u);
    CHECK(strcmp(response.code, "storage_error") == 0);
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

static int test_only_authenticated_activity_refreshes_idle_before_absolute_expiry(void) {
    uint64_t last = 1000u;
    CHECK(!watchy_portal_session_accept(1000u, last, 2000u, false, &last));
    CHECK(last == 1000u);
    CHECK(watchy_portal_session_accept(1000u, last, 2000u, true, &last));
    CHECK(last == 2000u);
    CHECK(!watchy_portal_session_accept(
        1000u, last, 1000u + WATCHY_PORTAL_ABSOLUTE_TIMEOUT_MS, true, &last));
    CHECK(last == 2000u);
    CHECK(!watchy_portal_session_accept(
        UINT64_MAX - 100u, UINT64_MAX - 50u, 25u, false, &last));
    return 0;
}

static int test_idle_lifetime_is_five_minutes_with_thirty_minute_cap(void) {
    uint64_t last = 1000u;
    CHECK(!watchy_portal_idle_expired(last, last + 299999u));
    CHECK(watchy_portal_idle_expired(last, last + 300000u));
    CHECK(WATCHY_PORTAL_ABSOLUTE_TIMEOUT_MS == 1800000u);
    CHECK(watchy_portal_session_accept(1000u, last, last + 299999u, true, &last));
    CHECK(!watchy_portal_session_accept(1000u, last, 1000u + 1800000u, true, &last));
    return 0;
}

int main(void) {
    int failures = 0;
    failures += test_mutating_routes_require_the_exact_session_token();
    failures += test_every_route_uses_an_out_of_band_basic_session_credential();
    failures += test_route_parser_accepts_only_exact_valid_components();
    failures += test_upload_policy_enforces_content_size_battery_storage_and_exclusion();
    failures += test_package_failures_map_to_stable_public_errors();
    failures += test_ap_password_generation_brackets_a_guaranteed_entropy_source();
    failures += test_ap_password_is_eight_unambiguous_characters();
    failures += test_upload_transport_and_storage_failures_have_distinct_public_errors();
    failures += test_idle_deadline_is_activity_relative_and_overflow_safe();
    failures += test_only_authenticated_activity_refreshes_idle_before_absolute_expiry();
    failures += test_idle_lifetime_is_five_minutes_with_thirty_minute_cap();
    if (failures == 0) {
        puts("portal tests passed");
    }
    return failures == 0 ? 0 : 1;
}
