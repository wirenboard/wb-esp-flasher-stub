# Wiren Board fork

This is a fork of `esp-flasher-stub` made while speeding up ESP32 production flashing on the
Wiren Board test stand (the module is flashed over a built-in CH343 USB-UART bridge). It adds the
changes below on top of upstream; each is described here. The upstream README follows further down.

## 1. UART clock/APB boost on ESP32

Upstream raises the CPU/APB clock (`stub_lib_clock_init()`, APB 40 → 80 MHz) **only for USB
transports**; over a plain UART the stub keeps running at the ROM download clock, which throttles
flash writes. The gate exists because the ESP32-S3 UART path was unstable without setting DBIAS.

The ESP32 target's `stub_target_clock_init()` **does** set DBIAS (`RTC_CNTL_DBIAS_1V25`), so the
boost is safe for UART there. We enable it for UART, **gated to esp32 only** — via an `ESP32`
compile macro defined by the build for `TARGET_CHIP=esp32` (same style as the existing `ESP8266`
macro); every other target keeps upstream USB-only behaviour. Boosting the APB changes the ROM-set
UART baud divider, so the divider is reprogrammed for the current link baud (115200) right after
the boost, otherwise the `OHAI` handshake is corrupted. Measured ~+12–17% flash-write throughput
on ESP32-U4WDH over a CH343 USB-UART.

## 2. Skip erase on already-blank sectors

`write_flash` always erases the target region before programming. On a factory-blank chip
(all `0xFF`) that erase is the single largest cost of a flash and is redundant: NOR program only
clears `1 → 0` bits, so any data can be written straight onto erased (`0xFF`) flash.

The stub now reads each sector before erasing it and **skips the erase when the sector is already
all-`0xFF`**. Heuristic: after the first non-blank sector is seen, it erases the rest
unconditionally without reading — a used chip is dirty from its low addresses (bootloader), so the
wasted reads are ~one sector, while a factory-blank chip skips *all* erases. Correctness is
guaranteed: a sector is only left un-erased when it has been verified blank. This is plain NOR
logic (independent of the CPU), so it is enabled on every target **except ESP8266**, whose tiny
IRAM/DRAM cannot fit the 4 KB sector read buffer and blank-check code — it keeps upstream's
unconditional erase. Measured on ESP32-U4WDH:
writing a 1.77 MB image to a blank chip drops from 13.0 s to 7.6 s (−5.3 s); a dirty chip erases
as before with no penalty; the hash is verified in both cases.

## 3. 32 KB flash write block

`FRAME_BUFFER_SIZE` is raised from 16 KB to 32 KB so esptool can send larger `FLASH_DEFL_DATA`
chunks (run esptool with `FLASH_WRITE_SIZE=0x8000`), halving the number of per-block round-trips.
The double buffer (2×32 KB) lives in `.bss`, so **every chip's** DRAM window is widened to hold it
(to `len 0x1D000` = 116 KB, mirroring esp32). Each `ld/<chip>.ld` also gains a link-time `ASSERT`
that the window stays below the chip's ROM data/stack, so an unsafe window **fails the build**
instead of silently corrupting RAM at runtime. **ESP8266 keeps the 16 KB block** — its ~80 KB DRAM
cannot hold a 2×32 KB double buffer. Verified in CI: all 14 chips build.

## Using the prebuilt stub

