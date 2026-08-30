#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "watchy/sdk.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

_Static_assert(_Generic(((watchy_host_caps_v1_t){0}).size, uint32_t: 1, default: 0),
               "watchy_host_caps_v1_t.size must be uint32_t");

typedef struct {
    uint32_t last_read_buffer_size;
    uint32_t last_written_size;
} test_storage_context_t;

static watchy_status_t test_storage_read(void *context,
                                         const char *path,
                                         void *buffer,
                                         uint32_t buffer_size,
                                         uint32_t *out_size) {
    static const char payload[] = "core";
    test_storage_context_t *storage = (test_storage_context_t *)context;

    (void)path;
    storage->last_read_buffer_size = buffer_size;
    memcpy(buffer, payload, sizeof(payload));
    *out_size = (uint32_t)sizeof(payload);
    return WATCHY_STATUS_OK;
}

static watchy_status_t test_storage_write(void *context,
                                          const char *path,
                                          const void *data,
                                          uint32_t data_size) {
    const char expected[] = "abi";
    test_storage_context_t *storage = (test_storage_context_t *)context;

    (void)path;
    CHECK(data_size == (uint32_t)sizeof(expected));
    CHECK(memcmp(data, expected, sizeof(expected)) == 0);
    storage->last_written_size = data_size;
    return WATCHY_STATUS_OK;
}

int main(void) {
    test_storage_context_t context = {0};
    watchy_storage_api_v1_t storage = {
        .context = &context,
        .read = test_storage_read,
        .write = test_storage_write,
    };
    watchy_host_caps_v1_t host = {
        .abi = {WATCHY_ABI_V1_MAJOR, WATCHY_ABI_V1_MINOR},
        .size = (uint32_t)sizeof(watchy_host_caps_v1_t),
        .storage = &storage,
    };
    char buffer[8] = {0};
    uint32_t out_size = 0;
    static const char payload[] = "abi";

    CHECK(host.size == (uint32_t)sizeof(watchy_host_caps_v1_t));
    CHECK(host.storage->read(host.storage->context, "/pkg/data", buffer, (uint32_t)sizeof(buffer), &out_size) ==
          WATCHY_STATUS_OK);
    CHECK(context.last_read_buffer_size == (uint32_t)sizeof(buffer));
    CHECK(out_size == 5u);
    CHECK(memcmp(buffer, "core", out_size) == 0);
    CHECK(host.storage->write(host.storage->context, "/pkg/data", payload, (uint32_t)sizeof(payload)) ==
          WATCHY_STATUS_OK);
    CHECK(context.last_written_size == (uint32_t)sizeof(payload));
    puts("PASS test_sdk_c");
    return 0;
}
