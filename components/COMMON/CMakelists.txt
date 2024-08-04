set(bsp_src "rfidcommon.c")

set(requires "driver" )

idf_component_register(
    SRCS ${bsp_src}
    INCLUDE_DIRS
        "."
    REQUIRES ${requires})