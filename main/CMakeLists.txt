# VFDWhisperer app.

idf_component_register(
    SRCS
        "main.cpp"
        "whisperer_app.cpp"
        "whisperer_preferences.cpp"
        "whisperer_access_point_manager.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp32_ntp_clock
        esp32_ntp_clock_drivers
        nvs_flash
)

target_compile_options(${COMPONENT_LIB} PUBLIC -std=gnu++17)
