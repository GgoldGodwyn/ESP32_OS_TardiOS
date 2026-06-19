/*
 * hello — first TardiOS app. Draws a label on screen via the OS GUI API.
 *
 * The OS passes an os_api_t* as argv[0]. Everything the app does to the
 * OS — including drawing — goes through that table.
 */
#include <stdio.h>
#include "os_api.h"

int main(int argc, char **argv)
{
    if (argc < 1 || !argv[0]) return -1;
    const os_api_t *os = (const os_api_t *)(void *)argv[0];

    if (os->abi_version < OS_ABI_VERSION) {
        os->log(OS_LOG_ERROR, "hello", "OS too old — need ABI v2");
        return -2;
    }

    os_caps_t caps;
    os->get_caps(&caps);

    /* Clear the OS splash screen before drawing app content */
    os->gui_clean_screen();

    /* Title label */
    void *title = os->gui_label_create(
        os->gui_screen,
        "Hello, TardiOS!",
        OS_ALIGN_CENTER, 0, -30
    );
    (void)title;

    /* Info label showing what the OS reported */
    char info[64];
    snprintf(info, sizeof(info), "%s  %ux%u  kbd=%s",
             caps.board_name, caps.screen_w, caps.screen_h,
             caps.has_keyboard ? "yes" : "no");
    os->gui_label_create(os->gui_screen, info, OS_ALIGN_CENTER, 0, 10);

    /* Uptime counter — update every second for 10 seconds */
    void *ticker = os->gui_label_create(
        os->gui_screen, "t=0s", OS_ALIGN_BOTTOM_MID, 0, -10
    );

    for (int i = 1; i <= 10; i++) {
        os->sleep_ms(1000);
        char buf[16];
        snprintf(buf, sizeof(buf), "t=%ds", i);
        os->gui_label_set_text(ticker, buf);
    }

    os->log(OS_LOG_INFO, "hello", "done");
    os->app_exit(0);
    return 0;
}
