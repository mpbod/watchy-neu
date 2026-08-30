#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

#include "watchy/sdk.hpp"

namespace {

struct StorageContext {
    std::uint32_t last_read_buffer_size = 0;
    std::uint32_t last_written_size = 0;
};

static_assert(std::is_same<decltype(watchy_host_caps_v1_t{}.size), std::uint32_t>::value,
              "watchy_host_caps_v1_t.size must be uint32_t");

watchy_status_t test_storage_read(void *context,
                                  const char *path,
                                  void *buffer,
                                  std::uint32_t buffer_size,
                                  std::uint32_t *out_size) {
    static const char payload[] = "cpp";
    StorageContext *storage = static_cast<StorageContext *>(context);

    (void)path;
    storage->last_read_buffer_size = buffer_size;
    std::memcpy(buffer, payload, sizeof(payload));
    *out_size = static_cast<std::uint32_t>(sizeof(payload));
    return WATCHY_STATUS_OK;
}

watchy_status_t test_storage_write(void *context,
                                   const char *path,
                                   const void *data,
                                   std::uint32_t data_size) {
    const char expected[] = "ok";
    StorageContext *storage = static_cast<StorageContext *>(context);

    (void)path;
    if (data_size != sizeof(expected) || std::memcmp(data, expected, sizeof(expected)) != 0) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    storage->last_written_size = data_size;
    return WATCHY_STATUS_OK;
}

}  // namespace

int main() {
    StorageContext context;
    watchy_storage_api_v1_t api = {
        &context,
        test_storage_read,
        test_storage_write,
    };
    watchy::Storage storage(&api);
    char buffer[8] = {};
    std::uint32_t out_size = 0;
    const char payload[] = "ok";

    if (storage.read("/pkg/data", buffer, static_cast<std::uint32_t>(sizeof(buffer)), &out_size) != WATCHY_STATUS_OK) {
        return 1;
    }
    if (context.last_read_buffer_size != sizeof(buffer) || out_size != 4u || std::memcmp(buffer, "cpp", out_size) != 0) {
        return 1;
    }
    if (storage.write("/pkg/data", payload, static_cast<std::uint32_t>(sizeof(payload))) != WATCHY_STATUS_OK) {
        return 1;
    }
    if (context.last_written_size != sizeof(payload)) {
        return 1;
    }

    watchy::Storage missing;
    if (missing.read("/pkg/data", buffer, static_cast<std::uint32_t>(sizeof(buffer)), &out_size) !=
        WATCHY_STATUS_UNSUPPORTED) {
        return 1;
    }

    std::puts("PASS test_sdk_cpp");
    return 0;
}
