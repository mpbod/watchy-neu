#ifndef WATCHY_STORAGE_H
#define WATCHY_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "watchy/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHY_STORAGE_BASE_PATH "/data"

watchy_status_t watchy_storage_init(void);
bool watchy_storage_ready(void);
watchy_status_t watchy_storage_atomic_rename(const char *source, const char *destination);
watchy_status_t watchy_storage_space(size_t *out_total_bytes, size_t *out_free_bytes);
void watchy_storage_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
