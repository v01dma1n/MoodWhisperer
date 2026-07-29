# LGL Studio VFD display driver — standalone, no esp32_ntp_clock dependency.
# Used for bare-board bring-up before this glass is wired into a larger app.

idf_component_register(
    SRCS
        "src/lgl_vfd_studio.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver
        esp_rom
)

target_compile_options(${COMPONENT_LIB} PUBLIC -std=gnu++17)
