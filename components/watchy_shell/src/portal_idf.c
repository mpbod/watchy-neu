#include "watchy/portal.h"

#include "watchy/battery.h"
#include "watchy/package_runtime.h"
#include "watchy/radios.h"
#include "watchy/rtc.h"
#include "watchy/storage.h"
#include "watchy/time_sync.h"
#include "watchy/timezone_action.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "bootloader_random.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

#define WATCHY_PORTAL_RECEIVE_CHUNK 1024u
#define WATCHY_PORTAL_CLIENT_TIMEOUT_MS 15000u

typedef struct {
    char *body;
    size_t body_size;
    cJSON *root;
} portal_json_request_t;

typedef struct {
    httpd_handle_t server;
    watchy_portal_session_info_t info;
    uint64_t started_ms;
    uint64_t last_activity_ms;
    bool running;
} portal_state_t;

static portal_state_t s_portal;
static uint8_t s_ap_entropy_seed[32];
static uint32_t s_ap_session_counter;
static bool s_ap_entropy_ready;
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
    "<div id=result></div></section><section><h2>Wi-Fi provisioning</h2>"
    "<input id=ssid maxlength=32 placeholder=SSID><input id=wifiPassword maxlength=64 "
    "type=password placeholder='Password (blank for open)'><button id=saveWifi>Save Wi-Fi</button>"
    "</section><section><h2>Packages</h2><div id=packages>Loading...</div></section><script>";

static const char PAGE_SCRIPT[] =
    "const statusEl=document.getElementById('status'),packagesEl=document.getElementById('packages'),"
    "resultEl=document.getElementById('result'),fileEl=document.getElementById('file'),"
    "uploadEl=document.getElementById('upload'),ssidEl=document.getElementById('ssid'),"
    "wifiPasswordEl=document.getElementById('wifiPassword'),saveWifiEl=document.getElementById('saveWifi');"
    "async function call(path,options={}){options.headers=options.headers||{};"
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
    "refresh()}catch(e){resultEl.textContent=e.message}};saveWifiEl.onclick=async()=>{try{await call('/api/v1/wifi',"
    "{method:'POST',headers:{'X-Watchy-SSID':ssidEl.value,'X-Watchy-WiFi-Password':wifiPasswordEl.value}});"
    "wifiPasswordEl.value='';resultEl.textContent='Wi-Fi saved'}catch(e){resultEl.textContent=e.message}};"
    "refresh()</script></body></html>";

static uint64_t now_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static void mark_authenticated_activity(void) {
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
    if (error.http_status == 401u) {
        httpd_resp_set_hdr(request, "WWW-Authenticate",
                           "Basic realm=\"Watchy\", charset=\"UTF-8\"");
    }
    return send_json(request, error.http_status, body);
}

static esp_err_t public_code(httpd_req_t *request,
                             uint16_t status,
                             const char *code) {
    return send_public_error(request,
                             (watchy_portal_error_response_t){status, code});
}

static esp_err_t storage_error(httpd_req_t *request) {
    return public_code(request, 507u, "storage_error");
}

static esp_err_t send_json_tree(httpd_req_t *request,
                                uint16_t status,
                                cJSON *root) {
    char *serialized;
    esp_err_t result;
    if (root == NULL) {
        return public_code(request, 500u, "internal_error");
    }
    serialized = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (serialized == NULL) {
        return public_code(request, 500u, "internal_error");
    }
    result = send_json(request, status, serialized);
    memset(serialized, 0, strlen(serialized));
    cJSON_free(serialized);
    return result;
}

static bool read_header(httpd_req_t *request,
                        const char *name,
                        char *value,
                        size_t capacity) {
    const size_t length = httpd_req_get_hdr_value_len(request, name);
    return length > 0u && length < capacity &&
           httpd_req_get_hdr_value_str(request, name, value, capacity) == ESP_OK;
}

static bool request_authorized(httpd_req_t *request) {
    char authorization[96];
    return read_header(request, "Authorization", authorization, sizeof(authorization)) &&
           watchy_portal_basic_authorized(s_portal.info.token, authorization);
}

