/*
 * app_loader.h — load, relocate, run and tear down an ELF app.
 *
 * Backed by Espressif's elf_loader component (esp_elf_*), which handles the
 * Xtensa (S3) and RISC-V (P4) relocations and runs the image out of PSRAM.
 */
#pragma once
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "os_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Read the ELF at `elf_path`, relocate it into PSRAM, and call its
 * `int main(int argc, char **argv)` with argv[0] = the os_api table pointer.
 *
 * Blocks until the app's main() returns; run it from a dedicated task so the
 * app gets an isolated stack and can be bounded by a watchdog later.
 * On return the relocated image is freed (clean teardown of the app arena).
 */
esp_err_t app_loader_run(const char *elf_path, const os_api_t *api, int *exit_code);

#define APP_NAME_MAX  32
/* Sized to fit "/sdcard/apps/" + a full 255-byte d_name + "/app.elf" + NUL,
 * since GCC's -Wformat-truncation assumes dirent's worst case (256 bytes),
 * not the filesystem's actual (smaller) name limit. */
#define APP_PATH_MAX  300

typedef struct {
    char name[APP_NAME_MAX];   /* the apps/<name> directory name — stable identity */
    char title[APP_NAME_MAX];  /* display name: manifest.json "name", else = name */
    char path[APP_PATH_MAX];   /* full path to that app's app.elf */
    bool from_sdcard;
} app_entry_t;

/*
 * Discover installed apps under /storage/apps/<name>/app.elf and, if a
 * microSD card is mounted, /sdcard/apps/<name>/app.elf too.
 *
 * SD-card apps are treated as user-installed/updatable content: an SD entry
 * with the same <name> as an internal one overrides it (replaces its path)
 * rather than appearing as a separate entry.
 *
 * If /storage/apps/<name>/manifest.json (or the sdcard equivalent) exists
 * and has a "name" string field, that becomes the entry's `title` — used
 * for display only. `name` (the directory name) stays the stable identity
 * apps are looked up/launched by. Apps without a manifest just show their
 * directory name.
 *
 * Writes up to max_entries results into `out` and the count into *out_count.
 * Never fails outright — a missing/unmounted root just contributes nothing.
 */
esp_err_t app_loader_discover(app_entry_t *out, size_t max_entries, size_t *out_count);

#ifdef __cplusplus
}
#endif
