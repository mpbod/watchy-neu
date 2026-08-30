idf_component_register(
    SRCS
        "src/battery.c"
        "src/battery_policy.c"
        "src/buses.c"
        "src/buttons.c"
        "src/buttons_policy.c"
        "src/diagnostics.c"
        "src/display.c"
        "src/display_policy.c"
        "src/framebuffer.c"
        "src/haptics.c"
        "src/motion.cpp"
        "src/power.c"
        "src/power_policy.c"
        "src/radios.c"
        "src/radios_policy.c"
        "src/rtc.c"
        "src/rtc_calendar.c"
        "src/storage.c"
        "src/storage_policy.c"
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "src"
    REQUIRES
        watchy_core
        driver
        esp_adc
        esp_event
        esp_netif
        esp_timer
        esp_system
        esp_wifi
        bt
        nvs_flash
        littlefs
        sensorlib
)

target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11 cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror
    -Wframe-larger-than=2048
    -fstack-usage
)
