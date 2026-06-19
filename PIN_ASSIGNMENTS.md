# Pin Assignments — TardiOS Phase 1

Target chip: **ESP32-S3** (8 MB flash)
Board: **Waveshare ESP32-S3-Touch-LCD-2.8**
Pin defines live in: `pocketos/components/board/board.c`

---

## ST7789 — SPI Display (240 × 320 IPS)

| GPIO | Signal | `#define`      |
|------|--------|----------------|
| 1    | SCLK   | `PIN_LCD_SCLK` |
| 2    | MOSI   | `PIN_LCD_MOSI` |
| 42   | MISO   | `PIN_LCD_MISO` |
| 41   | DC     | `PIN_LCD_DC`   |
| 39   | CS     | `PIN_LCD_CS`   |
| 40   | RST    | `PIN_LCD_RST`  |
| 6    | BL     | `PIN_LCD_BL`   |

SPI host: `SPI2_HOST` @ 40 MHz.

---

## CST816S — Capacitive Touch (I2C) — Phase 1b

| GPIO | Signal | `#define`       |
|------|--------|-----------------|
| 15   | SDA    | `PIN_TOUCH_SDA` |
| 7    | SCL    | `PIN_TOUCH_SCL` |
| 17   | INT    | `PIN_TOUCH_INT` |
| 16   | RST    | `PIN_TOUCH_RST` |

I2C address: `0x1A`.

---

## SD Card — Adafruit MicroSD Breakout (SPI) — Phase 2

Pins TBD. Will share or use a separate SPI bus from the display.
The OS will probe at boot and report `sd=present` / `sd=not found` in the boot log.

---

## Reserved / Avoid

| GPIO  | Reason |
|-------|--------|
| 0     | Strapping — boot mode |
| 19–20 | USB D−/D+ (ESP32-S3 native USB) |
| 26–32 | SPI flash (internal, do not use) |
| 33–37 | PSRAM (internal on -R2/-R8 modules, do not use) |
| 46    | Strapping — ROM message printing |
