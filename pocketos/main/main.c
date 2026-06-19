/*
 * pocketos — boot entry.
 *
 * Boot path: NVS → LittleFS → board_init → LVGL → splash → app launcher.
 * Launcher scans /storage/apps/ for app.elf files, shows tappable buttons,
 * launches selected app, then returns to launcher when app exits.
 */
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_littlefs.h"
#include "esp_lvgl_port.h"

#include "board.h"
#include "app_loader.h"
#include "os_api.h"

static const char *TAG = "tardi";

/* ------------------------------------------------------------------ */
static board_t   s_board;
static os_api_t  s_os_api;
static lv_obj_t *s_screen;
static lv_obj_t *s_splash_title;
static lv_obj_t *s_splash_sub;

/* ------------------------------------------------------------------ *
 * App catalogue — populated by scan_apps() each time we enter the
 * launcher so newly copied apps appear without a reboot.
 * ------------------------------------------------------------------ */
#define MAX_APPS 8
typedef struct { char name[32]; char path[80]; } app_entry_t;
static app_entry_t       s_apps[MAX_APPS];
static int               s_app_count   = 0;
static volatile int      s_selected_idx = -1;
static SemaphoreHandle_t s_app_done;
static SemaphoreHandle_t s_app_selected;

/* ------------------------------------------------------------------ *
 * os_api: system
 * ------------------------------------------------------------------ */

static void api_log(os_log_level_t lvl, const char *tag, const char *msg)
{
    esp_log_level_t e = (lvl == OS_LOG_ERROR) ? ESP_LOG_ERROR
                      : (lvl == OS_LOG_WARN)  ? ESP_LOG_WARN
                      : (lvl == OS_LOG_DEBUG) ? ESP_LOG_DEBUG
                                              : ESP_LOG_INFO;
    ESP_LOG_LEVEL(e, tag, "%s", msg);
}

static uint32_t api_millis(void)           { return (uint32_t)(esp_timer_get_time() / 1000ULL); }
static void     api_get_caps(os_caps_t *o) { if (o) *o = s_board.caps; }
static void     api_sleep_ms(uint32_t ms)  { vTaskDelay(pdMS_TO_TICKS(ms)); }

static void api_app_exit(int code)
{
    ESP_LOGI(TAG, "app exit(%d)", code);
    xSemaphoreGive(s_app_done);
}

/* ------------------------------------------------------------------ *
 * os_api: storage (stubs — Phase 2)
 * ------------------------------------------------------------------ */

static int  api_fs_open(const char *p, const char *m)      { (void)p; (void)m; return -1; }
static int  api_fs_read(int h, void *b, size_t n)          { (void)h; (void)b; (void)n; return -1; }
static int  api_fs_write(int h, const void *b, size_t n)   { (void)h; (void)b; (void)n; return -1; }
static void api_fs_close(int h)                            { (void)h; }

/* ------------------------------------------------------------------ *
 * os_api: gui
 * ------------------------------------------------------------------ */

static void api_gui_lock(void)   { lvgl_port_lock(0); }
static void api_gui_unlock(void) { lvgl_port_unlock(); }

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

static void api_gui_clean_screen(void)
{
    lvgl_port_lock(0);
    if (s_splash_title) { lv_obj_del(s_splash_title); s_splash_title = NULL; }
    if (s_splash_sub)   { lv_obj_del(s_splash_sub);   s_splash_sub   = NULL; }
    lvgl_port_unlock();
}

/* ------------------------------------------------------------------ *
 * Build API table
 * ------------------------------------------------------------------ */

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
    s_os_api.gui_clean_screen   = api_gui_clean_screen;
}

/* ------------------------------------------------------------------ *
 * Touch input callback (LVGL 9)
 * ------------------------------------------------------------------ */

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    int  x = 0, y = 0;
    bool pressed = false;
    board_touch_read(&s_board, &x, &y, &pressed);
    if (pressed) {
        data->point.x = (lv_coord_t)x;
        data->point.y = (lv_coord_t)y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ------------------------------------------------------------------ *
 * LVGL init (splash + display)
 * ------------------------------------------------------------------ */

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
        .rotation      = { .mirror_x = false, .mirror_y = false, .swap_xy = false },
        .flags         = { .buff_dma = true, .swap_bytes = true },
    };
    lvgl_port_add_disp(&disp_cfg);

    lvgl_port_lock(0);
    s_screen = lv_scr_act();
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, LV_PART_MAIN);

    /* Splash — stays until scan_apps() + show_launcher() replace it */
    s_splash_title = lv_label_create(s_screen);
    lv_label_set_text(s_splash_title, "TardiOS");
    lv_obj_set_style_text_color(s_splash_title, lv_color_hex(0x1a1a2e), LV_PART_MAIN);
    lv_obj_align(s_splash_title, LV_ALIGN_CENTER, 0, -20);

    s_splash_sub = lv_label_create(s_screen);
    lv_label_set_text(s_splash_sub, "loading...");
    lv_obj_set_style_text_color(s_splash_sub, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_align(s_splash_sub, LV_ALIGN_CENTER, 0, 10);
    lvgl_port_unlock();

    /* Touch input device */
    if (s_board.caps.touch != OS_TOUCH_NONE) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
        s_board.touch_indev = indev;
        ESP_LOGI(TAG, "touch indev registered");
    }

    ESP_LOGI(TAG, "LVGL up");
}

