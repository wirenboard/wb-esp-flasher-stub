/*
 * SPDX-FileCopyrightText: 2026 Wiren Board
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

/*
 * ESP32 fast flash write.
 *
 * Strong override of the weak stub_target_flash_write_buff() from esp-stub-lib
 * (kept as a pristine upstream submodule). Compiled only for the esp32 target
 * (see src/CMakeLists.txt), so the low-level SPI1 register access below is safe.
 *
 * The default writes via ROM esp_rom_spiflash_write() in 32-byte bursts. IDF's
 * esp_flash driver programs in 64-byte bursts (SPI_FLASH_HAL_MAX_WRITE_BYTES),
 * halving the number of page-program commands per 256-byte page. This replicates
 * the IDF sequence directly on the SPI1 controller with the hardware Page-Program
 * command (SPI_FLASH_PP, opcode 0x02). Program-only write rate +6% (275 -> 292 KB/s
 * at CPU 80MHz on ESP32-U4WDH + XM25QH32C), matching the on-device esp_flash driver.
 */

#include <stdbool.h>
#include <stdint.h>

#include <esp-stub-lib/soc_utils.h>
#include <esp-stub-lib/err.h>
#include <esp-stub-lib/bit_utils.h>

#include <target/flash.h>
#include <private/rom_flash.h>

#include <soc/spi_reg.h>

extern int esp_rom_spiflash_write_encrypted(uint32_t flash_addr, const void *data, uint32_t len);

int stub_target_flash_write_buff(uint32_t addr, const void *buffer, uint32_t size, bool encrypt)
{
    if (encrypt) {
        int res = esp_rom_spiflash_write_encrypted(addr, buffer, size);
        return (res == ESP_ROM_SPIFLASH_RESULT_OK) ? STUB_LIB_OK : STUB_LIB_ERR_FLASH_WRITE;
    }

    const uint8_t *src = (const uint8_t *)buffer;

    /* Wait for any in-progress erase to finish before the first page-program. flash_defl_begin
     * can ACK the region erase while the flash write-in-progress (WIP) bit is still set, so the
     * very first PP would otherwise fire into a busy flash and be silently dropped (leaving the
     * first 64 bytes at 0xFF -> MD5 mismatch). Later PPs are safe because each loop iteration
     * ends with the WIP wait below. spi_wait_ready() only tracks the SPI controller, not WIP. */
    {
        uint32_t prime_guard = 8000000U;
        while (stub_target_flash_is_busy() && (--prime_guard != 0U)) { }
        if (prime_guard == 0U) {
            return STUB_LIB_ERR_FLASH_WRITE;
        }
    }

    /* 24-bit flash address for the hardware page-program: the controller reads the byte
     * count from SPI_ADDR[31:24] and the address from [23:0] only when addr bitlen is 24
     * (mirrors IDF spi_flash_hal_program_page -> set_addr_bitlen(24)). */
    REG_SET_FIELD(SPI_USER1_REG(FLASH_SPI_NUM), SPI_USR_ADDR_BITLEN, 23);

    while (size > 0U) {
        uint32_t chunk = MIN(size, 64U);
        uint32_t page_left = 256U - (addr & 0xFFU); /* never cross a 256-byte flash page */
        if (chunk > page_left) {
            chunk = page_left;
        }

        stub_target_spi_wait_ready();

        /* Load the SPI data buffer word-at-a-time (alignment-safe), like IDF's
         * spi_flash_ll_set_buffer_data(). */
        uint32_t left = chunk;
        const uint8_t *p = src;
        for (uint32_t i = 0U; left > 0U; i++) {
            uint32_t word = 0U;
            uint32_t wl = MIN(left, 4U);
            for (uint32_t b = 0U; b < wl; b++) {
                word |= ((uint32_t)p[b]) << (8U * b);
            }
            WRITE_PERI_REG(SPI_W0_REG(FLASH_SPI_NUM) + (i * 4U), word);
            p += wl;
            left -= wl;
        }

        uint32_t guard;

        /* Write-enable (harmless even if the hardware PP command auto-issues it). */
        WRITE_PERI_REG(SPI_CMD_REG(FLASH_SPI_NUM), SPI_FLASH_WREN);
        guard = 4000000U;
        while ((READ_PERI_REG(SPI_CMD_REG(FLASH_SPI_NUM)) != 0U) && (--guard != 0U)) { }
        if (guard == 0U) {
            return STUB_LIB_ERR_FLASH_WRITE;
        }

        /* 24-bit address, byte count in the top 8 bits (IDF flash_pp convention). */
        WRITE_PERI_REG(SPI_ADDR_REG(FLASH_SPI_NUM), (addr & 0xFFFFFFU) | (chunk << 24));
        WRITE_PERI_REG(SPI_USER_REG(FLASH_SPI_NUM),
                       READ_PERI_REG(SPI_USER_REG(FLASH_SPI_NUM)) & ~(uint32_t)SPI_USR_DUMMY);

        /* Trigger hardware page-program and wait for the SPI transaction to finish. */
        WRITE_PERI_REG(SPI_CMD_REG(FLASH_SPI_NUM), SPI_FLASH_PP);
        guard = 4000000U;
        while ((READ_PERI_REG(SPI_CMD_REG(FLASH_SPI_NUM)) != 0U) && (--guard != 0U)) { }
        if (guard == 0U) {
            return STUB_LIB_ERR_FLASH_WRITE;
        }

        /* Wait for the flash write-in-progress bit to clear before the next page. */
        guard = 8000000U;
        while (stub_target_flash_is_busy() && (--guard != 0U)) { }
        if (guard == 0U) {
            return STUB_LIB_ERR_FLASH_WRITE;
        }

        addr += chunk;
        src += chunk;
        size -= chunk;
    }
    return STUB_LIB_OK;
}
