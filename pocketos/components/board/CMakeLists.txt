idf_component_register(SRCS "board.c" "kbd_tca8418.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES os_abi
                       PRIV_REQUIRES esp_lcd driver espressif__esp_lcd_ili9341
                                     fatfs sdmmc esp_driver_sdspi)
