#include "watchy/captive_portal.h"

#include <string.h>

#include "esp_memory_utils.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "heap_memory_layout.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#define WATCHY_CAPTIVE_DNS_PACKET_SIZE 512u
#define WATCHY_CAPTIVE_DNS_PORT 53u
#define WATCHY_CAPTIVE_DNS_RECEIVE_TIMEOUT_US 200000L
#define WATCHY_CAPTIVE_DNS_TASK_STACK_BYTES 4096u

static const char *TAG = "watchy_captive";

extern uint8_t _watchy_captive_task_region_start[];
extern uint8_t _watchy_captive_task_region_end[];

/*
 * ESP32 D/IRAM at this address is internal and byte-accessible, so it meets
 * portVALID_STACK_MEM without consuming the nearly-full low DRAM data segment.
 * Keep the fixed window out of the heap because the linker owns it below.
 */
SOC_RESERVE_MEMORY_REGION((intptr_t)_watchy_captive_task_region_start,
                          (intptr_t)_watchy_captive_task_region_end,
                          watchy_captive_dns_task);

typedef enum {
    CAPTIVE_DNS_STOPPED = 0,
    CAPTIVE_DNS_STARTING,
    CAPTIVE_DNS_RUNNING,
    CAPTIVE_DNS_STOPPING,
} captive_dns_lifecycle_t;

typedef struct {
    int socket_fd;
    uint8_t address[4];
    captive_dns_lifecycle_t lifecycle;
    TaskHandle_t task;
} captive_dns_state_t;

static portMUX_TYPE s_dns_lock = portMUX_INITIALIZER_UNLOCKED;
static StaticTask_t s_dns_task_storage
    __attribute__((section(".watchy_captive_task.tcb"), aligned(16)));
static StackType_t s_dns_task_stack[
    WATCHY_CAPTIVE_DNS_TASK_STACK_BYTES / sizeof(StackType_t)]
    __attribute__((section(".watchy_captive_task.stack"), aligned(16)));
static captive_dns_state_t s_dns = {
    .socket_fd = -1,
};

_Static_assert(WATCHY_CAPTIVE_DNS_PACKET_SIZE <= 512u,
               "captive DNS packets must stay bounded");
_Static_assert((WATCHY_CAPTIVE_DNS_TASK_STACK_BYTES % sizeof(StackType_t)) == 0u,
               "captive DNS task stack must use complete stack words");
_Static_assert(WATCHY_CAPTIVE_DNS_TASK_STACK_BYTES >= 4096u,
               "captive DNS task stack must preserve the linked call-chain reserve");

static bool dns_task_storage_is_valid(void) {
    const uint8_t *const stack_end =
        (const uint8_t *)s_dns_task_stack + sizeof(s_dns_task_stack) - 1u;
    const uint8_t *const task_end =
        (const uint8_t *)&s_dns_task_storage + sizeof(s_dns_task_storage) - 1u;

    return esp_ptr_internal(s_dns_task_stack) &&
           esp_ptr_byte_accessible(s_dns_task_stack) &&
           esp_ptr_internal(stack_end) && esp_ptr_byte_accessible(stack_end) &&
           esp_ptr_internal(&s_dns_task_storage) &&
           esp_ptr_byte_accessible(&s_dns_task_storage) &&
           esp_ptr_internal(task_end) && esp_ptr_byte_accessible(task_end);
}

static bool dns_service_snapshot(int *out_socket, uint8_t out_address[4]) {
    bool running;

    portENTER_CRITICAL(&s_dns_lock);
    running = s_dns.lifecycle == CAPTIVE_DNS_STARTING ||
              s_dns.lifecycle == CAPTIVE_DNS_RUNNING;
    if (running) {
        *out_socket = s_dns.socket_fd;
        memcpy(out_address, s_dns.address, sizeof(s_dns.address));
    }
    portEXIT_CRITICAL(&s_dns_lock);
    return running;
}

static void captive_dns_task(void *argument) {
    uint8_t packet[WATCHY_CAPTIVE_DNS_PACKET_SIZE];

    (void)argument;
    for (;;) {
        int socket_fd;
        uint8_t address[4];
        if (!dns_service_snapshot(&socket_fd, address)) {
            break;
        }

        struct sockaddr_in source;
        socklen_t source_size = sizeof(source);
        size_t reply_size = 0u;
        const ssize_t received = recvfrom(
            socket_fd, packet, sizeof(packet), 0,
            (struct sockaddr *)&source, &source_size);
        if (received <= 0) {
            continue;
        }
        if (!dns_service_snapshot(&socket_fd, address)) {
            break;
        }
        if (watchy_captive_dns_build_reply(
                packet, (size_t)received, address, packet, sizeof(packet),
                &reply_size)) {
            (void)sendto(socket_fd, packet, reply_size, 0,
                         (const struct sockaddr *)&source, source_size);
        }
    }
    vTaskSuspend(NULL);
    vTaskDelete(NULL);
}

static void wait_for_dns_task_to_finish(TaskHandle_t task) {
    while (eTaskGetState(task) != eSuspended) {
        vTaskDelay(1u);
    }
}

