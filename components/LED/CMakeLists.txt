set(bsp_src "led.c")

set(requires "driver" "led_indicator" "COMMON")

idf_component_register(
    SRCS ${bsp_src}
    INCLUDE_DIRS
        "."
    REQUIRES ${requires})