MEMORY {
  iram : org = 0x4FF10000, len = 0x4500
  dram : org = 0x4FF50000, len = 0x1D000
}

/* Compile-time guard: keep the stub DRAM window clear of ROM data at the top of DRAM.
   ROM data begins at 0x4ffbffc8 for this chip (s_usb_osglue, see esp-stub-lib esp32p4.rom*.ld;
   esp32p4-rev1 maps to the esp32p4 target). */
ASSERT(ORIGIN(dram) + LENGTH(dram) <= 0x4ffbffc8, "esp32p4-rev1: stub DRAM window overlaps ROM data")

INCLUDE common.ld