static esp_err_t send_page(httpd_req_t *request) {
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (httpd_resp_send_chunk(request, PAGE_HEAD, HTTPD_RESP_USE_STRLEN) != ESP_OK ||
        httpd_resp_send_chunk(request, PAGE_SCRIPT, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
        return ESP_FAIL;
    }
    return httpd_resp_send_chunk(request, NULL, 0u);
}

static esp_err_t send_status(httpd_req_t *request) {
    watchy_battery_state_t battery = {0};
    size_t total = 0u;
    size_t free_bytes = 0u;
    const bool battery_ok = watchy_battery_read(&battery) == WATCHY_STATUS_OK;
    const bool storage_ok = watchy_storage_space(&total, &free_bytes) == WATCHY_STATUS_OK;
    const char *network = s_portal.info.client_mode ? "client" : "access_point";
    const esp_app_desc_t *app = esp_app_get_description();
    cJSON *root = cJSON_CreateObject();
    cJSON *battery_json;
    cJSON *storage_json;
    cJSON *firmware_json;
    if (root == NULL ||
        (battery_json = cJSON_AddObjectToObject(root, "battery")) == NULL ||
        cJSON_AddBoolToObject(battery_json, "available", battery_ok) == NULL ||
        cJSON_AddNumberToObject(battery_json, "mv", battery.millivolts) == NULL ||
        cJSON_AddNumberToObject(battery_json, "percent", battery.percent) == NULL ||
        (storage_json = cJSON_AddObjectToObject(root, "storage")) == NULL ||
        cJSON_AddBoolToObject(storage_json, "available", storage_ok) == NULL ||
        cJSON_AddNumberToObject(storage_json, "total", (double)total) == NULL ||
        cJSON_AddNumberToObject(storage_json, "free", (double)free_bytes) == NULL ||
        cJSON_AddStringToObject(root, "network", network) == NULL ||
        cJSON_AddStringToObject(root, "address", s_portal.info.address) == NULL ||
        (firmware_json = cJSON_AddObjectToObject(root, "firmware")) == NULL ||
        cJSON_AddStringToObject(firmware_json, "project", app->project_name) == NULL ||
        cJSON_AddStringToObject(firmware_json, "version", app->version) == NULL ||
        cJSON_AddStringToObject(firmware_json, "buildDate", app->date) == NULL ||
        cJSON_AddStringToObject(firmware_json, "buildTime", app->time) == NULL ||
        cJSON_AddBoolToObject(root, "onWatchDiagnostics", true) == NULL) {
        cJSON_Delete(root);
        return public_code(request, 500u, "internal_error");
    }
    return send_json_tree(request, 200u, root);
}

static const char *transition_level_name(watchy_transition_level_t level) {
    switch (level) {
    case WATCHY_TRANSITION_LEVEL_FULL: return "full";
    case WATCHY_TRANSITION_LEVEL_REDUCED: return "reduced";
    case WATCHY_TRANSITION_LEVEL_OFF: return "off";
    }
    return NULL;
}

static cJSON *add_time_object(cJSON *root,
                              const char *name,
                              const watchy_time_t *time) {
    cJSON *object = cJSON_AddObjectToObject(root, name);
    if (object == NULL ||
        cJSON_AddNumberToObject(object, "year", time->year) == NULL ||
        cJSON_AddNumberToObject(object, "month", time->month) == NULL ||
        cJSON_AddNumberToObject(object, "day", time->day) == NULL ||
        cJSON_AddNumberToObject(object, "hour", time->hour) == NULL ||
        cJSON_AddNumberToObject(object, "minute", time->minute) == NULL ||
        cJSON_AddNumberToObject(object, "second", time->second) == NULL ||
        cJSON_AddNumberToObject(object, "weekday", time->weekday) == NULL ||
        cJSON_AddNumberToObject(object, "utcOffsetMinutes",
                               time->utc_offset_minutes) == NULL) {
        return NULL;
    }
    return object;
}

static esp_err_t send_settings_json(httpd_req_t *request,
                                    const watchy_portal_settings_response_t *response) {
    const watchy_settings_t *settings = response->settings;
    cJSON *root = cJSON_CreateObject();
    cJSON *wifi;
    int16_t offset;
    char timezone_label[16];
    const char *transition = transition_level_name(settings->transition_level);
    if (root == NULL || transition == NULL ||
        !watchy_settings_format_timezone(settings, timezone_label,
                                         sizeof(timezone_label)) ||
        cJSON_AddBoolToObject(root, "time24h", settings->time_24h) == NULL ||
        cJSON_AddBoolToObject(root, "motionWake", settings->motion_wake) == NULL ||
        cJSON_AddStringToObject(root, "transitionLevel", transition) == NULL ||
        cJSON_AddNumberToObject(root, "partialRefreshLimit",
                               settings->partial_refresh_limit) == NULL ||
        cJSON_AddStringToObject(root, "timezone", settings->timezone) == NULL ||
        cJSON_AddStringToObject(root, "timezoneLabel", timezone_label) == NULL ||
        cJSON_AddStringToObject(root, "ntpServer", settings->ntp_server) == NULL ||
        cJSON_AddStringToObject(root, "activeWatchface",
                               settings->active_watchface) == NULL ||
        (wifi = cJSON_AddObjectToObject(root, "wifi")) == NULL ||
        cJSON_AddStringToObject(wifi, "ssid", settings->wifi_ssid) == NULL ||
        cJSON_AddBoolToObject(wifi, "configured",
                             settings->wifi_ssid[0] != '\0') == NULL) {
        cJSON_Delete(root);
        return public_code(request, 500u, "internal_error");
    }
    if (watchy_settings_timezone_offset(settings, &offset)) {
        if (cJSON_AddNumberToObject(root, "timezoneOffsetMinutes", offset) == NULL) {
            cJSON_Delete(root);
            return public_code(request, 500u, "internal_error");
        }
    } else if (cJSON_AddNullToObject(root, "timezoneOffsetMinutes") == NULL) {
        cJSON_Delete(root);
        return public_code(request, 500u, "internal_error");
    }
    if (response->has_local_time &&
        add_time_object(root, "time", &response->local_time) == NULL) {
        cJSON_Delete(root);
        return public_code(request, 500u, "internal_error");
    }
    return send_json_tree(request, 200u, root);
}

static esp_err_t send_wifi_json(httpd_req_t *request,
                                const watchy_settings_t *settings) {
    cJSON *root = cJSON_CreateObject();
    cJSON *wifi;
    if (root == NULL || (wifi = cJSON_AddObjectToObject(root, "wifi")) == NULL ||
        cJSON_AddStringToObject(wifi, "ssid", settings->wifi_ssid) == NULL ||
        cJSON_AddBoolToObject(wifi, "configured",
                             settings->wifi_ssid[0] != '\0') == NULL) {
        cJSON_Delete(root);
        return public_code(request, 500u, "internal_error");
    }
    return send_json_tree(request, 200u, root);
}

static esp_err_t send_time_json(httpd_req_t *request,
                                const watchy_time_t *local_time) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL || add_time_object(root, "time", local_time) == NULL) {
        cJSON_Delete(root);
        return public_code(request, 500u, "internal_error");
    }
    return send_json_tree(request, 200u, root);
}

