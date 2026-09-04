idf_component_register(
    SRCS
        "src/captive_portal_idf.c"
        "src/captive_portal_policy.c"
        "src/portal_idf.c"
        "src/portal_policy.c"
        "src/settings_idf.c"
        "src/settings_policy.c"
        "src/shell_policy.c"
        "src/shell_render.c"
        "src/time_sync_idf.c"
        "src/time_sync_policy.c"
        "src/timezone_action_policy.c"
        "src/watchface_action_policy.c"
        "../../sdk/ui/src/ui_draw.c"
        "../../sdk/ui/generated/watchy_fonts.c"
    INCLUDE_DIRS
        "include"
        "../../sdk/ui/include"
        "../../sdk/ui/generated"
    REQUIRES
        watchy_core
        watchy_hal
        watchy_packages
        esp_http_server
        esp_app_format
        esp_netif
        esp_wifi
        bootloader_support
        json
        mbedtls
        nvs_flash
)

target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror
    -Wframe-larger-than=2048
    -fstack-usage
)
