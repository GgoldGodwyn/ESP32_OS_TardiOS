#include "kbd_tca8418.h"
#include "esp_tca8418.hxx"
#include "driver/gpio.h"

static TCA8418 *s_kbd = nullptr;

extern "C" bool kbd_tca8418_init(int scl_pin, int sda_pin, int int_pin, size_t rows, size_t cols)
{
    static TCA8418 kbd((gpio_num_t)scl_pin, (gpio_num_t)sda_pin,
                        int_pin >= 0 ? (gpio_num_t)int_pin : GPIO_NUM_NC);
    if (!kbd.hw_init(rows, cols)) {
        return false;
    }
    s_kbd = &kbd;
    return true;
}

extern "C" size_t kbd_tca8418_event_count(void)
{
    return s_kbd ? s_kbd->get_event_count() : 0;
}

extern "C" int kbd_tca8418_get_key(void)
{
    if (!s_kbd || s_kbd->get_event_count() == 0) {
        return -1;
    }
    return (int)s_kbd->get_key();
}
