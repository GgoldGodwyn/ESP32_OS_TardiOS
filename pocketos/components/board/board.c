#include "board.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_touch_xpt2046.h"
#include "kbd_tca8418.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "board";

/* ------------------------------------------------------------------ *
 * Pin assignments — per target board
 * ------------------------------------------------------------------ */

#if CONFIG_IDF_TARGET_ESP32S3
/* tardi-s3 — ESP32-S3 custom board. See PIN_ASSIGNMENTS.md. */
#define BOARD_NAME "tardi-s3"

/* ILI9341 — 3.2" panel pulled from a DevEBox STM32F4VE (see its J1 TFT
 * header schematic in PIN_ASSIGNMENTS.md). Panel has a solder-selectable
 * 8-bit/16-bit resistor bridge, must be set for 8-bit mode. 8-bit mode
 * uses the panel's UPPER 8 data lines (DB8-DB15 / the STM32 schematic's
 * FSMC_D8-D15), not the lower 8 (DB0-DB7 / FSMC_D0-D7) — this is ILI9341
 * chip-level behavior, not board-specific. Leaving the bridge on 16-bit
 * while only driving 8 lines (or wiring the wrong half) corrupts every
 * pixel word, which is what produced the raster/garbage screen before
 * this was diagnosed. */
#define PIN_LCD_D0    4   // -> FSMC_D8  (schematic pin 10)
#define PIN_LCD_D1    5   // -> FSMC_D9  (schematic pin 9)
#define PIN_LCD_D2    6   // -> FSMC_D10 (schematic pin 8)
#define PIN_LCD_D3    7   // -> FSMC_D11 (schematic pin 7)
#define PIN_LCD_D4   15   // -> FSMC_D12 (schematic pin 6)
#define PIN_LCD_D5   16   // -> FSMC_D13 (schematic pin 5)
#define PIN_LCD_D6   17   // -> FSMC_D14 (schematic pin 4)
#define PIN_LCD_D7   18   // -> FSMC_D15 (schematic pin 3)
#define PIN_LCD_WR    8   // -> FSMC_NWE (schematic pin 20)
#define PIN_LCD_DC    9   // RS -> FSMC_A18 (schematic pin 21)
#define PIN_LCD_CS   10   // SCS -> FSMC_NE1 (schematic pin 22)
#define PIN_LCD_RST  11   // -> RST (schematic pin 2)
#define PIN_LCD_BL   12   // -> LCD_BL (schematic pin 28)

/* XPT2046 — resistive touch, dedicated SPI2. Silkscreen/schematic labels
 * in parens (schematic pin numbers per PIN_ASSIGNMENTS.md's J1 table). */
#define PIN_TOUCH_CS    47  // CS_T  -> Touch_CS   (schematic pin 24) — moved
                            // from GPIO40, testing whether that specific
                            // pin was involved in the "needs power cycle"
                            // touch issue.
#define PIN_TOUCH_CLK   41  // SCK   -> Touch_CLK  (schematic pin 23)
#define PIN_TOUCH_MOSI  42  // SI    -> Touch_MOSI (schematic pin 25)
#define PIN_TOUCH_MISO   3  // SO    -> Touch_MISO (schematic pin 26)
#define PIN_TOUCH_IRQ   39  // INT   -> Touch_PEN  (schematic pin 27)

/* TCA8418 — QWERTY keyboard encoder, I2C */
#define PIN_KBD_SCL  46
#define PIN_KBD_SDA  48
#define PIN_KBD_INT  -1

/* microSD — SD-over-SPI, shares the touch SPI2 bus. Only CS is new. */
#define PIN_SD_CS  38

#define LCD_W  240
#define LCD_H  320
#define LCD_CLK_HZ  (10 * 1000 * 1000)

#elif CONFIG_IDF_TARGET_ESP32P4
/* tardi-p4 — pin-out not finalized yet. Fill these in once the P4 board
 * is wired, then delete this #error. Do not guess values here: a wrong
 * pin number compiles silently and fails at runtime instead. */
