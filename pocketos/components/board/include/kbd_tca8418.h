/*
 * kbd_tca8418.h — plain-C wrapper around the C++-only atanisoft/esp_tca8418
 * driver, so the rest of TardiOS (board.c, main.c) can stay C.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up the TCA8418 over I2C. int_pin may be -1 if not wired.
 * rows/cols must match the physical matrix (4x10 on tardi boards). */
bool kbd_tca8418_init(int scl_pin, int sda_pin, int int_pin, size_t rows, size_t cols);

/* Number of pending key events in the TCA8418's internal FIFO. */
size_t kbd_tca8418_event_count(void);

/* Pop one raw key event byte (TCA8418 KEY_EVENT_A register format):
 *   bit7    = 1 pressed / 0 released
 *   bits0-6 = key number, 1..(rows*cols), where
 *             key_number = row*10 + col + 1  (row/col are 0-based)
 * Returns -1 if no event is pending.
 *
 * No physical keymap exists yet, so this is as far as the driver goes —
 * translating key_number to an actual character/LV_KEY code and wiring an
 * LVGL keypad indev is follow-up work once the keycap layout is known. */
int kbd_tca8418_get_key(void);

#ifdef __cplusplus
}
#endif
