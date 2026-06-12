/*
 * app_loader.h — load, relocate, run and tear down an ELF app.
 *
 * Backed by Espressif's elf_loader component (esp_elf_*), which handles the
 * Xtensa (S3) and RISC-V (P4) relocations and runs the image out of PSRAM.
 */
#pragma once
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

#ifdef __cplusplus
}
#endif
