set(bsp_src "sc16is752.c")

set(requires "driver")

idf_component_register(
    SRCS ${bsp_src}
    INCLUDE_DIRS
        "."
    REQUIRES ${requires})