/* ------------------------------------------------------------------ *
 * Storage
 * ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ *
 * App launcher
 * ------------------------------------------------------------------ */

static void scan_apps(void)
{
    s_app_count = 0;
    DIR *dir = opendir("/storage/apps");
    if (!dir) {
        ESP_LOGW(TAG, "cannot open /storage/apps");
        return;
    }
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && s_app_count < MAX_APPS) {
        if (ent->d_name[0] == '.') continue;
        char path[80];
        snprintf(path, sizeof(path), "/storage/apps/%s/app.elf", ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        strncpy(s_apps[s_app_count].name, ent->d_name, sizeof(s_apps[0].name) - 1);
        strncpy(s_apps[s_app_count].path, path, sizeof(s_apps[0].path) - 1);
        s_app_count++;
    }
    closedir(dir);
    ESP_LOGI(TAG, "found %d app(s)", s_app_count);
}

static void launcher_btn_cb(lv_event_t *e)
{
    s_selected_idx = (int)(intptr_t)lv_event_get_user_data(e);
    xSemaphoreGive(s_app_selected);
}

static void show_launcher(void)
{
    lvgl_port_lock(0);
    lv_obj_clean(s_screen);  /* remove splash + any leftover app widgets */
    s_splash_title = NULL;
    s_splash_sub   = NULL;

    lv_obj_t *hdr = lv_label_create(s_screen);
    lv_label_set_text(hdr, "TardiOS");
    lv_obj_set_style_text_color(hdr, lv_color_hex(0x1a1a2e), 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *sub = lv_label_create(s_screen);
    lv_label_set_text(sub, "Select an app");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x888888), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 44);

    if (s_app_count == 0) {
        lv_obj_t *lbl = lv_label_create(s_screen);
        lv_label_set_text(lbl, "No apps found\nin /storage/apps/");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xaaaaaa), 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 10);
    } else {
        for (int i = 0; i < s_app_count; i++) {
            lv_obj_t *btn = lv_button_create(s_screen);
            lv_obj_set_size(btn, 200, 48);
            lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 76 + i * 58);
            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_text(lbl, s_apps[i].name);
            lv_obj_center(lbl);
            lv_obj_add_event_cb(btn, launcher_btn_cb, LV_EVENT_CLICKED,
                                (void *)(intptr_t)i);
        }
    }
    lvgl_port_unlock();
}

static void show_loading(const char *name)
{
    lvgl_port_lock(0);
    lv_obj_clean(s_screen);
    s_splash_title = lv_label_create(s_screen);
    lv_label_set_text(s_splash_title, name);
    lv_obj_set_style_text_color(s_splash_title, lv_color_hex(0x1a1a2e), 0);
    lv_obj_align(s_splash_title, LV_ALIGN_CENTER, 0, -20);
    s_splash_sub = lv_label_create(s_screen);
    lv_label_set_text(s_splash_sub, "loading...");
    lv_obj_set_style_text_color(s_splash_sub, lv_color_hex(0x888888), 0);
    lv_obj_align(s_splash_sub, LV_ALIGN_CENTER, 0, 10);
    lvgl_port_unlock();
}

/* ------------------------------------------------------------------ *
 * App task
 * ------------------------------------------------------------------ */

static void app_task(void *arg)
{
    const char *path = (const char *)arg;
    int code = 0;
    esp_err_t err = app_loader_run(path, &s_os_api, &code);
    ESP_LOGI(TAG, "app_loader_run(%s) -> %s exit=%d", path, esp_err_to_name(err), code);
    xSemaphoreGive(s_app_done);  /* signal even if app_exit wasn't called */
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ *
 * Boot
 * ------------------------------------------------------------------ */

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(mount_storage());
    ESP_ERROR_CHECK(board_init(&s_board));
    lvgl_init();
    build_os_api();

    ESP_LOGI(TAG, "boot: %s %ux%u touch=%d kbd=%d sd=%s",
             s_board.caps.board_name,
             s_board.caps.screen_w, s_board.caps.screen_h,
             s_board.caps.touch, s_board.caps.has_keyboard,
             s_board.caps.has_sd_card ? "present" : "not found");

    s_app_done     = xSemaphoreCreateBinary();
    s_app_selected = xSemaphoreCreateBinary();

    /* Launcher loop — runs forever; each iteration is one app session */
    while (1) {
        scan_apps();
        show_launcher();

        /* Block until user taps an app button */
        xSemaphoreTake(s_app_selected, portMAX_DELAY);

        int idx = s_selected_idx;
        show_loading(s_apps[idx].name);

        /* Launch app; it calls gui_clean_screen() before drawing */
        xTaskCreate(app_task, "app", 8192, (void *)s_apps[idx].path, 5, NULL);

        /* Block until app exits or crashes */
        xSemaphoreTake(s_app_done, portMAX_DELAY);

        /* Brief pause, then back to launcher */
        vTaskDelay(pdMS_TO_TICKS(400));
    }
}
