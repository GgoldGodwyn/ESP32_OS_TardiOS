/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

#include "private/elf_symbol.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"

/* Symbols exported to loaded ELF apps.
 * Apps may ONLY call functions listed here — everything else goes through os_api_t.
 * Add entries as needed; never remove or reorder existing ones. */

const struct esp_elfsym g_customer_elfsyms[] = {
    /* formatted I/O */
    ESP_ELFSYM_EXPORT(snprintf),
    ESP_ELFSYM_EXPORT(vsnprintf),
    ESP_ELFSYM_EXPORT(sprintf),
    ESP_ELFSYM_EXPORT(printf),

    /* memory */
    ESP_ELFSYM_EXPORT(malloc),
    ESP_ELFSYM_EXPORT(free),
    ESP_ELFSYM_EXPORT(calloc),
    ESP_ELFSYM_EXPORT(realloc),
    ESP_ELFSYM_EXPORT(memcpy),
    ESP_ELFSYM_EXPORT(memset),
    ESP_ELFSYM_EXPORT(memmove),
    ESP_ELFSYM_EXPORT(memcmp),

    /* strings */
    ESP_ELFSYM_EXPORT(strlen),
    ESP_ELFSYM_EXPORT(strcmp),
    ESP_ELFSYM_EXPORT(strncmp),
    ESP_ELFSYM_EXPORT(strcpy),
    ESP_ELFSYM_EXPORT(strncpy),
    ESP_ELFSYM_EXPORT(strcat),
    ESP_ELFSYM_EXPORT(strncat),
    ESP_ELFSYM_EXPORT(strchr),
    ESP_ELFSYM_EXPORT(strrchr),
    ESP_ELFSYM_EXPORT(strstr),
    ESP_ELFSYM_EXPORT(strtol),
    ESP_ELFSYM_EXPORT(strtoul),
    ESP_ELFSYM_EXPORT(atoi),
    ESP_ELFSYM_EXPORT(atof),

    /* math */
    ESP_ELFSYM_EXPORT(abs),
    ESP_ELFSYM_EXPORT(fabs),
    ESP_ELFSYM_EXPORT(sqrt),
    ESP_ELFSYM_EXPORT(sin),
    ESP_ELFSYM_EXPORT(cos),
    ESP_ELFSYM_EXPORT(floor),
    ESP_ELFSYM_EXPORT(ceil),

    ESP_ELFSYM_END
};

#pragma GCC diagnostic pop
