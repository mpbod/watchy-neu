#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <type_traits>

#include "watchy/sdk.hpp"

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

namespace {

struct StorageContext {
    std::uint32_t last_read_buffer_size = 0;
    std::uint32_t last_written_size = 0;
};

struct TransitionCapture {
    watchy_transition_request_v1_t request{};
};

static_assert(std::is_same<decltype(watchy_host_caps_v1_t{}.size), std::uint32_t>::value,
              "watchy_host_caps_v1_t.size must be uint32_t");

struct NetworkV10Prefix {
    void *context;
    bool (*connected)(void *context);
};

struct BluetoothV10Prefix {
    void *context;
    bool (*enabled)(void *context);
};

struct SystemV10Prefix {
    void *context;
    std::uint32_t (*millis)(void *context);
    void (*sleep_ms)(void *context, std::uint32_t duration_ms);
    void (*log)(void *context, const char *message);
};

static_assert(offsetof(watchy_network_api_v1_t, request) == sizeof(NetworkV10Prefix),
              "ABI 1.0 C++ network prefix changed");
static_assert(offsetof(watchy_bluetooth_api_v1_t, request) == sizeof(BluetoothV10Prefix),
              "ABI 1.0 C++ Bluetooth prefix changed");
static_assert(offsetof(watchy_system_api_v1_t, request_exit) == sizeof(SystemV10Prefix),
              "ABI 1.0 C++ system prefix changed");
static_assert(WATCHY_ABI_V1_MINOR == 2u, "ABI minor must be 1.2");
static_assert(WATCHY_TRANSITION_CUT == 0, "stable effect value");
static_assert(WATCHY_STATUS_BUSY == -5, "stable busy status value");
static_assert(sizeof(((watchy_system_api_v1_t *)0)->request_transition) == sizeof(void *),
              "system API exposes transition request");

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

watchy_status_t capture_transition_request(void *context,
                                           const watchy_transition_request_v1_t *request) {
    TransitionCapture *captured = static_cast<TransitionCapture *>(context);
    captured->request = *request;
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

    TransitionCapture captured;
    watchy_system_api_v1_t system_api = {
        &captured,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        capture_transition_request,
    };
    watchy_transition_request_v1_t request{};
    request.size = sizeof(request);
    request.effect = WATCHY_TRANSITION_WIPE;

    CHECK(watchy::System(&system_api).request_transition(&request) == WATCHY_STATUS_OK);
    CHECK(captured.request.effect == WATCHY_TRANSITION_WIPE);

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
