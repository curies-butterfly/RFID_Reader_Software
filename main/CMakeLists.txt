if(CONFIG_EXAMPLE_ENABLE_WEB_ROUTER)
    idf_component_register(SRC_DIRS "."
                    INCLUDE_DIRS ".")
    spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
else()
    idf_component_register(SRCS "dev_info.c" "main.c" "rfidnetwork.c" "httpconfig.c" "parameter.c" 
                    INCLUDE_DIRS ".")
endif()