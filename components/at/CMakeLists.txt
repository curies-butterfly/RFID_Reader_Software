# 定义源文件列表，这些文件会被当作本组件（component）的源文件参与编译
set(bsp_src "at.c" "algo_array_queue.c" "at_utils.c")

# 定义了一个变量 requires，组件编译时需要先编译并链接 driver_uart 组件
set(requires "driver_uart")

# 注册组件
idf_component_register(
    SRCS ${bsp_src}
    INCLUDE_DIRS
        "."
    REQUIRES ${requires}
)