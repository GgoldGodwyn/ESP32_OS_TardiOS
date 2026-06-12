# Pin Assignments — TardiOS Phase 1

Target chip: **ESP32-S3**  
Board name: `tardi-s3`  
Pin defines live in: `tardi/components/board/board.c`

---

## ILI9341 — 8080 Parallel (i80) Display

| GPIO | Signal | `#define` |
|------|--------|-----------|
| 4    | D0     | `PIN_LCD_D0` |
| 5    | D1     | `PIN_LCD_D1` |
| 6    | D2     | `PIN_LCD_D2` |
| 7    | D3     | `PIN_LCD_D3` |
| 15   | D4     | `PIN_LCD_D4` |
| 16   | D5     | `PIN_LCD_D5` |
| 17   | D6     | `PIN_LCD_D6` |
| 18   | D7     | `PIN_LCD_D7` |
| 8    | WR     | `PIN_LCD_WR` |
| 9    | DC     | `PIN_LCD_DC` |
| 10   | CS     | `PIN_LCD_CS` |
| 11   | RST    | `PIN_LCD_RST` |
| 12   | BL     | `PIN_LCD_BL` |

Resolution: 240 × 320. RD line not needed (write-only mode).

---

## XPT2046 — Resistive Touch (dedicated SPI2)

| GPIO | Signal | `#define` |
|------|--------|-----------|
| 40   | CS     | `PIN_TOUCH_CS` |
| 41   | CLK    | `PIN_TOUCH_CLK` |
| 42   | MOSI   | `PIN_TOUCH_MOSI` |
| 3    | MISO   | `PIN_TOUCH_MISO` |
| 39   | IRQ    | `PIN_TOUCH_IRQ` |

Separate SPI bus from the display (parallel display has no MISO).

---

## TCA8418 — QWERTY Keyboard Encoder (I2C)

| GPIO | Signal | `#define` |
|------|--------|-----------|
| 46   | SCL    | `PIN_KBD_SCL` |
| 48   | SDA    | `PIN_KBD_SDA` |
| TBD  | INT    | `PIN_KBD_INT` |

TCA8418 handles the key matrix (rows/cols) internally; the ESP32-S3 only needs I2C + INT.

---

## Reserved / Avoid

| GPIO  | Reason |
|-------|--------|
| 0     | Strapping — boot mode |
| 19–20 | USB D−/D+ (ESP32-S3 native USB) |
| 26–32 | SPI flash (internal, do not use) |
| 33–37 | PSRAM (internal on -R2 / -R8 modules, do not use) |
| 45    | Strapping — VDD_SPI voltage |
| 46    | Strapping — ROM message printing *(also used for KBD SCL — OK after boot)* |

> **Chip target change:** the build directory and `sdkconfig` must be deleted and the project re-targeted with `idf.py set-target esp32s3` before the first S3 build.
