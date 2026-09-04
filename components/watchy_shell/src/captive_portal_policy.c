#include "watchy/captive_portal.h"

#include <string.h>

#define DNS_HEADER_SIZE 12u
#define DNS_ANSWER_SIZE 16u

bool watchy_captive_dns_build_reply(const uint8_t *query, size_t query_size,
                                    const uint8_t address[4],
                                    uint8_t *reply, size_t reply_capacity,
                                    size_t *out_reply_size) {
    size_t cursor = DNS_HEADER_SIZE;
    size_t reply_size;

    if (out_reply_size != NULL) {
        *out_reply_size = 0u;
    }
    if (query == NULL || address == NULL || reply == NULL ||
        out_reply_size == NULL || query_size < DNS_HEADER_SIZE ||
        (query[2] & 0xf8u) != 0u || query[4] != 0u || query[5] != 1u) {
        return false;
    }

    for (;;) {
        uint8_t label_size;
        if (cursor >= query_size) {
            return false;
        }
        label_size = query[cursor++];
        if ((label_size & 0xc0u) != 0u || label_size > 63u) {
            return false;
        }
        if (label_size == 0u) {
            break;
        }
        if ((size_t)label_size > query_size - cursor) {
            return false;
        }
        cursor += label_size;
    }
    if (query_size - cursor < 4u || query[cursor] != 0u ||
        query[cursor + 1u] != 1u || query[cursor + 2u] != 0u ||
        query[cursor + 3u] != 1u) {
        return false;
    }
    cursor += 4u;
    if (cursor > SIZE_MAX - DNS_ANSWER_SIZE) {
        return false;
    }
    reply_size = cursor + DNS_ANSWER_SIZE;
    if (reply_capacity < reply_size) {
        return false;
    }

    memmove(reply, query, cursor);
    reply[2] = (uint8_t)(0x84u | (query[2] & 0x01u));
    reply[3] = 0u;
    reply[4] = 0u;
    reply[5] = 1u;
    reply[6] = 0u;
    reply[7] = 1u;
    reply[8] = 0u;
    reply[9] = 0u;
    reply[10] = 0u;
    reply[11] = 0u;
    const uint8_t answer[DNS_ANSWER_SIZE] = {
        0xc0, 0x0c, 0x00, 0x01, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x04,
        address[0], address[1], address[2], address[3],
    };
    memcpy(reply + cursor, answer, sizeof(answer));
    *out_reply_size = reply_size;
    return true;
}

bool watchy_portal_captive_redirect_path(const char *path) {
    if (path == NULL ||
        (path[0] == '/' && (path[1] == '\0' || path[1] == '?'))) {
        return false;
    }
    if (strncmp(path, "/api/v1", 7u) == 0 &&
        (path[7] == '\0' || path[7] == '/' || path[7] == '?')) {
        return false;
    }
    return true;
}