static void clear_json_request(portal_json_request_t *document) {
    if (document == NULL) return;
    cJSON_Delete(document->root);
    document->root = NULL;
    if (document->body != NULL) {
        memset(document->body, 0, document->body_size);
        free(document->body);
        document->body = NULL;
    }
    document->body_size = 0u;
}

static bool json_member_names_unique(const cJSON *root) {
    for (const cJSON *member = root->child; member != NULL; member = member->next) {
        if (member->string == NULL) return false;
        for (const cJSON *prior = root->child; prior != member; prior = prior->next) {
            if (strcmp(prior->string, member->string) == 0) return false;
        }
    }
    return true;
}

static watchy_portal_error_response_t read_json_object(
    httpd_req_t *request,
    portal_json_request_t *out_document) {
    char content_type[32];
    const char *parse_end = NULL;
    size_t received_total = 0u;
    watchy_portal_json_envelope_t envelope = {
        .content_type = NULL,
        .content_length = request->content_len,
        .read_complete = true,
        .parsed = true,
        .object_root = true,
        .unique_members = true,
    };
    watchy_portal_error_response_t error;

    memset(out_document, 0, sizeof(*out_document));
    if (read_header(request, "Content-Type", content_type,
                    sizeof(content_type))) {
        envelope.content_type = content_type;
    }
    error = watchy_portal_json_envelope_error(&envelope);
    if (error.http_status != 0u) return error;
    out_document->body_size = request->content_len + 1u;
    out_document->body = calloc(out_document->body_size, 1u);
    if (out_document->body == NULL) {
        return (watchy_portal_error_response_t){500u, "internal_error"};
    }
    while (received_total < request->content_len) {
        const int received = httpd_req_recv(
            request, out_document->body + received_total,
            request->content_len - received_total);
        if (received <= 0) {
            envelope.read_complete = false;
            return watchy_portal_json_envelope_error(&envelope);
        }
        received_total += (size_t)received;
    }
    if (watchy_portal_json_has_escaped_nul(out_document->body,
                                           request->content_len)) {
        envelope.parsed = false;
        return watchy_portal_json_envelope_error(&envelope);
    }
    out_document->root = cJSON_ParseWithLengthOpts(
        out_document->body, out_document->body_size, &parse_end, true);
    envelope.parsed = out_document->root != NULL &&
                      parse_end == out_document->body + request->content_len;
    envelope.object_root = cJSON_IsObject(out_document->root);
    envelope.unique_members = envelope.object_root &&
                              json_member_names_unique(out_document->root);
    return watchy_portal_json_envelope_error(&envelope);
}

static bool json_integer(const cJSON *item,
                         int minimum,
                         int maximum,
                         int *out_value) {
    int value;
    if (!cJSON_IsNumber(item) || item->valuedouble < (double)minimum ||
        item->valuedouble > (double)maximum) {
        return false;
    }
    value = (int)item->valuedouble;
    if ((double)value != item->valuedouble) return false;
    *out_value = value;
    return true;
}

static bool parse_settings_patch(const cJSON *root,
                                 watchy_portal_settings_patch_t *out_patch) {
    memset(out_patch, 0, sizeof(*out_patch));
    for (const cJSON *member = root->child; member != NULL; member = member->next) {
        if (strcmp(member->string, "time24h") == 0) {
            if (!cJSON_IsBool(member)) return false;
            out_patch->has_time_24h = true;
            out_patch->time_24h = cJSON_IsTrue(member);
        } else if (strcmp(member->string, "motionWake") == 0) {
            if (!cJSON_IsBool(member)) return false;
            out_patch->has_motion_wake = true;
            out_patch->motion_wake = cJSON_IsTrue(member);
        } else if (strcmp(member->string, "transitionLevel") == 0) {
            if (!cJSON_IsString(member)) return false;
            out_patch->has_transition_level = true;
            if (strcmp(member->valuestring, "full") == 0) {
                out_patch->transition_level = WATCHY_TRANSITION_LEVEL_FULL;
            } else if (strcmp(member->valuestring, "reduced") == 0) {
                out_patch->transition_level = WATCHY_TRANSITION_LEVEL_REDUCED;
            } else if (strcmp(member->valuestring, "off") == 0) {
                out_patch->transition_level = WATCHY_TRANSITION_LEVEL_OFF;
            } else {
                return false;
            }
        } else if (strcmp(member->string, "partialRefreshLimit") == 0) {
            int value;
            if (!json_integer(member, 1, WATCHY_SETTINGS_PARTIAL_LIMIT_MAX,
                              &value)) {
                return false;
            }
            out_patch->has_partial_refresh_limit = true;
            out_patch->partial_refresh_limit = (uint16_t)value;
        } else if (strcmp(member->string, "timezoneOffsetMinutes") == 0) {
            int value;
            if (!json_integer(member, INT16_MIN, INT16_MAX, &value)) return false;
            out_patch->has_timezone_offset = true;
            out_patch->timezone_offset_minutes = (int16_t)value;
        } else if (strcmp(member->string, "ntpServer") == 0) {
            if (!cJSON_IsString(member) ||
                strnlen(member->valuestring, WATCHY_SETTINGS_NTP_SERVER_MAX + 1u) >
                    WATCHY_SETTINGS_NTP_SERVER_MAX) {
                return false;
            }
            out_patch->has_ntp_server = true;
            out_patch->ntp_server = member->valuestring;
        } else {
            return false;
        }
    }
    return true;
}

