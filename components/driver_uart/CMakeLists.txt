set(bsp_src "driver_uart.c")

set(requires "driver" "fatfs" "RFIDModule" "COMMON")

idf_component_register(
    SRCS ${bsp_src}
    INCLUDE_DIRS
        "."
    REQUIRES ${requires})

    