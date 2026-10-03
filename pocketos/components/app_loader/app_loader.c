#include "app_loader.h"
#include "esp_elf.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "cJSON.h"

static const char *TAG = "app_loader";

/* elf_loader's built-in libc symbol table (CONFIG_ELF_LOADER_LIBC_SYMBOLS)
 * exports __divdf3 but omits __adddf3/__subdf3/__muldf3 and all the
 * single-precision (sf3) helpers — an incomplete curation upstream, not a
 * bug in our own code. Any app doing double/float arithmetic beyond
 * division fails ELF relocation with "Can't find symbol". Fill the gap via
 * esp_elf_register_symbol(), the component's own supported extension point.
 *
 * This struct is a local mirror of elf_loader's private `struct esp_elfsym`
 * (include/private/elf_symbol.h: { const char *name; const void *sym; }),
 * which isn't exposed through its public headers. Layout must stay in sync
 * if the upstream component changes it. */
typedef struct {
    const char *name;
    const void *sym;
} extra_elfsym_t;

/* double (df) */
extern double __adddf3(double a, double b);
extern double __subdf3(double a, double b);
extern double __muldf3(double a, double b);
extern double __negdf2(double a);
extern int    __eqdf2(double a, double b);
extern int    __nedf2(double a, double b);
extern int    __ledf2(double a, double b);
extern int    __gedf2(double a, double b);
extern int    __unorddf2(double a, double b);
extern double __floatsidf(int a);
extern int    __fixdfsi(double a);
extern float  __truncdfsf2(double a);

/* single (sf) — RISC-V targets (P4, C3/C6, ...) have hardware single-
 * precision float (the rv32imafc "F" extension), so the compiler never
 * emits calls to these by name; they simply don't exist in a RISC-V build,
 * which fails the link if referenced unconditionally. Xtensa (S3) has no
 * such hardware path here, hence still needs the software versions. */
#ifndef __riscv
extern float  __addsf3(float a, float b);
extern float  __subsf3(float a, float b);
extern float  __mulsf3(float a, float b);
extern float  __divsf3(float a, float b);
extern float  __negsf2(float a);
extern int    __eqsf2(float a, float b);
extern int    __nesf2(float a, float b);
extern int    __ltsf2(float a, float b);
extern int    __lesf2(float a, float b);
extern int    __gtsf2(float a, float b);
extern int    __gesf2(float a, float b);
extern int    __unordsf2(float a, float b);
extern float  __floatsisf(int a);
extern int    __fixsfsi(float a);
#endif
extern double __extendsfdf2(float a);   /* links fine on both architectures */

/* libc — elf_loader's table has printf/fprintf/vfprintf but omits the
 * snprintf family entirely, despite it being the obvious safe choice for
 * writing into a fixed-size buffer. Already declared via <stdio.h>. */

static const extra_elfsym_t s_extra_syms[] = {
    { "__adddf3", (void *)__adddf3 },
    { "__subdf3", (void *)__subdf3 },
    { "__muldf3", (void *)__muldf3 },
    { "__negdf2", (void *)__negdf2 },
    { "__eqdf2", (void *)__eqdf2 },
    { "__nedf2", (void *)__nedf2 },
    { "__ledf2", (void *)__ledf2 },
    { "__gedf2", (void *)__gedf2 },
    { "__unorddf2", (void *)__unorddf2 },
    { "__floatsidf", (void *)__floatsidf },
    { "__fixdfsi", (void *)__fixdfsi },
    { "__truncdfsf2", (void *)__truncdfsf2 },
#ifndef __riscv
    { "__addsf3", (void *)__addsf3 },
    { "__subsf3", (void *)__subsf3 },
    { "__mulsf3", (void *)__mulsf3 },
    { "__divsf3", (void *)__divsf3 },
    { "__negsf2", (void *)__negsf2 },
    { "__eqsf2", (void *)__eqsf2 },
    { "__nesf2", (void *)__nesf2 },
    { "__ltsf2", (void *)__ltsf2 },
    { "__lesf2", (void *)__lesf2 },
    { "__gtsf2", (void *)__gtsf2 },
    { "__gesf2", (void *)__gesf2 },
    { "__unordsf2", (void *)__unordsf2 },
    { "__floatsisf", (void *)__floatsisf },
    { "__fixsfsi", (void *)__fixsfsi },
#endif
    { "__extendsfdf2", (void *)__extendsfdf2 },
    { "snprintf", (void *)snprintf },
    { "vsnprintf", (void *)vsnprintf },
    { NULL, NULL },
};

