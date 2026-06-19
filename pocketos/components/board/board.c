#include "board.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "board";

/* ------------------------------------------------------------------ *
 * Pin assignments — Waveshare ESP32-S3-Touch-LCD-2.8
 * Display: ST7789 240x320 IPS, SPI
 * Touch:   CST816S capacitive, I2C
 * ------------------------------------------------------------------ */

/* ST7789 — SPI2 (FSPI) */
#define PIN_LCD_SCLK    1
#define PIN_LCD_MOSI    2
#define PIN_LCD_MISO   42
#define PIN_LCD_DC     41
#define PIN_LCD_CS     39
#define PIN_LCD_RST    40
#define PIN_LCD_BL      6

/* CST816S — capacitive touch, I2C */
#define PIN_TOUCH_SDA  15
#define PIN_TOUCH_SCL   7
#define PIN_TOUCH_INT  17
#define PIN_TOUCH_RST  16
#define TOUCH_ADDR     0x1A

#define LCD_HOST        SPI2_HOST
#define LCD_W           240
#define LCD_H           320
#define LCD_SPI_CLK_HZ  (40 * 1000 * 1000)

/* ------------------------------------------------------------------ *
 * CST816S state (module-level, not exposed through board_t)
 * ------------------------------------------------------------------ */

static i2c_master_bus_handle_t s_i2c_bus   = NULL;
static i2c_master_dev_handle_t s_touch_dev = NULL;
static bool                    s_touch_ok  = false;

/* ------------------------------------------------------------------ */

static esp_err_t touch_init(void)
{
    /* Hard-reset the CST816S */
    gpio_config_t rst_cfg = {
        .pin_bit_mask = (1ULL << PIN_TOUCH_RST),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&rst_cfg));
    gpio_set_level(PIN_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* I2C master bus */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port            = I2C_NUM_0,
        .sda_io_num          = PIN_TOUCH_SDA,
        .scl_io_num          = PIN_TOUCH_SCL,
        .clk_source          = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt   = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus: %s", esp_err_to_name(err));
        return err;
    }

    /* CST816S device */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = TOUCH_ADDR,
        .scl_speed_hz    = 100000,
    };
    err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_touch_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device: %s", esp_err_to_name(err));
        return err;
    }

    /* Probe device presence */
    err = i2c_master_probe(s_i2c_bus, TOUCH_ADDR, 50);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "CST816S not found at 0x%02X — touch disabled", TOUCH_ADDR);
        return err;
    }

    /* Enable motion interrupts: IRQ_CTL register 0xFA = 0x71 */
    uint8_t cfg[2] = { 0xFA, 0x71 };
    err = i2c_master_transmit(s_touch_dev, cfg, sizeof(cfg), 50);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "touch IRQ cfg failed: %s (non-fatal)", esp_err_to_name(err));
    }

    s_touch_ok = true;
    ESP_LOGI(TAG, "CST816S ready");
    return ESP_OK;
}

/* ------------------------------------------------------------------ */

bool board_touch_read(board_t *b, int *x, int *y, bool *pressed)
{
    (void)b;
    *x = 0; *y = 0; *pressed = false;
    if (!s_touch_ok) return false;

    /* Write register pointer 0x01, then read 6 bytes of event data */
    uint8_t reg  = 0x01;
    uint8_t buf[6] = {0};
    esp_err_t err = i2c_master_transmit_receive(s_touch_dev, &reg, 1, buf, 6, 50);
    if (err != ESP_OK) return false;

    uint8_t cnt  = buf[1];           /* FingerNum */
    uint8_t xh   = buf[2];
    uint8_t xl   = buf[3];
    uint8_t yh   = buf[4];
    uint8_t yl   = buf[5];
    uint8_t flag = (xh >> 6) & 0x03; /* 0=down, 1=up, 2=contact */

    *x       = ((xh & 0x0F) << 8) | xl;
    *y       = ((yh & 0x0F) << 8) | yl;
    *pressed = (cnt > 0) && (flag != 1);
    return true;
}

/* ------------------------------------------------------------------ */

esp_err_t board_init(board_t *out)
{
    memset(out, 0, sizeof(*out));
    strncpy(out->caps.board_name, "waveshare-s3-28", sizeof(out->caps.board_name) - 1);
    out->caps.screen_w     = LCD_W;
    out->caps.screen_h     = LCD_H;
    out->caps.panel_class  = OS_PANEL_SPI;
    out->caps.touch        = OS_TOUCH_CAPACITIVE;
    out->caps.has_keyboard = 0;
    out->caps.has_sd_card  = 0;

    /* Backlight as plain output GPIO, off until panel is ready */
    gpio_config_t gpio_cfg = {
        .pin_bit_mask = (1ULL << PIN_LCD_BL),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&gpio_cfg));
    gpio_set_level(PIN_LCD_BL, 0);

    /* SPI bus */
    spi_bus_config_t buscfg = {
        .mosi_io_num     = PIN_LCD_MOSI,
        .miso_io_num     = PIN_LCD_MISO,
        .sclk_io_num     = PIN_LCD_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_W * 40 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* Panel IO over SPI */
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num       = PIN_LCD_DC,
        .cs_gpio_num       = PIN_LCD_CS,
        .pclk_hz           = LCD_SPI_CLK_HZ,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .spi_mode          = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));

    /* ST7789 panel */
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_endian     = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_cfg, &panel));

    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    esp_lcd_panel_invert_color(panel, true);    /* IPS needs inversion */
    esp_lcd_panel_mirror(panel, false, false);
    esp_lcd_panel_disp_on_off(panel, true);

    gpio_set_level(PIN_LCD_BL, 1);

    out->panel = panel;
    out->io    = io;

    ESP_LOGI(TAG, "display up: %s %ux%u (ST7789 SPI)", out->caps.board_name, LCD_W, LCD_H);

    /* CST816S capacitive touch — non-fatal if not present */
    esp_err_t touch_err = touch_init();
    if (touch_err != ESP_OK) {
        out->caps.touch = OS_TOUCH_NONE;
        ESP_LOGW(TAG, "touch unavailable (%s)", esp_err_to_name(touch_err));
    }

    return ESP_OK;
}
