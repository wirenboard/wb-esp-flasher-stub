MEMORY {
  iram : org = 0x40028000, len = 0x18000
  dram : org = 0x3FFD0000, len = 0x28000
}

/* Compile-time guard: keep the stub DRAM window clear of ROM data at the top of DRAM.
   First real ROM DRAM data begins at 0x3fffc410 for this chip (see esp-stub-lib esp32s2.rom.ld).
   Note: the ROM region marker _dram0_0_start = 0x3ffeab00 sits below this window, but no ROM
   data object is actually placed there during stub execution; this hardware-verified 0x28000
   window is left unchanged. */
ASSERT(ORIGIN(dram) + LENGTH(dram) <= 0x3fffc410, "esp32s2: stub DRAM window overlaps ROM data")

INCLUDE common.ld
