set(bsp_src "tp1107.c")

set(requires "at" "driver")

idf_component_register(
    SRCS ${bsp_src}
    INCLUDE_DIRS
        "."
    REQUIRES ${requires})