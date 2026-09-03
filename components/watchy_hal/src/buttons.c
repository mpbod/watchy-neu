#include "watchy/buttons.h"

#include "watchy/board.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define WATCHY_BUTTON_EVENT_QUEUE_LENGTH 16u
#define WATCHY_BUTTON_TASK_STACK_BYTES 2048u
#define WATCHY_BUTTON_FILTER_POLL_TICKS 1u

static const gpio_num_t s_button_gpios[WATCHY_BUTTON_COUNT] = {
    WATCHY_PIN_BUTTON_MENU,
    WATCHY_PIN_BUTTON_BACK,
    WATCHY_PIN_BUTTON_DOWN,
    WATCHY_PIN_BUTTON_UP,
};

static bool s_ready;
static portMUX_TYPE s_service_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_service_running;
static bool s_accepting_isr;
static bool s_producer_parked = true;
static bool s_producer_created;
static bool s_overflowed;
static TaskHandle_t s_producer_task;
static QueueHandle_t s_event_queue;
static StaticQueue_t s_event_queue_storage;
static uint8_t s_event_queue_buffer[WATCHY_BUTTON_EVENT_QUEUE_LENGTH *
                                    sizeof(watchy_button_press_event_t)];
static StaticTask_t s_producer_task_storage;
static StackType_t s_producer_stack[WATCHY_BUTTON_TASK_STACK_BYTES / sizeof(StackType_t)];
static watchy_button_filter_t s_filter;
static bool s_handler_registered[WATCHY_BUTTON_COUNT];

_Static_assert((WATCHY_BUTTON_TASK_STACK_BYTES % sizeof(StackType_t)) == 0u,
               "button producer stack must occupy exactly 2 KiB");

static watchy_status_t start_event_service(void);

static TickType_t milliseconds_to_ticks(uint32_t milliseconds) {
    const uint64_t numerator = (uint64_t)milliseconds * configTICK_RATE_HZ;
    const uint64_t ticks = (numerator + 999u) / 1000u;

    if (milliseconds == 0u) {
        return 0;
    }
    if (ticks > (uint64_t)portMAX_DELAY) {
        return portMAX_DELAY;
    }
    return (TickType_t)(ticks == 0u ? 1u : ticks);
}

static bool service_is_running(void) {
    bool running;

    portENTER_CRITICAL(&s_service_lock);
    running = s_service_running;
    portEXIT_CRITICAL(&s_service_lock);
    return running;
}

