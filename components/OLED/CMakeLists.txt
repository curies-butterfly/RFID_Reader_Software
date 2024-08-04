set(bsp_src "OLEDDisplay.c" "OLEDDisplayFonts.c")

set(requires "driver" "fatfs" "COMMON")



idf_component_register(
    SRCS ${bsp_src}
    INCLUDE_DIRS
        "."  
    REQUIRES ${requires}
)

