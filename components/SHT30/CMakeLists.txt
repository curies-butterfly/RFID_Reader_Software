set(bsp_src "sht30.c")

set(requires "driver" "SHT30")

idf_component_register(
    SRCS ${bsp_src}
    INCLUDE_DIRS
        "."
    REQUIRES ${requires})