#include "board.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "board";

/* ------------------------------------------------------------------ *
 * Pin assignments — ESP32-S3 custom board
 * ------------------------------------------------------------------ */

/* ILI9341 — 8-bit 8080 parallel (i80) */
#define PIN_LCD_D0    4
#define PIN_LCD_D1    5
#define PIN_LCD_D2    6
#define PIN_LCD_D3    7
#define PIN_LCD_D4   15
#define PIN_LCD_D5   16
#define PIN_LCD_D6   17
#define PIN_LCD_D7   18
#define PIN_LCD_WR    8
#define PIN_LCD_DC    9
#define PIN_LCD_CS   10
#define PIN_LCD_RST  11
#define PIN_LCD_BL   12

/* XPT2046 — resistive touch, dedicated SPI2 */
#define PIN_TOUCH_CS    40
#define PIN_TOUCH_CLK   41
#define PIN_TOUCH_MOSI  42
#define PIN_TOUCH_MISO   3
#define PIN_TOUCH_IRQ   39

/* TCA8418 — QWERTY keyboard encoder, I2C */
#define PIN_KBD_SCL  46
#define PIN_KBD_SDA  48
#define PIN_KBD_INT  -1

#define LCD_W  240
#define LCD_H  320
#define LCD_CLK_HZ  (10 * 1000 * 1000)

/* ------------------------------------------------------------------ */

esp_err_t board_init(board_t *out)
{
    memset(out, 0, sizeof(*out));
    strncpy(out->caps.board_name, "tardi-s3", sizeof(out->caps.board_name) - 1);
    out->caps.screen_w     = LCD_W;
    out->caps.screen_h     = LCD_H;
    out->caps.panel_class  = OS_PANEL_I80;
    out->caps.touch        = OS_TOUCH_RESISTIVE;
    out->caps.has_keyboard = 1;

    /* BL and RST as plain output GPIOs */
    gpio_config_t gpio_cfg = {
        .pin_bit_mask = (1ULL << PIN_LCD_BL) | (1ULL << PIN_LCD_RST),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&gpio_cfg));
    gpio_set_level(PIN_LCD_BL, 0);

    /* i80 bus */
    esp_lcd_i80_bus_handle_t bus;
    esp_lcd_i80_bus_config_t bus_cfg = {
        .clk_src         = LCD_CLK_SRC_DEFAULT,
        .dc_gpio_num     = PIN_LCD_DC,
        .wr_gpio_num     = PIN_LCD_WR,
        .data_gpio_nums  = {
            PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3,
            PIN_LCD_D4, PIN_LCD_D5, PIN_LCD_D6, PIN_LCD_D7,
        },
        .bus_width           = 8,
        .max_transfer_bytes  = LCD_W * 40 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_cfg, &bus));

    /* Panel IO */
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_i80_config_t io_cfg = {
        .cs_gpio_num       = PIN_LCD_CS,
        .pclk_hz           = LCD_CLK_HZ,
        .trans_queue_depth = 10,
        .dc_levels = {
            .dc_idle_level  = 0,
            .dc_cmd_level   = 0,
            .dc_dummy_level = 0,
            .dc_data_level  = 1,
        },
        .flags = {
            .swap_color_bytes = 1,
        },
        .lcd_cmd_bits   = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(bus, &io_cfg, &io));

    /* ILI9341 panel — gives us a proper esp_lcd_panel_handle_t */
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .data_endian    = LCD_RGB_DATA_ENDIAN_BIG,   /* RGB, not BGR */
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io, &panel_cfg, &panel));

    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    esp_lcd_panel_invert_color(panel, false);
    esp_lcd_panel_mirror(panel, false, false);
    esp_lcd_panel_disp_on_off(panel, true);

    gpio_set_level(PIN_LCD_BL, 1);

    out->panel = panel;   /* handed to LVGL port in main */
    out->io    = io;

    ESP_LOGI(TAG, "display up: %s %ux%u", out->caps.board_name, LCD_W, LCD_H);

    /* Touch (XPT2046) and keyboard (TCA8418): Phase 1b */
    return ESP_OK;
}
