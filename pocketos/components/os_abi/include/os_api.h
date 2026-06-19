/*
 * os_api.h — the stable application binary interface for TardiOS.
 *
 * This is the ONLY contract between the OS and a loaded app. The loader hands
 * the app a pointer to a populated os_api_t at launch. The app reaches the OS
 * exclusively through this table — never by calling OS functions by name. That
 * is the sandbox seam and the version boundary.
 *
 * ABI rule: this struct is APPEND-ONLY. Never reorder or remove a field. To add
 * capabilities, bump OS_ABI_VERSION and append new function pointers (consuming
 * the _reserved slots). Old apps keep working because their offsets never move.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OS_ABI_VERSION 2u

typedef enum {
    OS_LOG_ERROR = 1,
    OS_LOG_WARN  = 2,
    OS_LOG_INFO  = 3,
    OS_LOG_DEBUG = 4,
} os_log_level_t;

typedef enum {
    OS_TOUCH_NONE       = 0,
    OS_TOUCH_RESISTIVE  = 1,   /* e.g. XPT2046 */
    OS_TOUCH_CAPACITIVE = 2,   /* e.g. GT911   */
} os_touch_type_t;

typedef enum {
    OS_PANEL_RGB      = 0,   /* dumb panel, no GRAM, continuous PSRAM scanout (ST7701) */
    OS_PANEL_I80      = 1,   /* 8080 parallel, has GRAM, dirty-region updates (ILI9341)  */
    OS_PANEL_MIPI_DSI = 2,   /* P4 DSI                                                   */
    OS_PANEL_SPI      = 3,   /* SPI with GRAM, dirty-region updates (ST7789, ILI9341-SPI) */
} os_panel_class_t;

/* LVGL alignment constants — values match LV_ALIGN_* in LVGL 8.x/9.x exactly.
 * Do NOT renumber; the cast in the OS implementation depends on this. */
typedef enum {
    OS_ALIGN_DEFAULT        = 0,
    OS_ALIGN_TOP_LEFT       = 1,
    OS_ALIGN_TOP_MID        = 2,
    OS_ALIGN_TOP_RIGHT      = 3,
    OS_ALIGN_BOTTOM_LEFT    = 4,
    OS_ALIGN_BOTTOM_MID     = 5,
    OS_ALIGN_BOTTOM_RIGHT   = 6,
    OS_ALIGN_LEFT_MID       = 7,
    OS_ALIGN_RIGHT_MID      = 8,
    OS_ALIGN_CENTER         = 9,
} os_align_t;

/* What an app is allowed to know about the device it is running on. */
typedef struct {
    uint16_t         screen_w;
    uint16_t         screen_h;
    os_panel_class_t panel_class;
    os_touch_type_t  touch;
    uint8_t          has_keyboard;
    uint8_t          has_sd_card;   /* 1 if SD card detected at boot */
    char             board_name[24];
} os_caps_t;

typedef struct os_api {
    uint32_t abi_version;

    /* ---- system ---- */
    void     (*log)(os_log_level_t level, const char *tag, const char *msg);
    uint32_t (*millis)(void);
    void     (*get_caps)(os_caps_t *out);
    void     (*sleep_ms)(uint32_t ms);
    void     (*app_exit)(int code);

    /* ---- jailed storage ---- */
    int      (*fs_open)(const char *rel_path, const char *mode);
    int      (*fs_read)(int handle, void *buf, size_t len);
    int      (*fs_write)(int handle, const void *buf, size_t len);
    void     (*fs_close)(int handle);

    /* ---- gui (ABI v1) ---- */
    void    *gui_screen;   /* active lv_obj_t* — cast and use directly if you link LVGL */

    /* ---- gui helpers (ABI v2) — no LVGL headers needed ---- */
    /* Acquire/release the LVGL mutex. Always bracket draw calls with these. */
    void     (*gui_lock)(void);
    void     (*gui_unlock)(void);
    /* Create a text label on parent (pass gui_screen for the active screen).
     * Returns opaque lv_obj_t* handle, or NULL on failure.                  */
    void    *(*gui_label_create)(void *parent, const char *text,
                                 os_align_t align, int x_ofs, int y_ofs);
    /* Update label text in place. */
    void     (*gui_label_set_text)(void *label, const char *text);
    /* Delete any LVGL object created by the app (clean up on exit). */
    void     (*gui_obj_del)(void *obj);
    /* Remove all OS-owned widgets from the screen so the app starts clean. */
    void     (*gui_clean_screen)(void);
} os_api_t;

#ifdef __cplusplus
}
#endif
