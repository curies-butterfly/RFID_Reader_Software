
set(bsp_src "ntc_temp_adc.c")

set(requires "driver" "fatfs" "LED" "COMMON")

idf_component_register(
    SRCS ${bsp_src}
    INCLUDE_DIRS
        "."  
    REQUIRES ${requires}
)
