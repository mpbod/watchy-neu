#include "watchy/portal.h"

#include "watchy/battery.h"
#include "watchy/package_runtime.h"
#include "watchy/radios.h"
#include "watchy/storage.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WATCHY_PORTAL_RECEIVE_CHUNK 1024u
#define WATCHY_PORTAL_CLIENT_TIMEOUT_MS 15000u

typedef struct {
    httpd_handle_t server;
    watchy_portal_session_info_t info;
    uint64_t last_activity_ms;
    bool running;
} portal_state_t;

static portal_state_t s_portal;
static portMUX_TYPE s_portal_mux = portMUX_INITIALIZER_UNLOCKED;

static const char PAGE_HEAD[] =
    "<!doctype html><html><head><meta charset=utf-8><meta name=viewport "
    "content=\"width=device-width,initial-scale=1\"><title>Watchy packages</title><style>"
    ":root{font:16px system-ui;color:#111;background:#eee}body{max-width:44rem;margin:auto;padding:1rem}"
    "h1{margin:.2rem 0}section{background:#fff;border:2px solid #111;border-radius:.5rem;padding:1rem;"
    "margin:1rem 0}button,input{font:inherit;padding:.65rem;margin:.25rem}.pkg{border-top:1px solid #999;"
    "padding:.7rem 0}.bad{color:#900}code{overflow-wrap:anywhere}</style></head><body>"
    "<h1>Watchy package portal</h1><section id=status>Loading status...</section>"
    "<section><h2>Install WPK</h2><input id=file type=file accept=.wpk><button id=upload>Upload</button>"
    "<div id=result></div></section><section><h2>Packages</h2><div id=packages>Loading...</div></section>"
    "<script>const TOKEN=\"";

static const char PAGE_SCRIPT[] =
    "\";const statusEl=document.getElementById('status'),packagesEl=document.getElementById('packages'),"
    "resultEl=document.getElementById('result'),fileEl=document.getElementById('file'),"
    "uploadEl=document.getElementById('upload');async function call(path,options={}){options.headers=options.headers||{};"
    "if(options.method&&options.method!=='GET')options.headers['X-Watchy-Token']=TOKEN;"
    "const r=await fetch(path,options),j=await r.json();if(!r.ok)throw Error(j.error?.code||'request_failed');"
    "return j}async function refresh(){try{const [s,p]=await Promise.all([call('/api/v1/status'),"
    "call('/api/v1/packages')]);statusEl.innerHTML='<h2>Status</h2><p>Battery '+s.battery.percent+'% ('+"
    "s.battery.mv+' mV)</p><p>Storage '+s.storage.free+' bytes free</p><p>Network '+s.network+'</p>';"
    "packagesEl.textContent='';if(!p.packages.length)packagesEl.textContent='No packages installed.';"
    "for(const x of p.packages){const d=document.createElement('div');d.className='pkg';"
    "const c=document.createElement('code');c.textContent=x.reference;d.append(c);"
    "d.append(document.createTextNode(' '+(x.active?'active ':'')+(x.pending?'pending ':'')));"
    "if(x.quarantined){const q=document.createElement('span');q.className='bad';q.textContent='quarantined';"
    "d.append(q)}d.append(document.createElement('br'));if(x.type==='watchface'){"
    "const a=document.createElement('button');a.textContent='Use watchface';"
    "a.onclick=async()=>{const [id,v]=x.reference.split('@');await call('/api/v1/watchface/'+id+'/'+v+"
    "'/activate',{method:'POST'});refresh()};d.append(a)}const b=document.createElement('button');"
    "b.textContent='Remove';b.onclick=async()=>{const [id,v]=x.reference.split('@');await call("
    "'/api/v1/packages/'+id+'/'+v,{method:'DELETE'});refresh()};d.append(b);packagesEl.append(d)}}"
    "catch(e){resultEl.textContent=e.message}}uploadEl.onclick=async()=>{const f=fileEl.files[0];if(!f)return;"
    "resultEl.textContent='Uploading...';try{const j=await call('/api/v1/packages',{method:'POST',"
    "headers:{'Content-Type':'application/octet-stream'},body:f});resultEl.textContent='Installed '+j.reference;"
    "refresh()}catch(e){resultEl.textContent=e.message}};refresh()</script></body></html>";

static uint64_t now_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static void mark_activity(void) {
    const uint64_t value = now_ms();
    portENTER_CRITICAL(&s_portal_mux);
    s_portal.last_activity_ms = value;
    portEXIT_CRITICAL(&s_portal_mux);
}