static void producer_wait_for_resume(void) {
    for (;;) {
        bool running;

        portENTER_CRITICAL(&s_service_lock);
        running = s_service_running;
        s_producer_parked = !running;
        portEXIT_CRITICAL(&s_service_lock);
        if (running) {
            return;
        }
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

static void wait_for_producer_park(void) {
    for (;;) {
        bool parked;

        portENTER_CRITICAL(&s_service_lock);
        parked = s_producer_parked;
        portEXIT_CRITICAL(&s_service_lock);
        if (parked) {
            return;
        }
        vTaskDelay(1);
    }
}

static void disable_button_interrupts(void) {
    for (size_t index = 0u; index < WATCHY_BUTTON_COUNT; ++index) {
        (void)gpio_intr_disable(s_button_gpios[index]);
    }
}

static void remove_button_handlers(void) {
    for (size_t index = 0u; index < WATCHY_BUTTON_COUNT; ++index) {
        if (s_handler_registered[index]) {
            (void)gpio_intr_disable(s_button_gpios[index]);
            (void)gpio_isr_handler_remove(s_button_gpios[index]);
            s_handler_registered[index] = false;
        }
    }
}

static void IRAM_ATTR menu_button_isr(void *argument) {
    BaseType_t higher_priority_task_woken = pdFALSE;

    (void)argument;
    portENTER_CRITICAL_ISR(&s_service_lock);
    if (s_accepting_isr && s_producer_task != NULL) {
        vTaskNotifyGiveFromISR(s_producer_task, &higher_priority_task_woken);
    }
    portEXIT_CRITICAL_ISR(&s_service_lock);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static void IRAM_ATTR back_button_isr(void *argument) {
    BaseType_t higher_priority_task_woken = pdFALSE;

    (void)argument;
    portENTER_CRITICAL_ISR(&s_service_lock);
    if (s_accepting_isr && s_producer_task != NULL) {
        vTaskNotifyGiveFromISR(s_producer_task, &higher_priority_task_woken);
    }
    portEXIT_CRITICAL_ISR(&s_service_lock);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static void IRAM_ATTR down_button_isr(void *argument) {
    BaseType_t higher_priority_task_woken = pdFALSE;

    (void)argument;
    portENTER_CRITICAL_ISR(&s_service_lock);
    if (s_accepting_isr && s_producer_task != NULL) {
        vTaskNotifyGiveFromISR(s_producer_task, &higher_priority_task_woken);
    }
    portEXIT_CRITICAL_ISR(&s_service_lock);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static void IRAM_ATTR up_button_isr(void *argument) {
    BaseType_t higher_priority_task_woken = pdFALSE;

    (void)argument;
    portENTER_CRITICAL_ISR(&s_service_lock);
    if (s_accepting_isr && s_producer_task != NULL) {
        vTaskNotifyGiveFromISR(s_producer_task, &higher_priority_task_woken);
    }
    portEXIT_CRITICAL_ISR(&s_service_lock);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static const gpio_isr_t s_button_handlers[WATCHY_BUTTON_COUNT] = {
    menu_button_isr,
    back_button_isr,
    down_button_isr,
    up_button_isr,
};

static void enqueue_stable_presses(void) {
    const uint32_t timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
    const watchy_button_mask_t pressed_mask = watchy_buttons_filter_observe(
        &s_filter, watchy_buttons_sample(), timestamp_ms);

    if (pressed_mask != 0u && service_is_running()) {
        const watchy_button_press_event_t event = {
            .mask = pressed_mask,
            .timestamp_ms = timestamp_ms,
        };

        if (xQueueSend(s_event_queue, &event, 0) != pdPASS) {
            portENTER_CRITICAL(&s_service_lock);
            s_overflowed = true;
            portEXIT_CRITICAL(&s_service_lock);
        }
    }
}

static void button_producer_task(void *argument) {
    (void)argument;

    for (;;) {
        TickType_t timeout;
        bool notified;

        producer_wait_for_resume();
        timeout = s_filter.pending_mask != 0u ? WATCHY_BUTTON_FILTER_POLL_TICKS : portMAX_DELAY;
        notified = ulTaskNotifyTake(pdTRUE, timeout) != 0u;

        if (!service_is_running()) {
            continue;
        }
        if (notified || s_filter.pending_mask != 0u) {
            enqueue_stable_presses();
        }
    }
}

static void stop_event_service(void) {
    TaskHandle_t producer_task;

    portENTER_CRITICAL(&s_service_lock);
    s_accepting_isr = false;
    s_service_running = false;
    producer_task = s_producer_task;
    portEXIT_CRITICAL(&s_service_lock);
    disable_button_interrupts();
    remove_button_handlers();

    if (producer_task != NULL) {
        xTaskNotifyGive(producer_task);
        wait_for_producer_park();
    }
    if (s_event_queue != NULL) {
        (void)xQueueReset(s_event_queue);
        s_event_queue = NULL;
    }
}

static watchy_status_t start_event_service(void) {
    esp_err_t result;

    if (s_event_queue != NULL) {
        return WATCHY_STATUS_OK;
    }

    s_event_queue = xQueueCreateStatic(WATCHY_BUTTON_EVENT_QUEUE_LENGTH,
                                       sizeof(watchy_button_press_event_t),
                                       s_event_queue_buffer,
                                       &s_event_queue_storage);
    if (s_event_queue == NULL) {
        return WATCHY_STATUS_INVALID_STATE;
    }

    watchy_buttons_filter_init(&s_filter,
                                watchy_buttons_sample(),
                                WATCHY_BUTTON_DEBOUNCE_MS);
    result = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        s_event_queue = NULL;
        return WATCHY_STATUS_INVALID_STATE;
    }

    if (!s_producer_created) {
        s_producer_task = xTaskCreateStatic(button_producer_task,
                                            "button-input",
                                            sizeof(s_producer_stack) / sizeof(s_producer_stack[0]),
                                            NULL,
                                            tskIDLE_PRIORITY + 1u,
                                            s_producer_stack,
                                            &s_producer_task_storage);
        if (s_producer_task == NULL) {
            s_event_queue = NULL;
            return WATCHY_STATUS_INVALID_STATE;
        }
        s_producer_created = true;
    }

    for (size_t index = 0u; index < WATCHY_BUTTON_COUNT; ++index) {
        result = gpio_isr_handler_add(s_button_gpios[index], s_button_handlers[index], NULL);
        if (result != ESP_OK) {
            stop_event_service();
            return WATCHY_STATUS_INVALID_STATE;
        }
        s_handler_registered[index] = true;
    }

    portENTER_CRITICAL(&s_service_lock);
    s_service_running = true;
    s_accepting_isr = true;
    s_producer_parked = false;
    portEXIT_CRITICAL(&s_service_lock);
    xTaskNotifyGive(s_producer_task);
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_buttons_init(void) {
    const uint64_t mask = (UINT64_C(1) << WATCHY_PIN_BUTTON_MENU) |
                          (UINT64_C(1) << WATCHY_PIN_BUTTON_BACK) |
                          (UINT64_C(1) << WATCHY_PIN_BUTTON_DOWN) |
                          (UINT64_C(1) << WATCHY_PIN_BUTTON_UP);
    gpio_config_t config = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    watchy_status_t status;

    if (s_ready) {
        return WATCHY_STATUS_OK;
    }
    if (gpio_config(&config) != ESP_OK) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    disable_button_interrupts();
    s_ready = true;
    s_overflowed = false;
    status = start_event_service();
    if (status != WATCHY_STATUS_OK) {
        disable_button_interrupts();
        s_ready = false;
    }
    return status;
}

bool watchy_buttons_ready(void) {
    return s_ready;
}

watchy_button_mask_t watchy_buttons_sample(void) {
    watchy_button_mask_t mask = 0;

    if (!s_ready) {
        return 0;
    }
    if (gpio_get_level(WATCHY_PIN_BUTTON_MENU) == WATCHY_BUTTON_ACTIVE_LEVEL) {
        mask |= WATCHY_BUTTON_MASK_MENU;
    }
    if (gpio_get_level(WATCHY_PIN_BUTTON_BACK) == WATCHY_BUTTON_ACTIVE_LEVEL) {
        mask |= WATCHY_BUTTON_MASK_BACK;
    }
    if (gpio_get_level(WATCHY_PIN_BUTTON_DOWN) == WATCHY_BUTTON_ACTIVE_LEVEL) {
        mask |= WATCHY_BUTTON_MASK_DOWN;
    }
    if (gpio_get_level(WATCHY_PIN_BUTTON_UP) == WATCHY_BUTTON_ACTIVE_LEVEL) {
        mask |= WATCHY_BUTTON_MASK_UP;
    }
    return mask;
}

bool watchy_buttons_take_press(watchy_button_press_event_t *out_event,
                               uint32_t timeout_ms) {
    if (out_event == NULL || s_event_queue == NULL) {
        return false;
    }
    return xQueueReceive(s_event_queue, out_event, milliseconds_to_ticks(timeout_ms)) == pdPASS;
}

bool watchy_buttons_press_pending(void) {
    return s_event_queue != NULL && uxQueueMessagesWaiting(s_event_queue) != 0u;
}

bool watchy_buttons_overflowed(void) {
    bool overflowed;

    portENTER_CRITICAL(&s_service_lock);
    overflowed = s_overflowed;
    portEXIT_CRITICAL(&s_service_lock);
    return overflowed;
}

watchy_status_t watchy_buttons_quiesce(void) {
    stop_event_service();
    return WATCHY_STATUS_OK;
}

watchy_status_t watchy_buttons_resume(void) {
    if (!s_ready) {
        return WATCHY_STATUS_INVALID_STATE;
    }
    return start_event_service();
}

void watchy_buttons_deinit(void) {
    (void)watchy_buttons_quiesce();
    s_overflowed = false;
    s_ready = false;
}
