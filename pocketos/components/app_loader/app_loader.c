#include "app_loader.h"
#include "esp_elf.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "app_loader";

esp_err_t app_loader_run(const char *elf_path, const os_api_t *api, int *exit_code)
{
    /* 1. Slurp the ELF image off the filesystem into PSRAM. */
    FILE *f = fopen(elf_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "cannot open %s", elf_path);
        return ESP_ERR_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return ESP_ERR_INVALID_SIZE; }

    uint8_t *image = heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!image) {
        fclose(f);
        ESP_LOGE(TAG, "no PSRAM for %ld-byte image", size);
        return ESP_ERR_NO_MEM;
    }
    size_t rd = fread(image, 1, (size_t)size, f);
    fclose(f);
    if (rd != (size_t)size) { free(image); return ESP_FAIL; }

    /* 2. Init + relocate. Sections land in PSRAM with the right (exec/data) caps. */
    esp_elf_t elf;
    esp_err_t err = esp_elf_init(&elf);
    if (err != ESP_OK) { free(image); return err; }

    err = esp_elf_relocate(&elf, image);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "relocate failed: %d", err);
        esp_elf_deinit(&elf);
        free(image);
        return err;
    }

    /* 3. Enter the app. We pass the os_api table as argv[0]; the app casts it
     *    back. The app reaches the OS only through that table (not via global
     *    symbols) — libc and other host symbols are resolved separately by the
     *    elf_loader's symbol table. */
    char *argv[1];
    argv[0] = (char *)(void *)api;
    ESP_LOGI(TAG, "launching %s (abi v%u)", elf_path, (unsigned)api->abi_version);

    int ret = esp_elf_request(&elf, 0, 1, argv);
    if (exit_code) *exit_code = ret;
    ESP_LOGI(TAG, "app returned %d", ret);

    /* 4. Tear down: free relocated sections, then the source image. */
    esp_elf_deinit(&elf);
    free(image);
    return ESP_OK;
}
