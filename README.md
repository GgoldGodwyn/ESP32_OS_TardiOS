# TardiOS

TardiOS is a small operating system for ESP32-S3 and ESP32-P4 boards with a screen. Apps don't get compiled into the firmware. You build each one on its own as a relocatable ELF, drop it onto the device's flash or a microSD card, and the OS lists it in a launcher. Tap it, and the OS loads it into PSRAM and runs it.

I started it because I wanted something like a pocket computer. I wanted to write a new program for the device without reflashing the whole firmware each time, the way you'd install an app on a phone. It's early, but the core loop works: boot, find apps, show a launcher, run one, return to the launcher.

> **Status:** experimental. It runs on my hand-wired ESP32-S3 and ESP32-P4 boards. Expect rough edges, hardcoded pins, and APIs that will grow.

---

## What works today

- **Launcher.** At boot, TardiOS scans `/storage/apps/` (internal flash) and `/sdcard/apps/` (microSD) and lists every app it finds. If an app with the same name is on the SD card, that copy replaces the internal one.
- **Runtime app loading.** Apps are relocatable ELFs. They're loaded with Espressif's [`elf_loader`](https://components.espressif.com/components/espressif/elf_loader), run from PSRAM, and freed when they exit. This works on both Xtensa (S3) and RISC-V (P4).
- **A stable app ABI.** Apps never link against OS symbols. The OS passes a function table (`os_api_t`) to the app's `main()`, and everything goes through it. The table is append-only, so old apps keep working when new calls are added.
- **GUI for apps.** Apps can create labels and tappable buttons and get click callbacks, without needing the LVGL headers.
- **Display + touch.** An ILI9341 panel on an 8-bit 8080 parallel bus, driven by LVGL, with XPT2046 resistive touch. There's a built-in touch calibration entry in the launcher.
- **microSD.** Mounted at `/sdcard` over SPI (it shares the bus with touch).
- **App manifests.** An optional `manifest.json` next to an app sets the name shown in the launcher.

Two sample apps are included: `hello` (labels and a ticking counter) and a four-function calculator built with the button API.

## Not done yet

The parts that are missing or stubbed, so you know before you dig in:

- **App file I/O is stubbed.** `fs_open`/`fs_read`/`fs_write` exist in the ABI but always return an error.
- **`app_exit()` doesn't terminate the app.** It only logs. An app finishes by returning from `main()`.
- **No preemption or sandboxing.** A misbehaving app can hang or crash the device. Apps run in the OS's address space.
- **The keyboard is only half wired.** The TCA8418 driver comes up, but it isn't connected to LVGL as an input device yet, and its interrupt pin isn't assigned.
- **Touch flakiness.** Touch occasionally stops responding until the board is fully power-cycled. The details are in `pocketos/components/board/board.c`.

## Hardware

TardiOS runs on two boards. Both use the same set of peripherals, so the OS and the apps behave the same on either one. Only the pin numbers in `pocketos/components/board/board.c` change.

| | `tardi-s3` | `tardi-p4` |
|---|---|---|
| MCU | ESP32-S3 (Xtensa), N16R8 module | ESP32-P4 (RISC-V), chip rev ≥ 1.0 |
| Flash | 16 MB | 16 MB |
| PSRAM | 8 MB octal | hex-mode PSRAM |
| Display | 3.2" ILI9341, 240×320, 8-bit i80 parallel | same |
| Touch | XPT2046 resistive, SPI2 | same |
| Storage | microSD on SPI2 (shared with touch) + LittleFS on flash | same |
| Keyboard | TCA8418 on I2C (driver present, not hooked up yet) | same |

PSRAM is required on both boards, because apps are relocated into it and run from there. The ESP32-P4 has no Wi-Fi or Bluetooth of its own, but TardiOS doesn't use either yet.

The display panel was salvaged from a DevEBox STM32F4VE board. Its header pinout, and a warning about its 8-bit/16-bit solder bridge (this one cost me a day of garbage on the screen), are in **[PIN_ASSIGNMENTS.md](PIN_ASSIGNMENTS.md)**.

### Pin map

| Signal | S3 GPIO | P4 GPIO |
|--------|:-------:|:-------:|
| LCD D0–D7 | 4, 5, 6, 7, 15, 16, 17, 18 | 39–46 |
| LCD WR | 8 | 47 |
| LCD DC | 9 | 48 |
| LCD CS | 10 | 49 |
| LCD RST | 11 | 51 |
| LCD backlight | 12 | 52 |
| Touch CS | 47 | 54 |
| Touch CLK | 41 | 3 |
| Touch MOSI | 42 | 4 |
| Touch MISO | 3 | 5 |
| Touch IRQ | 39 | — |
| SD card CS | 38 | 9 |
| Keyboard SCL | 46 | 1 |
| Keyboard SDA | 48 | 2 |
| Keyboard INT | — | 12 |

The LCD's RD line isn't connected on either board, so the display is driven write-only. To use different pins, edit the `#define`s for your target at the top of `board.c`. The right block is chosen automatically from `CONFIG_IDF_TARGET`.

## Repository layout

```
pocketos/              The OS firmware (ESP-IDF project, builds as "tardi")
  main/main.c          Boot, LVGL setup, os_api_t implementation, launcher
  components/
    board/             Hardware layer: display, touch, SD card, keyboard
    app_loader/        App discovery + ELF load/relocate/run
    os_abi/            os_api.h, the contract between OS and apps
  data/apps/           Gets packed into the LittleFS storage partition at build time
  partitions.csv       16 MB layout: 3 MB firmware, ~12.7 MB app storage

app-sdk/               Template for building an app (the "hello" app)
apps-src/myapp/        Example app built from the template (calculator)

make_storage.py        Standalone LittleFS image packer (alternative to the build-time image)
*.bat                  My Windows helper scripts; paths are hardcoded to my machine
```

## Building

You'll need **ESP-IDF 5.1 or newer** (I'm on 6.0). Dependencies such as LVGL, the LCD/touch drivers, littlefs, and elf_loader come from the ESP Component Registry automatically on the first build.