CI builds every chip on each push and publishes the JSON stubs to the rolling
[`latest`](https://github.com/wirenboard/wb-esp-flasher-stub/releases/tag/latest) release, so you
can use the faster stub without building it. Download the JSON for your chip and copy it over the
one esptool ships with:

```sh
# 1. Download the stub for your chip (esp32 shown; any released chip works)
curl -LO https://github.com/wirenboard/wb-esp-flasher-stub/releases/download/latest/esp32.json

# 2. Copy it into your esptool install (base path auto-detected from the installed esptool;
#    adjust the trailing "1" to your esptool's stub-version dir if different)
STUB_DIR="$(python -c "import esptool, os; print(os.path.join(os.path.dirname(esptool.__file__), 'targets', 'stub_flasher', '1'))")"
cp esp32.json "$STUB_DIR/"
```

Or grab every chip at once with the GitHub CLI:

```sh
gh release download latest --repo wirenboard/wb-esp-flasher-stub --pattern '*.json' --dir "$STUB_DIR"
```

The clock boost and skip-erase speedups then apply automatically. The 32 KB block additionally
requires esptool to send 32 KB chunks (raise its `FLASH_WRITE_SIZE` to `0x8000`).

<!-- wb-fork-notes-end -->

---

[![pre-commit.ci status](https://results.pre-commit.ci/badge/github/espressif/esp-flasher-stub/master.svg)](https://results.pre-commit.ci/latest/github/espressif/esp-flasher-stub/master)

# ESP Flasher Stub

ESP Flasher Stub is a set of small firmware programs (stubs) that run on Espressif ESP chips to enable fast and reliable flash programming via [esptool](https://github.com/espressif/esptool/). When esptool connects to an ESP chip, it uploads the flasher stub into the chip's RAM. The stub then takes over communication, providing faster flash operations and additional features compared to the chip's built-in ROM bootloader.

This project has replaced the deprecated [legacy flasher stub of esptool](https://github.com/espressif/esptool-legacy-flasher-stub/) and is the default flasher stub since esptool [v5.3](https://github.com/espressif/esptool/releases/tag/v5.3.0).

## Documentation

- [Architecture](docs/architecture.md) - Firmware architecture, source code structure, modules, and build system internals
- [Development Guide](docs/development-guide.md) - Contributing guidelines, testing, CI/CD, and release process
- [Plugin System](docs/plugin-system.md) - Runtime-loadable plugin architecture and guide for adding new plugins
- [NAND Flash Support](docs/plugins/nand-flash.md) - NAND flash programming support (preview)

## Supported Chips

| Architecture | Chips |
|---|---|
| Xtensa | ESP32, ESP32-S2, ESP32-S3 |
| RISC-V | ESP32-C2, ESP32-C3, ESP32-C5, ESP32-C6, ESP32-C61, ESP32-H2, ESP32-H4, ESP32-P4, ESP32-P4 (rev1) |
| Xtensa (LX106) | ESP8266 |

## Build Dependencies

### Submodules

The project depends on [esp-stub-lib](https://github.com/espressif/esp-stub-lib/) as a git submodule. Make sure to initialize it before building:

```sh
git submodule update --init --recursive
```

### Toolchains

The following toolchains must be set up and available in your PATH:
1. [`xtensa-lx106-elf-*`](https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/get-started/index.html#setup-toolchain)
2. [`xtensa-*-elf-*`](https://github.com/espressif/crosstool-NG)
3. [`riscv32-esp-elf-*`](https://github.com/espressif/crosstool-NG)

There is a convenience script for AMD64 Linux machines to download and install them into the `toolchains` directory:
```sh
mkdir -p toolchains
cd toolchains
../tools/setup_toolchains.sh
```

Then source the export script in every terminal where the project is used:
```sh
. ./tools/export_toolchains.sh
```

### Python Dependencies

[pyelftools](https://github.com/eliben/pyelftools) is needed for ELF file analysis. Install it in a virtual environment:

```sh
python -m venv venv
source venv/bin/activate
pip install pyelftools
```

Activate the virtual environment in every terminal where the project is used:
```sh
source venv/bin/activate
```

> [!NOTE]
> **Target tests only:** Running or building the target tests in `unittests/target/` additionally
> requires [esptool](https://github.com/espressif/esptool/) (`pip install esptool`). It is used
> to convert compiled ELF files to loadable binaries (`elf2image`) and to upload them to hardware
> (`load_ram`). Host tests (`unittests/host/`) have no esptool dependency.

## How to Build

### Build for One Selected Chip Target

```sh
mkdir -p build
cmake . -B build -G Ninja -DTARGET_CHIP=esp32s2   # Replace with your desired chip, e.g. esp32, esp8266
ninja -C build
```

### Build for All Supported Chip Targets

```sh
./tools/build_all_chips.sh
```

## How to Use with Esptool

1. Install esptool in [development mode](https://docs.espressif.com/projects/esptool/en/latest/esp32/contributing.html#development-setup).
2. Obtain the flasher stub binaries as JSON files either from the [releases page](https://github.com/espressif/esp-flasher-stub/releases) or from the artifacts of your pull request.
3. Replace the esptool JSON files in the `esptool/targets/stub_flasher` directory with the obtained JSON files.

    Example copy command (adjust the path to your esptool directory):

    ```sh
    cp build-*/*.json ~/esptool/esptool/targets/stub_flasher/1/
    ```

## How It Works

The flasher stub operates through upload, initialization, handshake (`OHAI` over the selected transport), and a command loop that handles flash, memory, register, and SPI operations. UART and USB transports use SLIP framing; SDIO uses raw command frames. For details, see the [Architecture](docs/architecture.md) document.

## Contributing

See the [Contributing](docs/development-guide.md#contributing) section of the Development Guide for code style, pre-commit hooks, copyright headers, and the pull request checklist.

## How to Release (for Maintainers Only)

See the [Releasing](docs/development-guide.md#releasing-maintainers-only) section of the Development Guide.

## License

This document and the attached source code are released as Free Software under either the [Apache License Version 2](LICENSE-APACHE) or [MIT License](LICENSE-MIT) at your option.
