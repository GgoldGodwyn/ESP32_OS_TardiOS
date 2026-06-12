idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES os_abi board app_loader nvs_flash esp_timer joltwallet__littlefs
                                 espressif__esp_lvgl_port)
littlefs_create_partition_image(storage ${CMAKE_SOURCE_DIR}/data FLASH_IN_PROJECT)