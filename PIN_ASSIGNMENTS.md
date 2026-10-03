# Pin Assignments — TardiOS Phase 1

Target chip: **ESP32-S3**  
Board name: `tardi-s3`  
Pin defines live in: `tardi/components/board/board.c`

---

## ILI9341 — 8080 Parallel (i80) Display

Panel: 3.2" ILI9341 panel pulled from a DevEBox STM32F4VE board. Confirmed
against the DevEBox's own J1 TFT header (2×16) schematic:

| J1 pin | Function | J1 pin | Function |
|--------|----------|--------|----------|
| 1 | GND | 2 | RST |
| 3 | FSMC_D15 (PD10) | 4 | FSMC_D14 (PD9) |
| 5 | FSMC_D13 (PD8) | 6 | FSMC_D12 (PE15) |
| 7 | FSMC_D11 (PE14) | 8 | FSMC_D10 (PE13) |
| 9 | FSMC_D9 (PE12) | 10 | FSMC_D8 (PE11) |
| 11 | FSMC_D7 (PE10) | 12 | FSMC_D6 (PE9) |
| 13 | FSMC_D5 (PE8) | 14 | FSMC_D4 (PE7) |
| 15 | FSMC_D3 (PD1) | 16 | FSMC_D2 (PD0) |
| 17 | FSMC_D1 (PD15) | 18 | FSMC_D0 (PD14) |
| 19 | FSMC_NOE (PD4) | 20 | FSMC_NWE (PD5) |
| 21 | FSMC_A18 (PD13) | 22 | FSMC_NE1 (PD7) |
| 23 | Touch_CLK (PB13) | 24 | Touch_CS (PB12) |
| 25 | Touch_MOSI (PB15) | 26 | Touch_MISO (PB14) |
| 27 | Touch_PEN (PC5) | 28 | LCD_BL (PB1) |
| 29 | VBAT (N.C.) | 30 | GND |
| 31 | +3.3V | 32 | GND |

`FSMC_NOE`/`FSMC_NWE` = RD/WR strobes, `FSMC_A18` = the address line used as
RS/DC, `FSMC_NE1` = chip-select (SCS), `Touch_PEN` = touch IRQ (PENIRQ).