static bool parse_wifi_patch(const cJSON *root,
                             watchy_portal_wifi_patch_t *out_patch,
                             char ssid[WATCHY_SETTINGS_WIFI_SSID_MAX + 1u],
                             char password[WATCHY_SETTINGS_WIFI_PASSWORD_MAX + 1u],
                             cJSON **out_password_member) {
    const cJSON *ssid_member = NULL;
    const cJSON *password_member = NULL;
    memset(out_patch, 0, sizeof(*out_patch));
    memset(ssid, 0, WATCHY_SETTINGS_WIFI_SSID_MAX + 1u);
    memset(password, 0, WATCHY_SETTINGS_WIFI_PASSWORD_MAX + 1u);
    *out_password_member = NULL;
    for (const cJSON *member = root->child; member != NULL; member = member->next) {
        if (strcmp(member->string, "ssid") == 0) ssid_member = member;
        else if (strcmp(member->string, "password") == 0) password_member = member;
        else return false;
    }
    if (!cJSON_IsString(ssid_member)) return false;
    const size_t ssid_length = strnlen(
        ssid_member->valuestring, WATCHY_SETTINGS_WIFI_SSID_MAX + 1u);
    if (ssid_length > WATCHY_SETTINGS_WIFI_SSID_MAX) return false;
    memcpy(ssid, ssid_member->valuestring, ssid_length);
    if (password_member != NULL) {
        if (!cJSON_IsString(password_member)) return false;
        const size_t password_length = strnlen(
            password_member->valuestring, WATCHY_SETTINGS_WIFI_PASSWORD_MAX + 1u);
        if (password_length > WATCHY_SETTINGS_WIFI_PASSWORD_MAX) return false;
        memcpy(password, password_member->valuestring, password_length);
        out_patch->password_present = true;
        out_patch->password = password;
        *out_password_member = (cJSON *)password_member;
    }
    out_patch->ssid = ssid;
    return true;
}