// #error "ESP32-P4 board pins not yet defined — fill in PIN_LCD_*/PIN_TOUCH_*/PIN_KBD_*, LCD_W/LCD_H/LCD_CLK_HZ, and BOARD_NAME in board.c for your P4 wiring"

/* tardi-s3 — ESP32-S3 custom board. See PIN_ASSIGNMENTS.md. */
#define BOARD_NAME "tardi-p4"

/* ILI9341 — 8-bit 8080 parallel (i80) */
#define PIN_LCD_D0    39
#define PIN_LCD_D1    40
#define PIN_LCD_D2    41
#define PIN_LCD_D3    42
#define PIN_LCD_D4    43
#define PIN_LCD_D5    44
#define PIN_LCD_D6    45
#define PIN_LCD_D7    46
#define PIN_LCD_WR    47
#define PIN_LCD_DC    48
#define PIN_LCD_CS    49
// #define PIN_LCD_RD    50
#define PIN_LCD_RST  51
#define PIN_LCD_BL   52

/* XPT2046 — resistive touch, dedicated SPI2 */
#define PIN_TOUCH_CS    54
#define PIN_TOUCH_CLK   3
#define PIN_TOUCH_MOSI  4
#define PIN_TOUCH_MISO  5
#define PIN_TOUCH_IRQ   -1

/* TCA8418 — QWERTY keyboard encoder, I2C */
#define PIN_KBD_SCL  1
#define PIN_KBD_SDA  2
#define PIN_KBD_INT  12

/* microSD — SD-over-SPI, shares the touch SPI2 bus. Only CS is new. */
#define PIN_SD_CS  9

#define LCD_W  240
#define LCD_H  320
#define LCD_CLK_HZ  (10 * 1000 * 1000)

#else
#error "TardiOS board component: unsupported CONFIG_IDF_TARGET"
#endif

/* XPT2046 lives on its own dedicated SPI bus (separate from the i80 LCD bus,
 * which has no MISO line). See PIN_ASSIGNMENTS.md. */
#define TOUCH_SPI_HOST  SPI2_HOST

/* TCA8418 keypad matrix — same physical keypad (4 rows x 10 columns) on
 * both boards. */
#define KBD_ROWS  4
#define KBD_COLS  10

/* ------------------------------------------------------------------ */

/* KNOWN ISSUE: touch sometimes doesn't respond until the board is fully
 * power-cycled (EN/RESET alone isn't enough) — XPT2046 has no dedicated
 * hardware reset pin (confirmed absent from the board's J1 schematic), so
 * its Vcc stays powered through a soft reset and it can end up in a
 * confused SPI protocol state. Tried and reverted: a GPIO bit-bang resync
 * before spi_bus_initialize(), a retry loop with a sanity read, and an
 * explicit periph_module_reset(PERIPH_SPI2_MODULE) — none of it helped, so
 * back to the simple version rather than carrying dead-weight complexity.
 * Next things worth trying: remapping PIN_TOUCH_CS to a different GPIO (in
 * case this specific pin has a strapping/contention issue), or a real
 * hardware fix (put the touch IC's Vcc behind a GPIO-controlled switch so
 * firmware can force an actual power-cycle of just that chip). */
