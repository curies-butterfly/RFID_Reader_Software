set(bsp_src "at.c" "algo_array_queue.c" "at_utils.c")

set(requires "driver_uart")

idf_component_register(
    SRCS ${bsp_src}
    INCLUDE_DIRS
        "."
    REQUIRES ${requires}
    )