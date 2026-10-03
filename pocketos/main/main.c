/*
 * pocketos — boot entry.
 *
 * Boot path: NVS → LittleFS → board_init → LVGL → os_api → launch ELF app.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "esp_littlefs.h"
#include "esp_lvgl_port.h"
#include "esp_lcd_touch.h"

#include "board.h"
#include "app_loader.h"
#include "os_api.h"

static const char *TAG = "tardi";

static board_t   s_board;
static os_api_t  s_os_api;
static lv_obj_t *s_screen;

/* ---------------- launcher state ----------------------------------- */

static app_entry_t     s_apps[8];
static size_t          s_app_count;
static SemaphoreHandle_t s_launch_sem;
static volatile int    s_selected_idx = -1;

/* Sentinel s_selected_idx value for the "Touch Calibration" launcher entry,
 * distinct from any real index into s_apps[] (which are always >= 0) and
 * from -1 (meaning "nothing selected"). */
#define CALIBRATION_IDX (-2)

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
    /* LVGL's default theme here is the light variant (dark text on light
     * backgrounds) — CONFIG_LV_THEME_DEFAULT_DARK is off — but the OS's own
     * screen background is a dark navy. Apps have no way to know or match
     * that, so give every app-created label a readable color explicitly;
     * otherwise text silently renders dark-on-dark and looks like nothing
     * drew at all. */
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xe0e0e0), LV_PART_MAIN);
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

/* ---- gui: buttons + click events (ABI v3) ------------------------- */

static void *api_gui_button_create(void *parent, const char *text,
                                   os_align_t align, int x_ofs, int y_ofs,
                                   int w, int h)
{
    lvgl_port_lock(0);
    lv_obj_t *btn = lv_button_create((lv_obj_t *)parent);
    lv_obj_set_size(btn, w, h);
    if (align != OS_ALIGN_DEFAULT) {
        lv_obj_align(btn, (lv_align_t)align, x_ofs, y_ofs);
    }
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lvgl_port_unlock();
    return btn;
}

/* Bridges LVGL's one-void*-user-data event API to the app's (cb, user_data)
 * pair. Freed on LV_EVENT_DELETE, which LVGL fires for every descendant when
 * an ancestor (e.g. the whole screen, via lv_obj_clean()) is torn down — so
 * this can't leak across app launches even though apps never call
 * gui_obj_del() on their own buttons. */
typedef struct {
    void (*cb)(void *user_data);
    void *user_data;
    int64_t last_click_us;
} click_trampoline_t;

/* Resistive touch contact can bounce — make/break contact several times
 * within one physical tap — and each bounce is a legitimate press-release
 * cycle from LVGL's point of view, so it fires LV_EVENT_CLICKED multiple
 * times for what was one tap (observed as repeated digits/symbols in the
 * calculator). Debounce per-button here so every app gets this for free. */
#define CLICK_DEBOUNCE_US (250 * 1000)

static void click_trampoline_cb(lv_event_t *e)
{
    click_trampoline_t *t = (click_trampoline_t *)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        int64_t now = esp_timer_get_time();
        if (now - t->last_click_us < CLICK_DEBOUNCE_US) {
            return;
        }
        t->last_click_us = now;
        if (t->cb) {
            t->cb(t->user_data);
        }
    } else if (code == LV_EVENT_DELETE) {
        free(t);
    }
}

static void api_gui_on_click(void *btn, void (*cb)(void *user_data), void *user_data)
{
    click_trampoline_t *t = malloc(sizeof(*t));
    if (!t) {
        return;
    }
    t->cb = cb;
    t->user_data = user_data;
    t->last_click_us = 0;

    lvgl_port_lock(0);
    lv_obj_add_event_cb((lv_obj_t *)btn, click_trampoline_cb, LV_EVENT_CLICKED, t);
    lv_obj_add_event_cb((lv_obj_t *)btn, click_trampoline_cb, LV_EVENT_DELETE, t);
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
    s_os_api.gui_button_create  = api_gui_button_create;
    s_os_api.gui_on_click       = api_gui_on_click;
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
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);

    if (s_board.touch_handle) {
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp   = disp,
            .handle = (esp_lcd_touch_handle_t)s_board.touch_handle,
        };
        s_board.touch_indev = lvgl_port_add_touch(&touch_cfg);
        if (!s_board.touch_indev) {
            ESP_LOGW(TAG, "lvgl_port_add_touch failed");
        }
    }

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

/* ---------------- launcher ------------------------------------------ */

/* Runs on the LVGL port's own task (called from lv_timer_handler() while
 * processing input). Must stay fast and non-blocking — it only records the
 * selection and wakes app_task, which does the actual launching from its
 * own task context. */
static void app_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_selected_idx = idx;
    xSemaphoreGive(s_launch_sem);
}

