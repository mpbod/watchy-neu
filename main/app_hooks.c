#include "app_hooks.h"

#include "esp_log.h"

static const char *TAG = "watchy_hooks";

void watchy_app_loader_hook(void) {
    ESP_LOGI(TAG, "package loader hook deferred to the runtime task");
}

void watchy_app_safe_mode_hook(void) {
    ESP_LOGW(TAG, "safe mode active; package loading suppressed");
}
