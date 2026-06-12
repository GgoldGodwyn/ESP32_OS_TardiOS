/*
 * pocketos — boot entry.
 *
 * Boot path: NVS → LittleFS → board_init → LVGL → os_api → launch ELF app.
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_littlefs.h"
#include "esp_lvgl_port.h"

#include "board.h"
#include "app_loader.h"
#include "os_api.h"

static const char *TAG = "tardi";

static board_t   s_board;
static os_api_t  s_os_api;
static lv_obj_t *s_screen;

/* ---------------- os_api: system ---------------------------------- */

static void api_log(os_log_level_t lvl, const char *tag, const char *msg)
{
    esp_log_level_t e = (lvl == OS_LOG_ERROR) ? ESP_LOG_ERROR
                      : (lvl == OS_LOG_WARN)  ? ESP_LOG_WARN
                      : (lvl == OS_LOG_DEBUG) ? ESP_LOG_DEBUG
                                              : ESP_LOG_INFO;
    ESP_LOG_LEVEL(e, tag, "%s", msg);
}

static uint32_t api_millis(void)            { return (uint32_t)(esp_timer_get_time() / 1000ULL); }
static void     api_get_caps(os_caps_t *o)  { if (o) *o = s_board.caps; }
static void     api_sleep_ms(uint32_t ms)   { vTaskDelay(pdMS_TO_TICKS(ms)); }
static void     api_app_exit(int code)      { ESP_LOGI(TAG, "app exit(%d)", code); }

/* ---------------- os_api: storage (Phase 1 stubs) ----------------- */

static int  api_fs_open(const char *p, const char *m) { (void)p; (void)m; return -1; }
static int  api_fs_read(int h, void *b, size_t n)     { (void)h; (void)b; (void)n; return -1; }
static int  api_fs_write(int h, const void *b, size_t n){ (void)h; (void)b; (void)n; return -1; }
static void api_fs_close(int h)                        { (void)h; }

/* ---------------- os_api: gui ------------------------------------- */

static void api_gui_lock(void)   { lvgl_port_lock(0); }
static void api_gui_unlock(void) { lvgl_port_unlock(); }

/* os_align_t values are defined to match LV_ALIGN_* numerically
 * (DEFAULT=0, TOP_LEFT=1 … BOTTOM_RIGHT=9) so a direct cast is safe. */
static void *api_gui_label_create(void *parent, const char *text,
                                  os_align_t align, int x_ofs, int y_ofs)
{
    lvgl_port_lock(0);
    lv_obj_t *lbl = lv_label_create((lv_obj_t *)parent);
    lv_label_set_text(lbl, text);
    if (align != OS_ALIGN_DEFAULT)
        lv_obj_align(lbl, (lv_align_t)align, x_ofs, y_ofs);
    lvgl_port_unlock();
    return lbl;
}

static void api_gui_label_set_text(void *label, const char *text)
{
    lvgl_port_lock(0);
    lv_label_set_text((lv_obj_t *)label, text);
    lvgl_port_unlock();
}

static void api_gui_obj_del(void *obj)
{
    lvgl_port_lock(0);
    lv_obj_del((lv_obj_t *)obj);
    lvgl_port_unlock();
}

/* ---------------- build api table --------------------------------- */

static void build_os_api(void)
{
    memset(&s_os_api, 0, sizeof(s_os_api));
    s_os_api.abi_version        = OS_ABI_VERSION;
    s_os_api.log                = api_log;
    s_os_api.millis             = api_millis;
    s_os_api.get_caps           = api_get_caps;
    s_os_api.sleep_ms           = api_sleep_ms;
    s_os_api.app_exit           = api_app_exit;
    s_os_api.fs_open            = api_fs_open;
    s_os_api.fs_read            = api_fs_read;
    s_os_api.fs_write           = api_fs_write;
    s_os_api.fs_close           = api_fs_close;
    s_os_api.gui_screen         = s_screen;
    s_os_api.gui_lock           = api_gui_lock;
    s_os_api.gui_unlock         = api_gui_unlock;
    s_os_api.gui_label_create   = api_gui_label_create;
    s_os_api.gui_label_set_text = api_gui_label_set_text;
    s_os_api.gui_obj_del        = api_gui_obj_del;
}

/* ---------------- LVGL init --------------------------------------- */

static void lvgl_init(void)
{
    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = s_board.io,
        .panel_handle  = s_board.panel,
        .buffer_size   = s_board.caps.screen_w * 40,
        .double_buffer = false,
        .hres          = s_board.caps.screen_w,
        .vres          = s_board.caps.screen_h,
        .monochrome    = false,
        .rotation      = { .mirror_x = true, .mirror_y = false, .swap_xy = false },
        .flags         = { .buff_dma = true },
    };
    lvgl_port_add_disp(&disp_cfg);

    lvgl_port_lock(0);
    s_screen = lv_scr_act();
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x1a1a2e), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "TardiOS");
    lv_obj_set_style_text_color(title, lv_color_hex(0xe0e0e0), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *sub = lv_label_create(s_screen);
    lv_label_set_text(sub, "loading...");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x666666), LV_PART_MAIN);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 10);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "LVGL up");
}

/* ---------------- boot -------------------------------------------- */

static esp_err_t mount_storage(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path              = "/storage",
        .partition_label        = "storage",
        .format_if_mount_failed = true,
        .dont_mount             = false,
    };
    return esp_vfs_littlefs_register(&conf);
}

static void app_task(void *arg)
{
    const char *path = (const char *)arg;
    int code = 0;
    esp_err_t err = app_loader_run(path, &s_os_api, &code);
    ESP_LOGI(TAG, "app_loader_run(%s) -> %s exit=%d", path, esp_err_to_name(err), code);
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(mount_storage());
    ESP_ERROR_CHECK(board_init(&s_board));
    lvgl_init();
    build_os_api();

    ESP_LOGI(TAG, "boot: %s %ux%u touch=%d kbd=%d",
             s_board.caps.board_name,
             s_board.caps.screen_w, s_board.caps.screen_h,
             s_board.caps.touch, s_board.caps.has_keyboard);

    static const char *hello = "/storage/apps/hello/app.elf";
    xTaskCreate(app_task, "app", 8192, (void *)hello, 5, NULL);
}