The panel also has a solder-selectable 8-bit/16-bit resistor bridge
(currently set for 16-bit; needs reworking to 8-bit for this board's
8-line wiring). **8-bit mode uses the panel's upper 8 data lines**
(`FSMC_D8`-`FSMC_D15` above, i.e. the panel's own DB8-DB15), not the lower
8 (`FSMC_D0`-`FSMC_D7`/DB0-DB7) — this is ILI9341 chip-level behavior, not
specific to this board, though it hasn't been continuity-checked against
this exact bridge. Leaving the bridge on 16-bit while only driving 8
lines, or wiring the wrong half, corrupts every pixel word — this is what
produced a raster/garbage screen before being diagnosed.

| GPIO | Signal | J1 pin | FSMC name | `#define` |
|------|--------|--------|-----------|-----------|
| 4    | D0     | 10     | FSMC_D8   | `PIN_LCD_D0` |
| 5    | D1     | 9      | FSMC_D9   | `PIN_LCD_D1` |
| 6    | D2     | 8      | FSMC_D10  | `PIN_LCD_D2` |
| 7    | D3     | 7      | FSMC_D11  | `PIN_LCD_D3` |
| 15   | D4     | 6      | FSMC_D12  | `PIN_LCD_D4` |
| 16   | D5     | 5      | FSMC_D13  | `PIN_LCD_D5` |
| 17   | D6     | 4      | FSMC_D14  | `PIN_LCD_D6` |
| 18   | D7     | 3      | FSMC_D15  | `PIN_LCD_D7` |
| 8    | WR     | 20     | FSMC_NWE  | `PIN_LCD_WR` |
| 9    | DC     | 21     | FSMC_A18 (RS) | `PIN_LCD_DC` |
| 10   | CS     | 22     | FSMC_NE1 (SCS) | `PIN_LCD_CS` |
| 11   | RST    | 2      | RST       | `PIN_LCD_RST` |
| 12   | BL     | 28     | LCD_BL    | `PIN_LCD_BL` |

Resolution: 240 × 320. RD (J1 pin 19, `FSMC_NOE`) not wired — write-only mode.

---

## XPT2046 — Resistive Touch (dedicated SPI2)

| GPIO | Signal | J1 pin | Schematic name | `#define` |
|------|--------|--------|-----------------|-----------|
| 47   | CS     | 24     | Touch_CS   | `PIN_TOUCH_CS` |
| 41   | CLK    | 23     | Touch_CLK  | `PIN_TOUCH_CLK` |
| 42   | MOSI   | 25     | Touch_MOSI | `PIN_TOUCH_MOSI` |
| 3    | MISO   | 26     | Touch_MISO | `PIN_TOUCH_MISO` |
| 39   | IRQ    | 27     | Touch_PEN  | `PIN_TOUCH_IRQ` |

CS moved from GPIO40 to GPIO47, testing whether that specific pin was
involved in the "touch needs a power cycle to work" issue.

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




Yes — app-sdk is a standalone ESP-IDF project template. Each app you build is its own separate idf.py project that happens to compile to a relocatable ELF instead of flashable firmware. Here's the workflow:

1. Copy it as a new project

cp -r app-sdk apps-src/myapp   # pick your own location outside the sdk

2. Rename the project

In CMakeLists.txt:
project(hello)       →  project(myapp)
project_elf(hello)   →  project_elf(myapp)
This name determines the output filename (see step 4).

3. Write your app in main/main.c

Everything goes through the os_api_t* you get as argv[0] — never link OS symbols directly. The pattern from hello:
int main(int argc, char **argv) {
    const os_api_t *os = (const os_api_t *)(void *)argv[0];
    if (os->abi_version < OS_ABI_VERSION) { os->log(OS_LOG_ERROR, "myapp", "OS too old"); return -2; }

    os_caps_t caps;
    os->get_caps(&caps);          // screen size, touch/keyboard presence, board name

    os->gui_lock();
    void *lbl = os->gui_label_create(os->gui_screen, "text", OS_ALIGN_CENTER, 0, 0);
    os->gui_unlock();

    os->sleep_ms(1000);
    os->gui_label_set_text(lbl, "updated");
    os->app_exit(0);
}
Current GUI surface is deliberately minimal: labels only (gui_label_create/set_text/gui_obj_del), plus gui_lock/gui_unlock for thread-safety with LVGL's own task. File I/O (fs_open/read/write/close) is declared in the ABI but currently stubbed to always fail on the OS side — don't rely on persistent storage from an app yet.

Don't edit main/os_api.h for new capabilities — that's a vendored copy of tardi/components/os_abi/include/os_api.h (actually pocketos/components/os_abi/include/os_api.h); the two must stay byte-identical. New OS capabilities get added on the OS side first, then both copies updated together.

4. Build

idf.py set-target esp32s3   # or esp32p4
idf.py build
The artifact you want is build/myapp.app.elf — not myapp.elf (that's the ordinary full ESP-IDF binary with debug info) and not myapp.bin. .app.elf is the small, relocation-ready payload the OS's esp_elf_relocate() actually consumes (for reference, hello.app.elf is ~1.5KB vs hello.elf's ~2.7MB).

5. Install (current Phase 1 sideload convention)

Copy it into the OS's LittleFS staging directory:
cp build/myapp.app.elf ../pocketos/data/apps/myapp/app.elf
matching the /storage/apps/<name>/app.elf layout main.c expects (this is the convention we just fixed for hello).


The destination directory doesn't exist yet — cp won't create intermediate directories for you. Run:

mkdir -p ../pocketos/data/apps/myapp
cp build/calculator.app.elf ../../pocketos/data/apps/myapp/app.elf

One more thing you'll need after that: pocketos/main/main.c still hardcodes the boot path to /storage/apps/hello/app.elf (line 180). For this new app to actually launch, either:
- change that string to /storage/apps/myapp/app.elf, or
- name your destination folder hello instead of myapp to match the existing path

Then rebuild pocketos (idf.py build) so storage.bin picks up the new file.


Current limitation

pocketos/main/main.c only ever launches one hardcoded path (/storage/apps/hello/app.elf) at boot — there's no launcher/menu yet to pick between multiple installed apps. So today, "using the SDK" for a second app means building it, but you'd need to either swap the hardcoded path in main.c or wait for the Phase 2 launcher the README mentions to actually run it on-device.



Got it — that's the Phase 2 vision the README already flags ("launcher + package manager"), and it's a real architectural shift from what's built today. Here's the honest gap list, in the order you'd hit them:

1. Input — currently doesn't exist at all. I checked: board_t has touch_indev/kbd_indev fields, but board_init() never sets them (they stay NULL). There's no XPT2046 or TCA8418 driver code anywhere in the repo yet. Without touch or keyboard wired to an LVGL indev, nothing on screen is selectable — this is the actual blocker before a launcher UI means anything.

2. App discovery. Right now nothing scans /storage/apps/. You'd add a directory listing step (opendir/readdir over LittleFS) in main.c that walks /storage/apps/*/ and collects whatever's found — instead of the one hardcoded string.

3. App metadata. A bare app.elf has no name/icon/label to show on a home screen. You'd need a small manifest convention per app — e.g. /storage/apps/<name>/manifest.json (or even just a name.txt) alongside app.elf — so the launcher has something to render besides a raw folder name.

4. The launcher UI itself. This is not built through os_api_t — the launcher is part of the OS (main.c/board), so it gets full LVGL access (grid/list/buttons), unlike sandboxed apps which only get the minimal label-only GUI surface. This would replace the current lvgl_init()'s static "TardiOS / loading..." screen with an actual home-screen widget tree.

5. Launch/return lifecycle. Today, app_loader_run() runs synchronously in one task and only returns when the app's main() returns — there's no way to interrupt a running app early, and app_exit() currently just logs (it doesn't actually terminate execution). For "tap an icon, run app, come back to home screen," you'll want: launcher calls app_loader_run(selected_path, ...), and on return, rebuild/re-show the launcher screen.




Before I write any calculator logic — I checked apps-src/myapp/main/main.c, and it's still literally the unmodified hello template (just renamed in CMakeLists.txt). No calculator code exists yet.

There's also a real architectural gap to resolve first: the current os_api_t ABI only gives apps labels (gui_label_create/set_text/del) — no buttons, no click events. A calculator needs a way to receive tap input on-screen (digits, +/-/×/÷/=), which the ABI doesn't support yet.




get_idf

