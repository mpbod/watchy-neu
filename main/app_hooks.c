#include "app_hooks.h"

#include "esp_log.h"
#include "watchy/package_runtime.h"

static const char *TAG = "watchy_hooks";

bool watchy_app_loader_hook(void) {
    if (!watchy_packages_link_smoke()) {
        ESP_LOGE(TAG, "package install/select linkage unavailable");
        return false;
    }
    const bool rendered = watchy_packages_run_watchface(false);
    ESP_LOGI(TAG, "package watchface rendered=%d", rendered);
    return rendered;
}

void watchy_app_safe_mode_hook(void) {
    ESP_LOGW(TAG, "safe mode active; package loading suppressed");
}
