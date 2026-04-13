/*
 * Manul - Flash Storage
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * Based on Iris by Mikhail Matveev / VersaTerm by David Hansel
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>
#include <stdlib.h>

/* 16MB flash: use top 64KB (16 sectors) for data storage */
#define FLASH_STORAGE_SIZE  65536
#define FLASH_TARGET_OFFSET (16 * 1024 * 1024 - (FLASH_STORAGE_SIZE))

uint32_t flash_get_write_offset(uint8_t sector) {
    return sector < 16 ? FLASH_TARGET_OFFSET + sector * 4096 : 0;
}

uint8_t *flash_get_read_ptr(uint8_t sector) {
    return sector < 16 ? (uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET + FLASH_SECTOR_SIZE * sector) : NULL;
}

size_t flash_get_sector_size(void) {
    return FLASH_SECTOR_SIZE;
}

int flash_write_partial(uint8_t sector, const void *data, size_t position, size_t size) {
    int ok = 0;
    if (position + size <= FLASH_SECTOR_SIZE) {
        uint8_t *mem = malloc(FLASH_SECTOR_SIZE);
        if (mem != NULL) {
            memcpy(mem, flash_get_read_ptr(sector), FLASH_SECTOR_SIZE);
            memcpy(mem + position, data, size);
            ok = flash_write(sector, mem, FLASH_SECTOR_SIZE);
            free(mem);
        }
    }
    return ok;
}

int flash_write(uint8_t sector, const void *data, size_t length) {
    if (sector < 16 && length <= FLASH_SECTOR_SIZE) {
        size_t offset = 0;
        uint32_t ints = save_and_disable_interrupts();

        flash_range_erase(flash_get_write_offset(sector), FLASH_SECTOR_SIZE);

        offset = 0;
        while (offset + FLASH_PAGE_SIZE <= length) {
            flash_range_program(flash_get_write_offset(sector) + offset, (uint8_t *)data + offset, FLASH_PAGE_SIZE);
            offset += FLASH_PAGE_SIZE;
        }

        if (offset < length) {
            static uint8_t buffer[FLASH_PAGE_SIZE];
            memcpy(buffer, (uint8_t *)data + offset, length - offset);
            flash_range_program(flash_get_write_offset(sector) + offset, buffer, FLASH_PAGE_SIZE);
        }

        restore_interrupts(ints);
        return memcmp(data, flash_get_read_ptr(sector), length) == 0;
    }
    return 0;
}

void flash_read(uint8_t sector, void *data, size_t length) {
    uint8_t *ptr = flash_get_read_ptr(sector);
    if (ptr != NULL) memmove(data, ptr, length);
}
