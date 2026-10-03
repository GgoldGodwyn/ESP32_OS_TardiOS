/*
 * calculator — a 4-function calculator app for TardiOS.
 *
 * Demonstrates the ABI v3 button + click-event additions to os_api_t.
 * The OS passes an os_api_t* as argv[0]; the app reaches the OS only
 * through that table.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "os_api.h"

#define BTN_W    50
#define BTN_H    45
#define GAP      5
#define GRID_X0  10
#define GRID_Y0  70
#define COL_X(c) (GRID_X0 + (c) * (BTN_W + GAP))
#define ROW_Y(r) (GRID_Y0 + (r) * (BTN_H + GAP))

static const os_api_t *s_os;
static void            *s_display;
static volatile bool    s_running = true;

static double s_accum;
static char   s_pending_op;        /* 0 = none */
static char   s_entry[24] = "0";
static bool   s_start_new_entry = true;

static void update_display(void)
{
    s_os->gui_label_set_text(s_display, s_entry);
}

static double apply_op(double a, double b, char op)
{
    switch (op) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': return (b != 0.0) ? a / b : 0.0;   /* avoid crashing on div-by-0 */
        default:  return b;
    }
}

static void on_digit(char d)
{
    if (s_start_new_entry) {
        s_entry[0] = '\0';
        s_start_new_entry = false;
    }
    size_t len = strlen(s_entry);
    if (len == 1 && s_entry[0] == '0') {
        s_entry[0] = d;
        s_entry[1] = '\0';
    } else if (len < sizeof(s_entry) - 1) {
        s_entry[len] = d;
        s_entry[len + 1] = '\0';
    }
    update_display();
}

static void on_dot(void)
{
    if (s_start_new_entry) {
        strcpy(s_entry, "0");
        s_start_new_entry = false;
    }
    if (!strchr(s_entry, '.')) {
        size_t len = strlen(s_entry);
        if (len < sizeof(s_entry) - 2) {
            s_entry[len] = '.';
            s_entry[len + 1] = '\0';
        }
    }
    update_display();
}

static void on_clear(void)
{
    s_accum = 0.0;
    s_pending_op = 0;
    strcpy(s_entry, "0");
    s_start_new_entry = true;
    update_display();
}

static void on_op(char op)
{
    double v = strtod(s_entry, NULL);   /* atof isn't in elf_loader's symbol table; strtod is */
    s_accum = s_pending_op ? apply_op(s_accum, v, s_pending_op) : v;
    s_pending_op = (op == '=') ? 0 : op;

    snprintf(s_entry, sizeof(s_entry), "%g", s_accum);
    s_start_new_entry = true;
    update_display();
}

/* Runs on the OS's own GUI task (see os_api.h's gui_on_click doc) — stays
 * fast/synchronous, no sleep_ms, safe to touch gui_* from here. */
static void on_key(void *user_data)
{
    char key = (char)(intptr_t)user_data;
    if (key >= '0' && key <= '9') {
        on_digit(key);
    } else if (key == '.') {
        on_dot();
    } else if (key == 'C') {
        on_clear();
    } else {
        on_op(key);
    }
}

static void on_exit_tap(void *user_data)
{
    (void)user_data;
    s_running = false;
}

static void make_key(const char *text, int col, int row, int w, char key)
{
    void *btn = s_os->gui_button_create(s_os->gui_screen, text, OS_ALIGN_TOP_LEFT,
                                        COL_X(col), ROW_Y(row), w, BTN_H);
    s_os->gui_on_click(btn, on_key, (void *)(intptr_t)key);
}

int main(int argc, char **argv)
{
    if (argc < 1 || !argv[0]) {
        return -1;
    }
    s_os = (const os_api_t *)(void *)argv[0];

    if (s_os->abi_version < OS_ABI_VERSION) {
        s_os->log(OS_LOG_ERROR, "calculator", "OS too old — need ABI v3");
        return -2;
    }

    s_os->gui_lock();
    s_display = s_os->gui_label_create(s_os->gui_screen, s_entry,
                                       OS_ALIGN_TOP_LEFT, GRID_X0, 20);
    s_os->gui_unlock();

    void *exit_btn = s_os->gui_button_create(s_os->gui_screen, "Exit",
                                             OS_ALIGN_TOP_RIGHT, -10, 10, 60, 30);
    s_os->gui_on_click(exit_btn, on_exit_tap, NULL);

    make_key("7", 0, 0, BTN_W, '7');
    make_key("8", 1, 0, BTN_W, '8');
    make_key("9", 2, 0, BTN_W, '9');
    make_key("/", 3, 0, BTN_W, '/');

    make_key("4", 0, 1, BTN_W, '4');
    make_key("5", 1, 1, BTN_W, '5');
    make_key("6", 2, 1, BTN_W, '6');
    make_key("*", 3, 1, BTN_W, '*');

    make_key("1", 0, 2, BTN_W, '1');
    make_key("2", 1, 2, BTN_W, '2');
    make_key("3", 2, 2, BTN_W, '3');
    make_key("-", 3, 2, BTN_W, '-');

    make_key("C", 0, 3, BTN_W, 'C');
    make_key("0", 1, 3, BTN_W, '0');
    make_key(".", 2, 3, BTN_W, '.');
    make_key("+", 3, 3, BTN_W, '+');

    make_key("=", 0, 4, 4 * BTN_W + 3 * GAP, '=');

    s_os->log(OS_LOG_INFO, "calculator", "ready");

    /* Stay resident and responsive to taps until "Exit" is pressed — button
     * clicks are handled entirely via callbacks on the OS's GUI task, so
     * this loop only needs to poll the exit flag. */
    while (s_running) {
        s_os->sleep_ms(50);
    }

    s_os->log(OS_LOG_INFO, "calculator", "exiting");
    s_os->app_exit(0);
    return 0;
}
