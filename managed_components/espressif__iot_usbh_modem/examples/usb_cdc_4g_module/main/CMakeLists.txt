if(CONFIG_EXAMPLE_ENABLE_WEB_ROUTER)
    idf_component_register(SRC_DIRS "."
                    INCLUDE_DIRS ".")
    spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
else()
    idf_component_register(SRC_DIRS "."
                    INCLUDE_DIRS "."
                    EXCLUDE_SRCS "app_main.c" "modem_http_config.c")
endif()