### 1. Build and flash the OS

```bash
cd pocketos
idf.py set-target esp32s3      # or esp32p4
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

The build packs everything under `pocketos/data/` into a LittleFS image and flashes it along with the firmware, so the `hello` app is already installed on first boot.

**On an S3, rebuild `hello` first.** The `app.elf` checked into `pocketos/data/apps/hello/` is a P4 (RISC-V) build. For the S3, build `app-sdk` for `esp32s3` (step 2) and copy the result over that file before you flash.

> An app has to be built for the same chip as the OS that will run it. An S3 app won't load on a P4, and a P4 app won't load on an S3.
>
> If you switch chip targets, delete `build/` and `sdkconfig` before running `set-target` again.

### 2. Build an app

```bash
cd app-sdk
idf.py set-target esp32s3      # must match the OS target
idf.py build
```

The file you want is **`build/hello.app.elf`**, *not* `hello.elf`. The `.app.elf` is the small relocatable payload (around 1.5 KB for hello). The plain `.elf` is a full 2.7 MB firmware image with debug info.

### 3. Install the app

Pick one of these:

- **SD card (easiest):** copy it to `/apps/<name>/app.elf` on the card. Reboot, and it shows up in the launcher.
- **Internal flash:** copy it to `pocketos/data/apps/<name>/app.elf` and reflash the OS. The storage image is rebuilt from that folder.

To give the app a nicer name in the launcher, add a `manifest.json` next to it:

```json
{ "name": "Calculator" }
```

## Writing your own app

Copy the template, rename it, and write `main.c`:

```bash
cp -r app-sdk apps-src/myapp
# in apps-src/myapp/CMakeLists.txt, change both project(hello) and project_elf(hello) to your name
```

An app is an ordinary `main()`. The OS hands you its API table in `argv[0]`:

```c
#include "os_api.h"

static void on_tap(void *user) {
    /* Runs on the OS's GUI task: keep it short, no sleep_ms() here */
}

int main(int argc, char **argv)
{
    const os_api_t *os = (const os_api_t *)(void *)argv[0];
    if (os->abi_version < OS_ABI_VERSION) return -2;   // OS too old for this app

    os_caps_t caps;
    os->get_caps(&caps);   // screen size, touch type, keyboard, board name

    os->gui_lock();
    os->gui_label_create(os->gui_screen, "Hi there", OS_ALIGN_CENTER, 0, -20);
    void *btn = os->gui_button_create(os->gui_screen, "Tap me",
                                      OS_ALIGN_CENTER, 0, 30, 120, 40);
    os->gui_unlock();
    os->gui_on_click(btn, on_tap, NULL);

    os->sleep_ms(10000);
    return 0;   // returning hands the screen back to the launcher
}
```

Some rules that will save you time:

- **Only talk to the OS through `os_api_t`.** Don't call ESP-IDF or LVGL functions directly. They aren't guaranteed to resolve when the ELF is loaded.
- **Not all of libc is available.** The loader exports a limited symbol table. For example, `atof` is missing but `strtod` works. If an app fails to load with an unresolved symbol, this is usually why.
- **Don't edit `os_api.h` in your app.** It's a copy of `pocketos/components/os_abi/include/os_api.h`, and the two have to stay identical. New capabilities are added on the OS side first, and then the copy is updated.

The full API, with comments on each call, is in [`os_api.h`](pocketos/components/os_abi/include/os_api.h). The calculator in `apps-src/myapp/main/main.c` is the most complete example.

## How it fits together

```
 ┌───────────────────────────────┐
 │  app.elf  (your code)         │   loaded at runtime into PSRAM
 └──────────────┬────────────────┘
                │  os_api_t*  (argv[0])
 ┌──────────────▼────────────────┐
 │  TardiOS core  (main.c)       │   launcher, LVGL, os_api_t implementation
 │  app_loader                   │   discover → relocate → run → free
 ├───────────────────────────────┤
 │  board                        │   the only layer that changes per device
 └──────────────┬────────────────┘
                │
     ILI9341 · XPT2046 · microSD · TCA8418
```

The `board` layer is the only part that knows about specific hardware. It fills in an `os_caps_t` (screen size, panel type, touch type, keyboard), and everything above it is meant to stay the same whether the screen is an i80 ILI9341, an RGB ST7701, or a MIPI-DSI panel on a P4.

## Roadmap

Roughly in the order I plan to work on them:

- [ ] Hook the TCA8418 keyboard up to LVGL
- [ ] Real jailed file storage for apps (`fs_*` calls)
- [ ] Make `app_exit()` actually stop the app, and add a watchdog for apps that hang
- [ ] More GUI primitives in the ABI (text input, lists, images)
- [ ] Use the P4's MIPI-DSI display interface (`OS_PANEL_MIPI_DSI`) instead of the i80 ILI9341
- [ ] A simple way to install apps over USB or Wi-Fi instead of shuffling SD cards

## Contributing

Issues and pull requests are welcome. If you get TardiOS running on different hardware, I'd especially like to hear about it, even if it's only a new pin block for `board.c`.

If you change the ABI, follow the append-only rule in `os_api.h`. Never reorder or remove fields. Add new function pointers at the end and bump `OS_ABI_VERSION`.

## License

TardiOS is released under the [MIT License](LICENSE).
