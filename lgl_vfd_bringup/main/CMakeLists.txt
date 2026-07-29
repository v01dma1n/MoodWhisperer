# lgl_vfd_bringup app.

idf_component_register(
    SRCS
        "app_main.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        lgl_vfd_studio
        driver
)

target_compile_options(${COMPONENT_LIB} PUBLIC -std=gnu++17)