static void register_extra_symbols_once(void)
{
    static bool done = false;
    if (done) {
        return;
    }
    esp_elf_register_symbol((esp_elf_symbol_table_t *)s_extra_syms);
    done = true;
}

esp_err_t app_loader_run(const char *elf_path, const os_api_t *api, int *exit_code)
{
    register_extra_symbols_once();

    /* Intermittent "Error to load elf file, ret=-22" has been observed on
     * an unchanged, previously-working app.elf — a strong signal of heap
     * corruption from something else rather than a deterministic parse
     * bug in that file. This scans every heap for corruption and logs
     * details if found, so the next occurrence gives real evidence
     * instead of another guess. */
    if (!heap_caps_check_integrity_all(true)) {
        ESP_LOGE(TAG, "heap corruption detected before loading %s", elf_path);
    }

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

/* Fills `title` with manifest.json's "name" field if present and valid,
 * otherwise leaves it untouched (caller has already set the folder-name
 * fallback). Never fails loudly — a missing/malformed manifest just means
 * no title override. */
static void load_title(const char *root, const char *dir_name, char *title, size_t title_len)
{
    char manifest_path[APP_PATH_MAX];
    snprintf(manifest_path, sizeof(manifest_path), "%s/%s/manifest.json", root, dir_name);

    FILE *f = fopen(manifest_path, "rb");
    if (!f) {
        return;   /* no manifest — folder-name fallback already in place */
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 2048) {   /* sane upper bound for a name/version blob */
        fclose(f);
        return;
    }

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return;
    }
    size_t rd = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[rd] = '\0';

    cJSON *json = cJSON_Parse(buf);
    free(buf);
    if (!json) {
        ESP_LOGW(TAG, "%s: invalid JSON, ignoring", manifest_path);
        return;
    }

    const cJSON *name_item = cJSON_GetObjectItemCaseSensitive(json, "name");
    if (cJSON_IsString(name_item) && name_item->valuestring && name_item->valuestring[0]) {
        strncpy(title, name_item->valuestring, title_len - 1);
        title[title_len - 1] = '\0';
    }
    cJSON_Delete(json);
}

static void discover_root(const char *root, bool from_sdcard,
                          app_entry_t *out, size_t max_entries, size_t *count)
{
    DIR *d = opendir(root);
    if (!d) {
        return;   /* root doesn't exist / not mounted — nothing to contribute */
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;   /* skip "." / ".." */
        }

        char elf_path[APP_PATH_MAX];
        snprintf(elf_path, sizeof(elf_path), "%s/%s/app.elf", root, ent->d_name);

        struct stat st;
        if (stat(elf_path, &st) != 0) {
            continue;   /* not a dir with an app.elf in it — skip */
        }

        /* If this name is already in the list (e.g. found on /storage),
         * overwrite it in place instead of adding a duplicate entry. */
        size_t idx = *count;
        for (size_t i = 0; i < *count; i++) {
            if (strcmp(out[i].name, ent->d_name) == 0) {
                idx = i;
                break;
            }
        }
        if (idx == *count) {
            if (*count >= max_entries) {
                ESP_LOGW(TAG, "app list full, skipping %s/%s", root, ent->d_name);
                continue;
            }
            (*count)++;
        }

        strncpy(out[idx].name, ent->d_name, sizeof(out[idx].name) - 1);
        out[idx].name[sizeof(out[idx].name) - 1] = '\0';
        strncpy(out[idx].title, ent->d_name, sizeof(out[idx].title) - 1);   /* fallback */
        out[idx].title[sizeof(out[idx].title) - 1] = '\0';
        strncpy(out[idx].path, elf_path, sizeof(out[idx].path) - 1);
        out[idx].path[sizeof(out[idx].path) - 1] = '\0';
        out[idx].from_sdcard = from_sdcard;

        load_title(root, ent->d_name, out[idx].title, sizeof(out[idx].title));
    }
    closedir(d);
}

esp_err_t app_loader_discover(app_entry_t *out, size_t max_entries, size_t *out_count)
{
    if (!out || !out_count || max_entries == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;

    discover_root("/storage/apps", false, out, max_entries, out_count);
    discover_root("/sdcard/apps", true, out, max_entries, out_count);   /* overrides same-named entries */

    return ESP_OK;
}
