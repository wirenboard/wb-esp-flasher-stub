/* Note: stub is deliberately loaded close to the very top
   of available RAM, to reduce change of colliding with anything
   else... */
MEMORY {
  iram : org = 0x400A8000, len = 0x2100
  dram : org = 0x3ffc3000, len = 0x1D000
}

INCLUDE common.ld
