set(bsp_src "rfidmodule.c" )

set(requires "driver_uart" "COMMON" "RS485" "main")

idf_component_register(
    SRCS ${bsp_src}
    INCLUDE_DIRS
        "."
    REQUIRES ${requires})