/* (Re)builds the whole screen from scratch: title + a tappable list of
 * discovered apps. Safe to call after an app has left objects on s_screen
 * behind (it never cleans up after itself) since lv_obj_clean() wipes it
 * first. Called from app_task's context, not the LVGL port task, so it
 * takes the lock itself. */
static void show_launcher(void)
{
    lvgl_port_lock(0);
    lv_obj_clean(s_screen);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "TardiOS By Ggold");
    lv_obj_set_style_text_color(title, lv_color_hex(0xe0e0e0), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    if (s_app_count == 0) {
        lv_obj_t *empty = lv_label_create(s_screen);
        lv_label_set_text(empty, "no apps found");
        lv_obj_set_style_text_color(empty, lv_color_hex(0x666666), LV_PART_MAIN);
        lv_obj_center(empty);
        lvgl_port_unlock();
        return;
    }

    lv_obj_t *list = lv_list_create(s_screen);
    lv_obj_set_size(list, s_board.caps.screen_w - 20, s_board.caps.screen_h - 50);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -10);

    for (size_t i = 0; i < s_app_count; i++) {
        char label[48];
        snprintf(label, sizeof(label), "%s%s", s_apps[i].title,
                 s_apps[i].from_sdcard ? "  [SD]" : "");
        lv_obj_t *btn = lv_list_add_button(list, NULL, label);
        lv_obj_add_event_cb(btn, app_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    if (s_board.touch_indev) {
        lv_obj_t *calib_btn = lv_list_add_button(list, NULL, "Touch Calibration");
        lv_obj_add_event_cb(calib_btn, app_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)CALIBRATION_IDX);
    }
    lvgl_port_unlock();
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

/* TEMPORARY diagnostic — logs touch coordinates as LVGL's own indev layer
 * already sees them (post swap_xy/mirror_x/mirror_y). Reads only
 * lv_indev_get_point()/get_state(), which reflect whatever LVGL's own
 * polling last captured — no direct driver/SPI calls here. An earlier
 * version of this task called esp_lcd_touch_read_data() directly on a
 * timer, racing LVGL's own concurrent polling of the same SPI2 touch
 * device and causing "Cannot acquire bus when a polling transaction is in
 * progress" — that's fixed by going through the indev instead of the
 * driver a second time. Safe to delete once touch is confirmed working. */
static void touch_debug_task(void *arg)
{
    (void)arg;
    if (!s_board.touch_indev) {
        ESP_LOGW(TAG, "touch_debug: no touch_indev, nothing to poll");
        vTaskDelete(NULL);
        return;
    }
    lv_indev_t *indev = (lv_indev_t *)s_board.touch_indev;

    for (;;) {
        if (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            ESP_LOGI(TAG, "touch_debug: x=%d y=%d", (int)p.x, (int)p.y);
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

#define CALIB_MARGIN    25
#define CALIB_POINTS    5

/* Walks the user through tapping 5 known points (corners + center) and logs
 * target vs. measured position for each. Reports coordinates as LVGL's own
 * indev layer reports them — i.e. after swap_xy/mirror_x/mirror_y and the
 * touch driver's own raw-ADC-to-pixel scaling — not the underlying raw ADC
 * counts. Getting true raw ADC would mean either bypassing LVGL's polling
 * (which reintroduces the SPI bus contention bug touch_debug_task used to
 * have) or disabling CONFIG_XPT2046_CONVERT_ADC_TO_COORDS globally (which
 * would break the swap/mirror math elsewhere, since it assumes coordinates
 * already scaled to screen space). The mapped coordinate is what actually
 * matters for tuning: comparing it against the known target directly shows
 * whether the current swap/mirror/scale settings are accurate. */
static void run_touch_calibration(void)
{
    if (!s_board.touch_indev) {
        ESP_LOGW(TAG, "calibration: no touch_indev available");
        return;
    }
    lv_indev_t *indev = (lv_indev_t *)s_board.touch_indev;

    int w = s_board.caps.screen_w;
    int h = s_board.caps.screen_h;
    const lv_point_t targets[CALIB_POINTS] = {
        { CALIB_MARGIN,     CALIB_MARGIN     },
        { w - CALIB_MARGIN, CALIB_MARGIN     },
        { CALIB_MARGIN,     h - CALIB_MARGIN },
        { w - CALIB_MARGIN, h - CALIB_MARGIN },
        { w / 2,            h / 2            },
    };
    static const char *const names[CALIB_POINTS] = {
        "top-left", "top-right", "bottom-left", "bottom-right", "center"
    };

    for (int i = 0; i < CALIB_POINTS; i++) {
        lvgl_port_lock(0);
        lv_obj_clean(s_screen);

        char buf[32];
        snprintf(buf, sizeof(buf), "Tap point %d/%d", i + 1, CALIB_POINTS);
        lv_obj_t *info = lv_label_create(s_screen);
        lv_label_set_text(info, buf);
        lv_obj_set_style_text_color(info, lv_color_hex(0xe0e0e0), LV_PART_MAIN);
        lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 10);

        lv_obj_t *marker = lv_label_create(s_screen);
        lv_label_set_text(marker, "+");
        lv_obj_set_style_text_color(marker, lv_color_hex(0xff5050), LV_PART_MAIN);
        lv_obj_set_pos(marker, targets[i].x - 6, targets[i].y - 8);
        lvgl_port_unlock();

        /* Wait for a fresh press (not-pressed -> pressed edge), so a touch
         * still held from the previous point can't immediately count here. */
        bool was_pressed = (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED);
        lv_point_t measured = { 0, 0 };
        for (;;) {
            bool pressed = (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED);
            if (pressed && !was_pressed) {
                /* Resistive touch contact is noisy right at touch-down —
                 * sampling the very first "pressed" reading catches an
                 * unsettled value. Let it settle, then average a few
                 * samples while still held for a stable result. */
                vTaskDelay(pdMS_TO_TICKS(100));
                int32_t sum_x = 0, sum_y = 0;
                int samples = 0;
                for (int s = 0; s < 4; s++) {
                    if (lv_indev_get_state(indev) != LV_INDEV_STATE_PRESSED) {
                        break;
                    }
                    lv_point_t p;
                    lv_indev_get_point(indev, &p);
                    sum_x += p.x;
                    sum_y += p.y;
                    samples++;
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
                if (samples > 0) {
                    measured.x = sum_x / samples;
                    measured.y = sum_y / samples;
                } else {
                    /* Released before settling — fall back to last point. */
                    lv_indev_get_point(indev, &measured);
                }
                break;
            }
            was_pressed = pressed;
            vTaskDelay(pdMS_TO_TICKS(30));
        }

        ESP_LOGI(TAG, "calib %-12s target=(%3d,%3d) measured=(%3d,%3d) diff=(%4d,%4d)",
                 names[i], (int)targets[i].x, (int)targets[i].y, (int)measured.x, (int)measured.y,
                 (int)measured.x - (int)targets[i].x, (int)measured.y - (int)targets[i].y);

        /* Wait for release before the next point, so this same touch can't
         * also register there. */
        while (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
            vTaskDelay(pdMS_TO_TICKS(30));
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    lvgl_port_lock(0);
    lv_obj_clean(s_screen);
    lv_obj_t *done = lv_label_create(s_screen);
    lv_label_set_text(done, "Calibration done - check serial log");
    lv_obj_set_style_text_color(done, lv_color_hex(0xe0e0e0), LV_PART_MAIN);
    lv_obj_center(done);
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(2000));
}

static void app_task(void *arg)
{
    (void)arg;

    app_loader_discover(s_apps, sizeof(s_apps) / sizeof(s_apps[0]), &s_app_count);
    ESP_LOGI(TAG, "discovered %u app(s):", (unsigned)s_app_count);
    for (size_t i = 0; i < s_app_count; i++) {
        ESP_LOGI(TAG, "  %-16s %s (%s)", s_apps[i].name, s_apps[i].path,
                 s_apps[i].from_sdcard ? "sdcard" : "internal");
    }

    show_launcher();

    for (;;) {
        xSemaphoreTake(s_launch_sem, portMAX_DELAY);
        int idx = s_selected_idx;

        if (idx == CALIBRATION_IDX) {
            run_touch_calibration();
            show_launcher();
            continue;
        }

        if (idx < 0 || (size_t)idx >= s_app_count) {
            continue;
        }

        lvgl_port_lock(0);
        lv_obj_clean(s_screen);   /* hand the screen to the app cleanly */
        lvgl_port_unlock();

        ESP_LOGI(TAG, "heap before launch: internal=%u psram=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

        int code = 0;
        esp_err_t err = app_loader_run(s_apps[idx].path, &s_os_api, &code);
        ESP_LOGI(TAG, "app_loader_run(%s) -> %s exit=%d", s_apps[idx].path,
                 esp_err_to_name(err), code);

        ESP_LOGI(TAG, "heap after launch: internal=%u psram=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

        show_launcher();

        /* Debounce: ignore any selection for a bit after a launch attempt
         * returns. Touch shares SPI2 with the microSD card and spurious
         * low-pressure reads have been observed re-triggering a launch
         * immediately — each failed attempt is near-instant and redraws
         * the launcher, which looks like a device reset from the outside.
         * Drain any stray signal that arrived during the cooldown before
         * going back to a real blocking wait. */
        vTaskDelay(pdMS_TO_TICKS(1500));
        xSemaphoreTake(s_launch_sem, 0);
    }
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

    s_launch_sem = xSemaphoreCreateBinary();
    xTaskCreate(app_task, "app", 8192, NULL, 5, NULL);
    xTaskCreate(touch_debug_task, "touch_debug", 3072, NULL, 3, NULL);
}
