MEMORY {
  iram : org = 0x4FF50000, len = 0x4500
  dram : org = 0x4FF60000, len = 0x1D000
}

/* Compile-time guard: keep the stub DRAM window clear of ROM data at the top of DRAM.
   ROM data begins at 0x4ffbffc8 for this chip (s_usb_osglue, see esp-stub-lib esp32p4.rom*.ld). */
ASSERT(ORIGIN(dram) + LENGTH(dram) <= 0x4ffbffc8, "esp32p4: stub DRAM window overlaps ROM data")

INCLUDE common.ld