static const char *http_status_text(uint16_t status) {
    switch (status) {
    case 200u: return "200 OK";
    case 201u: return "201 Created";
    case 400u: return "400 Bad Request";
    case 401u: return "401 Unauthorized";
    case 404u: return "404 Not Found";
    case 409u: return "409 Conflict";
    case 411u: return "411 Length Required";
    case 413u: return "413 Payload Too Large";
    case 415u: return "415 Unsupported Media Type";
    case 422u: return "422 Unprocessable Content";
    case 507u: return "507 Insufficient Storage";
    default: return "500 Internal Server Error";
    }
}

static esp_err_t send_json(httpd_req_t *request,
                           uint16_t status,
                           const char *json) {
    httpd_resp_set_status(request, http_status_text(status));
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_public_error(httpd_req_t *request,
                                   watchy_portal_error_response_t error) {
    char body[96];
    const int length = snprintf(body, sizeof(body),
                                "{\"error\":{\"code\":\"%s\"}}", error.code);
    if (length < 0 || length >= (int)sizeof(body)) {
        return send_json(request, 500u, "{\"error\":{\"code\":\"internal_error\"}}");
    }
    return send_json(request, error.http_status, body);
}

static bool read_header(httpd_req_t *request,
                        const char *name,
                        char *value,
                        size_t capacity) {
    const size_t length = httpd_req_get_hdr_value_len(request, name);
    return length > 0u && length < capacity &&
           httpd_req_get_hdr_value_str(request, name, value, capacity) == ESP_OK;
}

static bool mutation_authorized(httpd_req_t *request) {
    char token[WATCHY_PORTAL_TOKEN_HEX_SIZE + 1u];
    return read_header(request, "X-Watchy-Token", token, sizeof(token)) &&
           watchy_portal_token_authorized(s_portal.info.token, token);
}

static esp_err_t send_page(httpd_req_t *request) {
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (httpd_resp_send_chunk(request, PAGE_HEAD, HTTPD_RESP_USE_STRLEN) != ESP_OK ||
        httpd_resp_send_chunk(request, s_portal.info.token, HTTPD_RESP_USE_STRLEN) != ESP_OK ||
        httpd_resp_send_chunk(request, PAGE_SCRIPT, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
        return ESP_FAIL;
    }
    return httpd_resp_send_chunk(request, NULL, 0u);
}

static esp_err_t send_status(httpd_req_t *request) {
    watchy_battery_state_t battery = {0};
    size_t total = 0u;
    size_t free_bytes = 0u;
    char body[320];
    const bool battery_ok = watchy_battery_read(&battery) == WATCHY_STATUS_OK;
    const bool storage_ok = watchy_storage_space(&total, &free_bytes) == WATCHY_STATUS_OK;
    const char *network = s_portal.info.client_mode ? "client" : "access_point";
    const int length = snprintf(body, sizeof(body),
        "{\"battery\":{\"available\":%s,\"mv\":%u,\"percent\":%u},"
        "\"storage\":{\"available\":%s,\"total\":%zu,\"free\":%zu},"
        "\"network\":\"%s\",\"address\":\"%s\"}",
        battery_ok ? "true" : "false", battery.millivolts, battery.percent,
        storage_ok ? "true" : "false", total, free_bytes, network, s_portal.info.address);
    return length < 0 || length >= (int)sizeof(body)
               ? send_public_error(request, (watchy_portal_error_response_t){500u, "internal_error"})
               : send_json(request, 200u, body);
}

static esp_err_t send_json_escaped_chunk(httpd_req_t *request, const char *text) {
    char piece[3];
    while (*text != '\0') {
        const unsigned char value = (unsigned char)*text++;
        if (value == '"' || value == '\\') {
            piece[0] = '\\';
            piece[1] = (char)value;
            piece[2] = '\0';
        } else if (value < 0x20u) {
            piece[0] = '?';
            piece[1] = '\0';
        } else {
            piece[0] = (char)value;
            piece[1] = '\0';
        }
        if (httpd_resp_send_chunk(request, piece, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

static esp_err_t send_installed(httpd_req_t *request, const char *package_ref) {
    httpd_resp_set_status(request, "201 Created");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (httpd_resp_send_chunk(request, "{\"reference\":\"", HTTPD_RESP_USE_STRLEN) != ESP_OK ||
        send_json_escaped_chunk(request, package_ref) != ESP_OK ||
        httpd_resp_send_chunk(request, "\"}", 2u) != ESP_OK) {
        return ESP_FAIL;
    }
    return httpd_resp_send_chunk(request, NULL, 0u);
}

static esp_err_t send_packages(httpd_req_t *request) {
    watchy_package_catalog_t *catalog = calloc(1u, sizeof(*catalog));
    watchy_package_status_t status;
    if (catalog == NULL) {
        return send_public_error(request, (watchy_portal_error_response_t){500u, "internal_error"});
    }
    status = watchy_packages_snapshot(catalog);
    if (status != WATCHY_PACKAGE_OK) {
        free(catalog);
        return send_public_error(request, watchy_portal_map_package_error(status));
    }
    httpd_resp_set_status(request, "200 OK");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (httpd_resp_send_chunk(request, "{\"packages\":[", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
        free(catalog);
        return ESP_FAIL;
    }
    for (size_t index = 0u; index < catalog->count; ++index) {
        const watchy_package_info_t *package = &catalog->packages[index];
        char fields[192];
        if (index != 0u && httpd_resp_send_chunk(request, ",", 1u) != ESP_OK) {
            free(catalog);
            return ESP_FAIL;
        }
        if (httpd_resp_send_chunk(request, "{\"reference\":\"", HTTPD_RESP_USE_STRLEN) != ESP_OK ||
            send_json_escaped_chunk(request, package->package_ref) != ESP_OK) {
            free(catalog);
            return ESP_FAIL;
        }
        const int length = snprintf(fields, sizeof(fields),
            "\",\"type\":\"%s\",\"active\":%s,\"pending\":%s,\"quarantined\":%s}",
            package->type == WATCHY_PACKAGE_TYPE_WATCHFACE ? "watchface" : "app",
            package->active ? "true" : "false", package->pending ? "true" : "false",
            package->quarantined ? "true" : "false");
        if (length < 0 || length >= (int)sizeof(fields) ||
            httpd_resp_send_chunk(request, fields, (ssize_t)length) != ESP_OK) {
            free(catalog);
            return ESP_FAIL;
        }
    }
    free(catalog);
    if (httpd_resp_send_chunk(request, "]}", 2u) != ESP_OK) {
        return ESP_FAIL;
    }
    return httpd_resp_send_chunk(request, NULL, 0u);
}

static esp_err_t receive_upload(httpd_req_t *request) {
    char content_type[48];
    char transfer_encoding[32];
    watchy_battery_state_t battery = {0};
    size_t total = 0u;
    size_t free_bytes = 0u;
    watchy_portal_upload_request_t policy = {
        .content_type = NULL,
        .content_length = request->content_len,
        .content_length_known = request->content_len > 0u,
        .chunked = read_header(request, "Transfer-Encoding", transfer_encoding,
                               sizeof(transfer_encoding)),
        .battery_mv = watchy_battery_read(&battery) == WATCHY_STATUS_OK
                          ? battery.millivolts : 0u,
        .free_bytes = watchy_storage_space(&total, &free_bytes) == WATCHY_STATUS_OK
                          ? free_bytes : 0u,
        .upload_in_progress = watchy_packages_upload_active(),
    };
    watchy_portal_policy_status_t policy_status;
    uint8_t chunk[WATCHY_PORTAL_RECEIVE_CHUNK];
    size_t remaining = request->content_len;
    char package_ref[WATCHY_PACKAGE_REF_MAX + 1u];
    watchy_package_status_t package_status;
    if (read_header(request, "Content-Type", content_type, sizeof(content_type))) {
        policy.content_type = content_type;
    }
    policy_status = watchy_portal_check_upload(&policy);
    if (policy_status != WATCHY_PORTAL_OK) {
        return send_public_error(request, watchy_portal_error_from_policy(policy_status));
    }
    package_status = watchy_packages_upload_begin(remaining);
    if (package_status != WATCHY_PACKAGE_OK) {
        return send_public_error(request, watchy_portal_map_package_error(package_status));
    }
    while (remaining != 0u) {
        const size_t wanted = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        const int received = httpd_req_recv(request, (char *)chunk, wanted);
        if (received <= 0 ||
            watchy_packages_upload_write(chunk, (size_t)received) != WATCHY_PACKAGE_OK) {
            watchy_packages_upload_abort();
            return send_public_error(request,
                (watchy_portal_error_response_t){400u, "upload_incomplete"});
        }
        remaining -= (size_t)received;
        mark_activity();
    }
    package_status = watchy_packages_upload_finish(package_ref);
    if (package_status != WATCHY_PACKAGE_OK) {
        return send_public_error(request, watchy_portal_map_package_error(package_status));
    }
    return send_installed(request, package_ref);
}

static esp_err_t mutate_package(httpd_req_t *request,
                                const watchy_portal_route_t *route) {
    char package_ref[WATCHY_PACKAGE_REF_MAX + 1u];
    const int length = snprintf(package_ref, sizeof(package_ref), "%s@%s",
                                route->identifier, route->version);
    watchy_package_status_t status;
    if (length < 0 || length >= (int)sizeof(package_ref)) {
        return send_public_error(request,
            (watchy_portal_error_response_t){400u, "invalid_request"});
    }
    status = route->action == WATCHY_PORTAL_ROUTE_ACTIVATE
                 ? watchy_packages_select_watchface(package_ref)
                 : watchy_packages_remove(package_ref);
    return status == WATCHY_PACKAGE_OK
               ? send_json(request, 200u, "{\"ok\":true}")
               : send_public_error(request, watchy_portal_map_package_error(status));
}

static esp_err_t request_handler(httpd_req_t *request) {
    watchy_portal_method_t method;
    watchy_portal_route_t route;
    mark_activity();
    if (request->method == HTTP_GET) method = WATCHY_PORTAL_METHOD_GET;
    else if (request->method == HTTP_POST) method = WATCHY_PORTAL_METHOD_POST;
    else if (request->method == HTTP_DELETE) method = WATCHY_PORTAL_METHOD_DELETE;
    else return send_public_error(request, watchy_portal_error_from_policy(
                                      WATCHY_PORTAL_ERR_INVALID_ROUTE));
    if (!watchy_portal_parse_route(method, request->uri, &route)) {
        return send_public_error(request, watchy_portal_error_from_policy(
                                      WATCHY_PORTAL_ERR_INVALID_ROUTE));
    }
    if ((route.action == WATCHY_PORTAL_ROUTE_UPLOAD ||
         route.action == WATCHY_PORTAL_ROUTE_ACTIVATE ||
         route.action == WATCHY_PORTAL_ROUTE_REMOVE) && !mutation_authorized(request)) {
        return send_public_error(request, watchy_portal_error_from_policy(
                                      WATCHY_PORTAL_ERR_UNAUTHORIZED));
    }
    switch (route.action) {
    case WATCHY_PORTAL_ROUTE_PAGE: return send_page(request);
    case WATCHY_PORTAL_ROUTE_STATUS: return send_status(request);
    case WATCHY_PORTAL_ROUTE_PACKAGES: return send_packages(request);
    case WATCHY_PORTAL_ROUTE_UPLOAD: return receive_upload(request);
    case WATCHY_PORTAL_ROUTE_ACTIVATE:
    case WATCHY_PORTAL_ROUTE_REMOVE: return mutate_package(request, &route);
    default: return send_public_error(request, watchy_portal_error_from_policy(
                                          WATCHY_PORTAL_ERR_INVALID_ROUTE));
    }
}

static void random_hex(char *out, size_t byte_count) {
    static const char digits[] = "0123456789abcdef";
    for (size_t index = 0u; index < byte_count; ++index) {
        const uint8_t value = (uint8_t)esp_random();
        out[index * 2u] = digits[value >> 4u];
        out[index * 2u + 1u] = digits[value & 15u];
    }
    out[byte_count * 2u] = '\0';
}

static void random_password(char out[65]) {
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    for (size_t index = 0u; index < 16u; ++index) {
        out[index] = alphabet[esp_random() % (sizeof(alphabet) - 1u)];
    }
    out[16] = '\0';
}

static watchy_status_t start_network(watchy_portal_network_mode_t mode,
                                     const watchy_settings_t *settings) {
    if (mode == WATCHY_PORTAL_NETWORK_AP) {
        uint8_t mac[6];
        watchy_wifi_ap_config_t config = {.channel = 1u};
        if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK) {
            return WATCHY_STATUS_INVALID_STATE;
        }
        snprintf(config.ssid, sizeof(config.ssid), "Watchy-%02X%02X%02X",
                 mac[3], mac[4], mac[5]);
        random_password(config.password);
        if (watchy_wifi_start_ap(&config) != WATCHY_STATUS_OK) {
            return WATCHY_STATUS_INVALID_STATE;
        }
        memcpy(s_portal.info.network_name, config.ssid, sizeof(s_portal.info.network_name));
        memcpy(s_portal.info.network_secret, config.password,
               sizeof(s_portal.info.network_secret));
        memcpy(s_portal.info.address, "192.168.4.1", sizeof("192.168.4.1"));
        return WATCHY_STATUS_OK;
    }
    if (settings->wifi_ssid[0] == '\0') {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    watchy_wifi_sta_config_t config = {0};
    memcpy(config.ssid, settings->wifi_ssid, sizeof(config.ssid));
    memcpy(config.password, settings->wifi_password, sizeof(config.password));
    if (watchy_wifi_start_sta(&config, false) != WATCHY_STATUS_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    const uint64_t deadline = now_ms() + WATCHY_PORTAL_CLIENT_TIMEOUT_MS;
    while (watchy_wifi_state() != WATCHY_WIFI_STA_CONNECTED &&
           (int64_t)(deadline - now_ms()) > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    esp_netif_t *interface = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (watchy_wifi_state() != WATCHY_WIFI_STA_CONNECTED || interface == NULL ||
        esp_netif_get_ip_info(interface, &ip) != ESP_OK ||
        esp_ip4addr_ntoa(&ip.ip, s_portal.info.address, sizeof(s_portal.info.address)) == NULL) {
        (void)watchy_wifi_stop();
        return WATCHY_STATUS_INVALID_STATE;
    }
    memcpy(s_portal.info.network_name, settings->wifi_ssid,
           sizeof(s_portal.info.network_name));
    s_portal.info.client_mode = true;
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_portal_start(watchy_portal_network_mode_t mode,
                                    const watchy_settings_t *settings,
                                    watchy_portal_session_info_t *out_info) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    const httpd_uri_t get = {.uri = "/*", .method = HTTP_GET,
                             .handler = request_handler, .user_ctx = NULL};
    const httpd_uri_t post = {.uri = "/*", .method = HTTP_POST,
                              .handler = request_handler, .user_ctx = NULL};
    const httpd_uri_t remove = {.uri = "/*", .method = HTTP_DELETE,
                                .handler = request_handler, .user_ctx = NULL};
    if (settings == NULL || out_info == NULL || watchy_portal_active()) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    memset(&s_portal, 0, sizeof(s_portal));
    random_hex(s_portal.info.token, WATCHY_PORTAL_TOKEN_HEX_SIZE / 2u);
    if (start_network(mode, settings) != WATCHY_STATUS_OK) {
        memset(&s_portal, 0, sizeof(s_portal));
        return WATCHY_STATUS_INVALID_STATE;
    }
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 4u;
    config.stack_size = 8192u;
    config.recv_wait_timeout = 10u;
    config.send_wait_timeout = 10u;
    config.lru_purge_enable = true;
    if (httpd_start(&s_portal.server, &config) != ESP_OK ||
        httpd_register_uri_handler(s_portal.server, &get) != ESP_OK ||
        httpd_register_uri_handler(s_portal.server, &post) != ESP_OK ||
        httpd_register_uri_handler(s_portal.server, &remove) != ESP_OK) {
        watchy_portal_stop();
        return WATCHY_STATUS_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_portal_mux);
    s_portal.running = true;
    portEXIT_CRITICAL(&s_portal_mux);
    mark_activity();
    *out_info = s_portal.info;
    return WATCHY_STATUS_OK;
}

bool watchy_portal_active(void) {
    bool running;
    portENTER_CRITICAL(&s_portal_mux);
    running = s_portal.running;
    portEXIT_CRITICAL(&s_portal_mux);
    return running;
}

bool watchy_portal_timed_out(void) {
    bool running;
    uint64_t last_activity_ms;
    portENTER_CRITICAL(&s_portal_mux);
    running = s_portal.running;
    last_activity_ms = s_portal.last_activity_ms;
    portEXIT_CRITICAL(&s_portal_mux);
    return running && watchy_portal_idle_expired(last_activity_ms, now_ms());
}

watchy_status_t watchy_portal_stop(void) {
    watchy_status_t status = WATCHY_STATUS_OK;
    httpd_handle_t server;
    portENTER_CRITICAL(&s_portal_mux);
    server = s_portal.server;
    s_portal.server = NULL;
    s_portal.running = false;
    portEXIT_CRITICAL(&s_portal_mux);
    if (server != NULL && httpd_stop(server) != ESP_OK) {
        status = WATCHY_STATUS_INVALID_STATE;
    }
    watchy_packages_upload_abort();
    if (watchy_radios_stop_all() != WATCHY_STATUS_OK) {
        status = WATCHY_STATUS_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_portal_mux);
    memset(&s_portal, 0, sizeof(s_portal));
    portEXIT_CRITICAL(&s_portal_mux);
    return status;
}