watchy_status_t watchy_captive_portal_start(const char *address) {
    struct sockaddr_in bind_address = {
        .sin_family = AF_INET,
        .sin_port = htons(WATCHY_CAPTIVE_DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    struct in_addr parsed_address;
    const struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = WATCHY_CAPTIVE_DNS_RECEIVE_TIMEOUT_US,
    };
    TaskHandle_t task;
    int socket_fd;

    if (address == NULL || inet_pton(AF_INET, address, &parsed_address) != 1) {
        return WATCHY_STATUS_INVALID_ARGUMENT;
    }
    portENTER_CRITICAL(&s_dns_lock);
    const bool can_start = s_dns.lifecycle == CAPTIVE_DNS_STOPPED &&
                           s_dns.socket_fd < 0 && s_dns.task == NULL;
    if (can_start) {
        s_dns.lifecycle = CAPTIVE_DNS_STARTING;
    }
    portEXIT_CRITICAL(&s_dns_lock);
    if (!can_start) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    if (!dns_task_storage_is_valid()) {
        ESP_LOGE(TAG, "DNS task storage failed ESP32 memory validation");
        portENTER_CRITICAL(&s_dns_lock);
        s_dns.lifecycle = CAPTIVE_DNS_STOPPED;
        portEXIT_CRITICAL(&s_dns_lock);
        return WATCHY_STATUS_INVALID_STATE;
    }

    socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0) {
        ESP_LOGE(TAG, "DNS socket creation failed errno=%d", errno);
    } else if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                          sizeof(timeout)) != 0) {
        ESP_LOGE(TAG, "DNS receive timeout setup failed errno=%d", errno);
    } else if (bind(socket_fd, (const struct sockaddr *)&bind_address,
                    sizeof(bind_address)) != 0) {
        ESP_LOGE(TAG, "DNS bind port 53 failed errno=%d", errno);
    } else {
        goto socket_ready;
    }
    {
        if (socket_fd >= 0) {
            (void)closesocket(socket_fd);
        }
        portENTER_CRITICAL(&s_dns_lock);
        s_dns.lifecycle = CAPTIVE_DNS_STOPPED;
        portEXIT_CRITICAL(&s_dns_lock);
        return WATCHY_STATUS_INVALID_STATE;
    }

socket_ready:

    portENTER_CRITICAL(&s_dns_lock);
    s_dns.socket_fd = socket_fd;
    memcpy(s_dns.address, &parsed_address.s_addr, sizeof(s_dns.address));
    portEXIT_CRITICAL(&s_dns_lock);
    memset(&s_dns_task_storage, 0, sizeof(s_dns_task_storage));
    memset(s_dns_task_stack, 0, sizeof(s_dns_task_stack));
    task = xTaskCreateStatic(
        captive_dns_task, "captive-dns",
        sizeof(s_dns_task_stack) / sizeof(s_dns_task_stack[0]), NULL,
        tskIDLE_PRIORITY + 1u, s_dns_task_stack, &s_dns_task_storage);
    if (task == NULL) {
        ESP_LOGE(TAG, "DNS static task creation failed");
        portENTER_CRITICAL(&s_dns_lock);
        s_dns.socket_fd = -1;
        s_dns.lifecycle = CAPTIVE_DNS_STOPPED;
        portEXIT_CRITICAL(&s_dns_lock);
        (void)closesocket(socket_fd);
        return WATCHY_STATUS_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_dns_lock);
    s_dns.task = task;
    s_dns.lifecycle = CAPTIVE_DNS_RUNNING;
    portEXIT_CRITICAL(&s_dns_lock);
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_captive_portal_stop(void) {
    watchy_status_t status = WATCHY_STATUS_OK;
    TaskHandle_t task;
    int socket_fd;

    portENTER_CRITICAL(&s_dns_lock);
    if (s_dns.lifecycle == CAPTIVE_DNS_STOPPED) {
        portEXIT_CRITICAL(&s_dns_lock);
        return WATCHY_STATUS_OK;
    }
    if (s_dns.lifecycle != CAPTIVE_DNS_RUNNING) {
        portEXIT_CRITICAL(&s_dns_lock);
        return WATCHY_STATUS_INVALID_STATE;
    }
    s_dns.lifecycle = CAPTIVE_DNS_STOPPING;
    socket_fd = s_dns.socket_fd;
    s_dns.socket_fd = -1;
    task = s_dns.task;
    portEXIT_CRITICAL(&s_dns_lock);

    if (socket_fd >= 0) {
        (void)shutdown(socket_fd, SHUT_RDWR);
        if (closesocket(socket_fd) != 0) {
            status = WATCHY_STATUS_INVALID_STATE;
        }
    }
    if (task != NULL) {
        wait_for_dns_task_to_finish(task);
        vTaskDelete(task);
    }
    portENTER_CRITICAL(&s_dns_lock);
    s_dns.task = NULL;
    s_dns.lifecycle = CAPTIVE_DNS_STOPPED;
    portEXIT_CRITICAL(&s_dns_lock);
    return status;
}