static esp_err_t touch_init(board_t *out)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = PIN_TOUCH_MOSI,
        .miso_io_num     = PIN_TOUCH_MISO,
        .sclk_io_num     = PIN_TOUCH_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 64,
    };
    esp_err_t err = spi_bus_initialize(TOUCH_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "touch spi_bus_initialize failed: %d", err);
        return err;
    }

    esp_lcd_panel_io_handle_t tp_io;
    esp_lcd_panel_io_spi_config_t tp_io_cfg =
        ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(PIN_TOUCH_CS);
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TOUCH_SPI_HOST, &tp_io_cfg, &tp_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "touch panel_io_spi failed: %d", err);
        return err;
    }

    esp_lcd_touch_config_t tp_cfg = {
        .x_max        = LCD_W,
        .y_max        = LCD_H,
        .rst_gpio_num = -1,
        .int_gpio_num = PIN_TOUCH_IRQ,
        .flags = {
            .swap_xy  = 0,   /* calibration data showed swap_xy=1 was wrong: measured
                              * axes tracked (target.y, target.x) — i.e. already-swapped
                              * relative to target — so no swap is actually needed. */
            .mirror_x = 1,   /* X is also inverted relative to the panel */
            .mirror_y = 1,   /* Y is also inverted relative to the panel */
        },
    };
    esp_lcd_touch_handle_t tp;
    err = esp_lcd_touch_new_spi_xpt2046(tp_io, &tp_cfg, &tp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "xpt2046 init failed: %d", err);
        return err;
    }

    out->touch_handle = tp;
    ESP_LOGI(TAG, "touch up: xpt2046 on SPI2 (cs=%d irq=%d)", PIN_TOUCH_CS, PIN_TOUCH_IRQ);
    return ESP_OK;
}

/* microSD over SPI, sharing the already-initialized touch SPI2 bus.
 * Must run after touch_init() — it depends on that bus being up. */
static esp_err_t sd_init(board_t *out)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = TOUCH_SPI_HOST;

    sdspi_device_config_t dev_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev_cfg.host_id = TOUCH_SPI_HOST;
    dev_cfg.gpio_cs = PIN_SD_CS;

    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,   /* never auto-format a user's SD card */
        .max_files              = 4,
        .allocation_unit_size   = 16 * 1024,
    };

    sdmmc_card_t *card;
    esp_err_t err = esp_vfs_fat_sdspi_mount("/sdcard", &host, &dev_cfg, &mount_cfg, &card);
    if (err != ESP_OK) {
        return err;
    }

    out->sdcard_mounted = true;
    ESP_LOGI(TAG, "sdcard up: /sdcard (cs=%d, shared SPI2)", PIN_SD_CS);
    return ESP_OK;
}

static esp_err_t kbd_init(board_t *out)
{
    if (!kbd_tca8418_init(PIN_KBD_SCL, PIN_KBD_SDA, PIN_KBD_INT, KBD_ROWS, KBD_COLS)) {
        return ESP_FAIL;
    }
    (void)out;
    ESP_LOGI(TAG, "keyboard up: tca8418 %dx%d (scl=%d sda=%d int=%d)",
             KBD_ROWS, KBD_COLS, PIN_KBD_SCL, PIN_KBD_SDA, PIN_KBD_INT);
    return ESP_OK;
}

esp_err_t board_init(board_t *out)
{
    memset(out, 0, sizeof(*out));
    strncpy(out->caps.board_name, BOARD_NAME, sizeof(out->caps.board_name) - 1);
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

    esp_err_t touch_err = touch_init(out);
    if (touch_err != ESP_OK) {
        ESP_LOGW(TAG, "touch unavailable (%d) — continuing without it", touch_err);
        out->caps.touch = OS_TOUCH_NONE;
    } else {
        /* SD card shares the touch SPI2 bus, so it can only come up if touch did. */
        esp_err_t sd_err = sd_init(out);
        if (sd_err != ESP_OK) {
            ESP_LOGW(TAG, "sdcard unavailable (%d) — continuing without it", sd_err);
        }
    }

    esp_err_t kbd_err = kbd_init(out);
    if (kbd_err != ESP_OK) {
        ESP_LOGW(TAG, "keyboard unavailable (%d) — continuing without it", kbd_err);
        out->caps.has_keyboard = 0;
    }
    /* Keyboard is driver-level only so far: kbd_tca8418_get_key() returns raw
     * TCA8418 key numbers, not LVGL indev events — no physical keymap exists
     * yet to translate row/col into characters. kbd_indev stays NULL. */

    return ESP_OK;
}
