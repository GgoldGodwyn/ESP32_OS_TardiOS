idf_component_register(SRCS "board.c"
                       INCLUDE_DIRS "include"
                       REQUIRES os_abi
                       PRIV_REQUIRES esp_lcd driver espressif__esp_lcd_ili9341)
