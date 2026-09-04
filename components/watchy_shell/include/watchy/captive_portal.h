#ifndef WATCHY_CAPTIVE_PORTAL_H
#define WATCHY_CAPTIVE_PORTAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "watchy/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

bool watchy_captive_dns_build_reply(const uint8_t *query, size_t query_size,
                                    const uint8_t address[4],
                                    uint8_t *reply, size_t reply_capacity,
                                    size_t *out_reply_size);
watchy_status_t watchy_captive_portal_start(const char *address);
watchy_status_t watchy_captive_portal_stop(void);
bool watchy_portal_captive_redirect_path(const char *path);

#ifdef __cplusplus
}
#endif

#endif
