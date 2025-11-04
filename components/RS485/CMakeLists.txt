set(bsp_src "modbus_rtu.c")

set(requires "driver_uart" "RFIDModule")

idf_component_register(
    SRCS ${bsp_src}
    INCLUDE_DIRS
        "."
    REQUIRES ${requires}
    PRIV_REQUIRES main     # 声明对 main 组件的依赖
)