static bool parse_time_fields(const cJSON *root,
                              int *year,
                              int *month,
                              int *day,
                              int *hour,
                              int *minute) {
    unsigned found = 0u;
    for (const cJSON *member = root->child; member != NULL; member = member->next) {
        if (strcmp(member->string, "year") == 0) {
            if (!json_integer(member, 2000, 2099, year)) return false;
            found |= 1u;
        } else if (strcmp(member->string, "month") == 0) {
            if (!json_integer(member, 1, 12, month)) return false;
            found |= 2u;
        } else if (strcmp(member->string, "day") == 0) {
            if (!json_integer(member, 1, 31, day)) return false;
            found |= 4u;
        } else if (strcmp(member->string, "hour") == 0) {
            if (!json_integer(member, 0, 23, hour)) return false;
            found |= 8u;
        } else if (strcmp(member->string, "minute") == 0) {
            if (!json_integer(member, 0, 59, minute)) return false;
            found |= 16u;
        } else {
            return false;
        }
    }
    return found == 31u;
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
    const watchy_status_t storage_status = watchy_storage_space(&total, &free_bytes);
    watchy_portal_upload_request_t policy = {
        .content_type = NULL,
        .content_length = request->content_len,
        .content_length_known = request->content_len > 0u,
        .chunked = read_header(request, "Transfer-Encoding", transfer_encoding,
                               sizeof(transfer_encoding)),
        .battery_mv = watchy_battery_read(&battery) == WATCHY_STATUS_OK
                          ? battery.millivolts : 0u,
        .storage_available = storage_status == WATCHY_STATUS_OK,
        .free_bytes = free_bytes,
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
        if (received <= 0) {
            watchy_packages_upload_abort();
            return send_public_error(request, watchy_portal_map_upload_io_error(
                WATCHY_PORTAL_UPLOAD_IO_CLIENT, WATCHY_PACKAGE_OK));
        }
        if (watchy_portal_timed_out()) {
            watchy_packages_upload_abort();
            return send_public_error(request, watchy_portal_error_from_policy(
                                          WATCHY_PORTAL_ERR_UNAUTHORIZED));
        }
        package_status = watchy_packages_upload_write(chunk, (size_t)received);
        if (package_status != WATCHY_PACKAGE_OK) {
            watchy_packages_upload_abort();
            return send_public_error(request, watchy_portal_map_upload_io_error(
                WATCHY_PORTAL_UPLOAD_IO_PACKAGE, package_status));
        }
        remaining -= (size_t)received;
        mark_authenticated_activity();
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

static esp_err_t get_settings(httpd_req_t *request) {
    watchy_settings_t settings;
    watchy_time_t local_time;
    const watchy_time_t *time_response = NULL;
    watchy_portal_settings_response_t settings_response;
    esp_err_t response;
    memset(&settings, 0, sizeof(settings));
    memset(&local_time, 0, sizeof(local_time));
    memset(&settings_response, 0, sizeof(settings_response));
    if (watchy_settings_load(&settings) != WATCHY_STATUS_OK) {
        response = storage_error(request);
        goto cleanup;
    }
    if (watchy_rtc_read_local(&local_time) == WATCHY_STATUS_OK) {
        time_response = &local_time;
    }
    if (!watchy_portal_prepare_settings_response(&settings, time_response,
                                                 &settings_response)) {
        response = public_code(request, 500u, "internal_error");
        goto cleanup;
    }
    response = send_settings_json(request, &settings_response);

cleanup:
    memset(&settings, 0, sizeof(settings));
    memset(&local_time, 0, sizeof(local_time));
    memset(&settings_response, 0, sizeof(settings_response));
    return response;
}

static watchy_status_t portal_set_rtc_offset(void *context, int16_t minutes) {
    (void)context;
    return watchy_rtc_set_utc_offset(minutes);
}

static watchy_status_t portal_save_settings(
    void *context,
    const watchy_settings_t *settings) {
    (void)context;
    return watchy_settings_save(settings);
}

static watchy_status_t portal_read_local(void *context,
                                         watchy_time_t *out_time) {
    (void)context;
    return watchy_rtc_read_local(out_time);
}

static const watchy_timezone_action_ops_t portal_timezone_ops = {
    .set_rtc_offset = portal_set_rtc_offset,
    .save_settings = portal_save_settings,
    .read_local = portal_read_local,
    .context = NULL,
};

static esp_err_t update_settings(httpd_req_t *request) {
    portal_json_request_t document = {0};
    watchy_portal_settings_patch_t patch;
    watchy_settings_t current;
    watchy_settings_t candidate;
    watchy_time_t local_time;
    watchy_portal_settings_response_t settings_response;
    watchy_portal_error_response_t json_error;
    esp_err_t response;
    memset(&patch, 0, sizeof(patch));
    memset(&current, 0, sizeof(current));
    memset(&candidate, 0, sizeof(candidate));
    memset(&local_time, 0, sizeof(local_time));
    memset(&settings_response, 0, sizeof(settings_response));

    json_error = read_json_object(request, &document);
    if (json_error.http_status != 0u) {
        response = send_public_error(request, json_error);
        goto cleanup;
    }
    if (watchy_settings_load(&current) != WATCHY_STATUS_OK) {
        response = storage_error(request);
        goto cleanup;
    }
    if (!parse_settings_patch(document.root, &patch)) {
        response = public_code(request, 400u, "invalid_request");
        goto cleanup;
    }
    if (!watchy_portal_apply_settings_patch(&current, &patch, &candidate)) {
        response = public_code(request, 422u, "invalid_settings");
        goto cleanup;
    }
    if (watchy_rtc_read_local(&local_time) != WATCHY_STATUS_OK ||
        watchy_timezone_action_apply(&current, &candidate, &local_time,
                                     &portal_timezone_ops) != WATCHY_STATUS_OK) {
        response = storage_error(request);
        goto cleanup;
    }
    if (!watchy_portal_prepare_settings_response(&current, &local_time,
                                                 &settings_response)) {
        response = public_code(request, 500u, "internal_error");
        goto cleanup;
    }
    response = send_settings_json(request, &settings_response);

cleanup:
    clear_json_request(&document);
    memset(&patch, 0, sizeof(patch));
    memset(&current, 0, sizeof(current));
    memset(&candidate, 0, sizeof(candidate));
    memset(&local_time, 0, sizeof(local_time));
    memset(&settings_response, 0, sizeof(settings_response));
    return response;
}

static void wipe_wifi_password_members(cJSON *root) {
    if (root == NULL) return;
    for (cJSON *member = root->child; member != NULL; member = member->next) {
        if (member->string != NULL && strcmp(member->string, "password") == 0 &&
            cJSON_IsString(member) && member->valuestring != NULL) {
            memset(member->valuestring, 0, strlen(member->valuestring));
        }
        wipe_wifi_password_members(member);
    }
}

static esp_err_t update_wifi(httpd_req_t *request) {
    portal_json_request_t document = {0};
    watchy_portal_wifi_patch_t patch;
    watchy_settings_t current;
    watchy_settings_t candidate;
    cJSON *password_member = NULL;
    char ssid[WATCHY_SETTINGS_WIFI_SSID_MAX + 1u] = {0};
    char password[WATCHY_SETTINGS_WIFI_PASSWORD_MAX + 1u] = {0};
    watchy_portal_error_response_t json_error;
    esp_err_t response;
    memset(&patch, 0, sizeof(patch));
    memset(&current, 0, sizeof(current));
    memset(&candidate, 0, sizeof(candidate));

    if (s_portal.info.client_mode) {
        response = public_code(request, 409u, "conflict");
        goto cleanup;
    }
    json_error = read_json_object(request, &document);
    if (json_error.http_status != 0u) {
        response = send_public_error(request, json_error);
        goto cleanup;
    }
    if (!parse_wifi_patch(document.root, &patch, ssid, password,
                          &password_member)) {
        response = public_code(request, 400u, "invalid_request");
        goto cleanup;
    }
    if (watchy_settings_load(&current) != WATCHY_STATUS_OK) {
        response = storage_error(request);
        goto cleanup;
    }
    if (!watchy_portal_apply_wifi_patch(&current, &patch, &candidate)) {
        response = public_code(request, 422u, "invalid_wifi");
        goto cleanup;
    }
    if (watchy_settings_save(&candidate) != WATCHY_STATUS_OK) {
        response = storage_error(request);
        goto cleanup;
    }
    response = send_wifi_json(request, &candidate);

cleanup:
    if (password_member != NULL && password_member->valuestring != NULL) {
        memset(password_member->valuestring, 0,
               strlen(password_member->valuestring));
    }
    wipe_wifi_password_members(document.root);
    clear_json_request(&document);
    memset(password, 0, sizeof(password));
    memset(ssid, 0, sizeof(ssid));
    memset(&patch, 0, sizeof(patch));
    memset(&current, 0, sizeof(current));
    memset(&candidate, 0, sizeof(candidate));
    return response;
}

static esp_err_t set_time(httpd_req_t *request) {
    portal_json_request_t document = {0};
    watchy_settings_t settings;
    watchy_time_t local_time;
    watchy_portal_error_response_t json_error;
    int16_t offset;
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    esp_err_t response;
    memset(&settings, 0, sizeof(settings));
    memset(&local_time, 0, sizeof(local_time));

    json_error = read_json_object(request, &document);
    if (json_error.http_status != 0u) {
        response = send_public_error(request, json_error);
        goto cleanup;
    }
    if (!parse_time_fields(document.root, &year, &month, &day, &hour,
                           &minute)) {
        response = public_code(request, 400u, "invalid_request");
        goto cleanup;
    }
    if (watchy_settings_load(&settings) != WATCHY_STATUS_OK) {
        response = storage_error(request);
        goto cleanup;
    }
    if (!watchy_settings_timezone_offset(&settings, &offset) ||
        !watchy_portal_time_from_fields(year, month, day, hour, minute,
                                       offset, &local_time)) {
        response = public_code(request, 422u, "invalid_time");
        goto cleanup;
    }
    if (watchy_rtc_set_local(&local_time) != WATCHY_STATUS_OK) {
        response = public_code(request, 500u, "rtc_error");
        goto cleanup;
    }
    response = send_time_json(request, &local_time);

cleanup:
    clear_json_request(&document);
    memset(&settings, 0, sizeof(settings));
    memset(&local_time, 0, sizeof(local_time));
    return response;
}

static esp_err_t sync_ntp(httpd_req_t *request) {
    watchy_settings_t settings;
    watchy_time_t local_time;
    watchy_portal_settings_response_t settings_response;
    esp_err_t response;
    memset(&settings, 0, sizeof(settings));
    memset(&local_time, 0, sizeof(local_time));
    memset(&settings_response, 0, sizeof(settings_response));

    if (!s_portal.info.client_mode) {
        response = public_code(request, 409u, "client_mode_required");
        goto cleanup;
    }
    if (watchy_settings_load(&settings) != WATCHY_STATUS_OK) {
        response = storage_error(request);
        goto cleanup;
    }
    if (settings.wifi_ssid[0] == '\0') {
        response = public_code(request, 409u, "no_wifi");
        goto cleanup;
    }
    mark_authenticated_activity();
    const watchy_status_t sync_status = watchy_time_sync_connected(&settings);
    mark_authenticated_activity();
    if (sync_status != WATCHY_STATUS_OK) {
        response = public_code(request, 500u, "ntp_error");
        goto cleanup;
    }
    if (watchy_rtc_read_local(&local_time) != WATCHY_STATUS_OK) {
        response = public_code(request, 500u, "rtc_error");
        goto cleanup;
    }
    if (!watchy_portal_prepare_settings_response(&settings, &local_time,
                                                 &settings_response)) {
        response = public_code(request, 500u, "internal_error");
        goto cleanup;
    }
    response = send_settings_json(request, &settings_response);

cleanup:
    memset(&settings, 0, sizeof(settings));
    memset(&local_time, 0, sizeof(local_time));
    memset(&settings_response, 0, sizeof(settings_response));
    return response;
}

static esp_err_t provision_wifi_legacy(httpd_req_t *request) {
    watchy_settings_t settings;
    char ssid[WATCHY_SETTINGS_WIFI_SSID_MAX + 1u];
    char password[WATCHY_SETTINGS_WIFI_PASSWORD_MAX + 1u] = {0};
    const size_t password_length =
        httpd_req_get_hdr_value_len(request, "X-Watchy-WiFi-Password");
    if (s_portal.info.client_mode) {
        return send_public_error(request,
            (watchy_portal_error_response_t){409u, "conflict"});
    }
    if (!read_header(request, "X-Watchy-SSID", ssid, sizeof(ssid)) ||
        password_length >= sizeof(password) ||
        (password_length != 0u &&
         httpd_req_get_hdr_value_str(request, "X-Watchy-WiFi-Password", password,
                                     sizeof(password)) != ESP_OK)) {
        memset(password, 0, sizeof(password));
        return send_public_error(request,
            (watchy_portal_error_response_t){400u, "invalid_request"});
    }
    memset(&settings, 0, sizeof(settings));
    if (watchy_settings_load(&settings) != WATCHY_STATUS_OK) {
        memset(password, 0, sizeof(password));
        memset(&settings, 0, sizeof(settings));
        return send_public_error(request,
            (watchy_portal_error_response_t){507u, "storage_error"});
    }
    if (!watchy_settings_set_wifi(&settings, ssid, password)) {
        memset(password, 0, sizeof(password));
        memset(&settings, 0, sizeof(settings));
        return send_public_error(request,
            (watchy_portal_error_response_t){400u, "invalid_request"});
    }
    const watchy_status_t status = watchy_settings_save(&settings);
    const esp_err_t response = status == WATCHY_STATUS_OK
                                   ? send_wifi_json(request, &settings)
                                   : storage_error(request);
    memset(&settings, 0, sizeof(settings));
    memset(password, 0, sizeof(password));
    memset(ssid, 0, sizeof(ssid));
    return response;
}

static esp_err_t request_handler(httpd_req_t *request) {
    watchy_portal_method_t method;
    watchy_portal_route_t route;
    uint64_t started;
    uint64_t last;
    uint64_t accepted_last;
    const uint64_t now = now_ms();
    const bool authenticated = request_authorized(request);
    portENTER_CRITICAL(&s_portal_mux);
    started = s_portal.started_ms;
    last = s_portal.last_activity_ms;
    portEXIT_CRITICAL(&s_portal_mux);
    if (!watchy_portal_session_accept(started, last, now, authenticated, &accepted_last)) {
        return send_public_error(request, watchy_portal_error_from_policy(
                                      WATCHY_PORTAL_ERR_UNAUTHORIZED));
    }
    portENTER_CRITICAL(&s_portal_mux);
    s_portal.last_activity_ms = accepted_last;
    portEXIT_CRITICAL(&s_portal_mux);
    if (request->method == HTTP_GET) method = WATCHY_PORTAL_METHOD_GET;
    else if (request->method == HTTP_POST) method = WATCHY_PORTAL_METHOD_POST;
    else if (request->method == HTTP_PUT) method = WATCHY_PORTAL_METHOD_PUT;
    else if (request->method == HTTP_DELETE) method = WATCHY_PORTAL_METHOD_DELETE;
    else return send_public_error(request, watchy_portal_error_from_policy(
                                      WATCHY_PORTAL_ERR_INVALID_ROUTE));
    if (!watchy_portal_parse_route(method, request->uri, &route)) {
        return send_public_error(request, watchy_portal_error_from_policy(
                                      WATCHY_PORTAL_ERR_INVALID_ROUTE));
    }
    switch (route.action) {
    case WATCHY_PORTAL_ROUTE_PAGE: return send_page(request);
    case WATCHY_PORTAL_ROUTE_STATUS: return send_status(request);
    case WATCHY_PORTAL_ROUTE_GET_SETTINGS: return get_settings(request);
    case WATCHY_PORTAL_ROUTE_UPDATE_SETTINGS: return update_settings(request);
    case WATCHY_PORTAL_ROUTE_PACKAGES: return send_packages(request);
    case WATCHY_PORTAL_ROUTE_UPLOAD: return receive_upload(request);
    case WATCHY_PORTAL_ROUTE_PROVISION_WIFI:
        return request->method == HTTP_PUT ? update_wifi(request)
                                           : provision_wifi_legacy(request);
    case WATCHY_PORTAL_ROUTE_SET_TIME: return set_time(request);
    case WATCHY_PORTAL_ROUTE_SYNC_NTP: return sync_ntp(request);
    case WATCHY_PORTAL_ROUTE_ACTIVATE:
    case WATCHY_PORTAL_ROUTE_REMOVE: return mutate_package(request, &route);
    default: return send_public_error(request, watchy_portal_error_from_policy(
                                          WATCHY_PORTAL_ERR_INVALID_ROUTE));
    }
}

static void random_hex(char *out, size_t byte_count) {
    static const char digits[] = "0123456789abcdef";
    uint8_t bytes[WATCHY_PORTAL_TOKEN_HEX_SIZE / 2u];
    esp_fill_random(bytes, byte_count);
    for (size_t index = 0u; index < byte_count; ++index) {
        const uint8_t value = bytes[index];
        out[index * 2u] = digits[value >> 4u];
        out[index * 2u + 1u] = digits[value & 15u];
    }
    out[byte_count * 2u] = '\0';
    memset(bytes, 0, sizeof(bytes));
}

static bool entropy_enable(void *context) {
    (void)context;
    bootloader_random_enable();
    return true;
}

static bool entropy_fill(void *context, uint8_t *bytes, size_t size) {
    (void)context;
    if (bytes == NULL || size == 0u) return false;
    esp_fill_random(bytes, size);
    return true;
}

static void entropy_disable(void *context) {
    (void)context;
    bootloader_random_disable();
}

watchy_status_t watchy_portal_prepare_ap_password(void) {
    const watchy_portal_entropy_api_t entropy = {
        .enable = entropy_enable,
        .fill = entropy_fill,
        .disable = entropy_disable,
        .context = NULL,
    };
    if (s_ap_entropy_ready) return WATCHY_STATUS_OK;
    s_ap_entropy_ready = watchy_portal_fill_guaranteed_entropy(
        &entropy, s_ap_entropy_seed, sizeof(s_ap_entropy_seed));
    return s_ap_entropy_ready ? WATCHY_STATUS_OK : WATCHY_STATUS_INVALID_STATE;
}

static bool derive_ap_password(char *out_password, size_t out_size) {
    uint8_t material[sizeof(s_ap_entropy_seed) + sizeof(s_ap_session_counter)];
    uint8_t digest[32];
    bool mapped;
    if (!s_ap_entropy_ready || out_password == NULL ||
        out_size < WATCHY_PORTAL_AP_PASSWORD_SIZE + 1u ||
        s_ap_session_counter == UINT32_MAX) {
        return false;
    }
    memcpy(material, s_ap_entropy_seed, sizeof(s_ap_entropy_seed));
    const uint32_t counter = ++s_ap_session_counter;
    material[32] = (uint8_t)(counter >> 24u);
    material[33] = (uint8_t)(counter >> 16u);
    material[34] = (uint8_t)(counter >> 8u);
    material[35] = (uint8_t)counter;
    if (mbedtls_sha256(material, sizeof(material), digest, 0) != 0) {
        memset(material, 0, sizeof(material));
        return false;
    }
    mapped = watchy_portal_password_from_digest(digest, out_password, out_size);
    memset(material, 0, sizeof(material));
    memset(digest, 0, sizeof(digest));
    return mapped;
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
        if (!derive_ap_password(config.password, sizeof(config.password))) {
            return WATCHY_STATUS_INVALID_STATE;
        }
        if (watchy_wifi_start_ap(&config) != WATCHY_STATUS_OK) {
            memset(config.password, 0, sizeof(config.password));
            return WATCHY_STATUS_INVALID_STATE;
        }
        memcpy(s_portal.info.network_name, config.ssid, sizeof(s_portal.info.network_name));
        memcpy(s_portal.info.network_secret, config.password,
               sizeof(s_portal.info.network_secret));
        memset(config.password, 0, sizeof(config.password));
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
    const httpd_uri_t put = {.uri = "/*", .method = HTTP_PUT,
                             .handler = request_handler, .user_ctx = NULL};
    const httpd_uri_t remove = {.uri = "/*", .method = HTTP_DELETE,
                                .handler = request_handler, .user_ctx = NULL};
    if (settings == NULL || out_info == NULL || watchy_portal_active()) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    memset(&s_portal, 0, sizeof(s_portal));
    if (start_network(mode, settings) != WATCHY_STATUS_OK) {
        memset(&s_portal, 0, sizeof(s_portal));
        return WATCHY_STATUS_INVALID_STATE;
    }
    /* The Wi-Fi radio is now an initialized true-entropy source. Keep the
     * out-of-band session credential at 128 bits and generate it after startup. */
    random_hex(s_portal.info.token, WATCHY_PORTAL_TOKEN_HEX_SIZE / 2u);
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 4u;
    config.stack_size = 8192u;
    config.recv_wait_timeout = 10u;
    config.send_wait_timeout = 10u;
    config.lru_purge_enable = true;
    if (httpd_start(&s_portal.server, &config) != ESP_OK ||
        httpd_register_uri_handler(s_portal.server, &get) != ESP_OK ||
        httpd_register_uri_handler(s_portal.server, &post) != ESP_OK ||
        httpd_register_uri_handler(s_portal.server, &put) != ESP_OK ||
        httpd_register_uri_handler(s_portal.server, &remove) != ESP_OK) {
        watchy_portal_stop();
        return WATCHY_STATUS_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_portal_mux);
    s_portal.started_ms = now_ms();
    s_portal.last_activity_ms = s_portal.started_ms;
    s_portal.running = true;
    portEXIT_CRITICAL(&s_portal_mux);
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
    uint64_t started_ms;
    uint64_t last_activity_ms;
    portENTER_CRITICAL(&s_portal_mux);
    running = s_portal.running;
    started_ms = s_portal.started_ms;
    last_activity_ms = s_portal.last_activity_ms;
    portEXIT_CRITICAL(&s_portal_mux);
    const uint64_t now = now_ms();
    return running &&
           (watchy_portal_idle_expired(last_activity_ms, now) ||
            now - started_ms >= WATCHY_PORTAL_ABSOLUTE_TIMEOUT_MS);
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
