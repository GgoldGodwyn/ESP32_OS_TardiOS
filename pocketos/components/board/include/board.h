/*
 * board.h — the hardware abstraction layer (the one layer that differs per device).
 *
 * board_init() brings up the compiled-in board's display + input and reports
 * what the device can do via os_caps_t. Everything above this layer is identical
 * across an ILI9341/i80 board, an ST7701/RGB board, and a P4/MIPI board.
 */
#pragma once
#include "esp_err.h"
#include "os_api.h"   /* reuse the capability enums */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    os_caps_t caps;          /* what apps are allowed to know */
    void     *panel;         /* esp_lcd_panel_handle_t — passed to lvgl_port_add_disp */
    void     *io;            /* esp_lcd_panel_io_handle_t */
    void     *touch_indev;   /* LVGL pointer indev (XPT2046), NULL until Phase 1b */
    void     *kbd_indev;     /* LVGL keypad indev (TCA8418), NULL until Phase 1b  */
} board_t;

esp_err_t board_init(board_t *out);

#ifdef __cplusplus
}
#endif
