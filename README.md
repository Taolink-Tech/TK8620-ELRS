# TK8620 ELRS

TK8620 ELRS is an ExpressLRS 3.5.6 firmware port for TK8620-based TX and RX
modules.

This package includes:

- TX/RX application source code
- public TK8620 SDK headers
- TK8620 binary SDK library
- Windows build scripts
- Windows flashing scripts
- TK8620 bootloader image

The application source is licensed under GPL-3.0. The TK8620 binary SDK library
is provided under `LICENSE.sdk.md`.

## Requirements

- Windows
- PowerShell
- Nuclei RISC-V GCC `2020.08` / GCC `9.2.0`

Download the verified Windows toolchain:

https://download.nucleisys.com/upload/files/toolchain/gcc/nuclei_riscv_newlibc_prebuilt_win32_2020.08.zip

After extracting the toolchain, add the directory containing
`riscv-nuclei-elf-gcc.exe` to `PATH`, or set `NUCLEI_GCC_BIN` to that directory.

Open a new PowerShell window and run:

```powershell
riscv-nuclei-elf-gcc.exe --version
```

The expected compiler is GCC `9.2.0`.

Newer GCC 14.x toolchains, including Nuclei 2025.10 packages using
`riscv64-unknown-elf-gcc.exe`, are not compatible with the current
`open-sdk/lib/libtk86xx.a` binary SDK library.

## Build

Open PowerShell in this directory.

Build RX firmware:

```powershell
.\build.cmd rx
```

Build TX firmware:

```powershell
.\build.cmd tx
```

Build both:

```powershell
.\build.cmd
```

Clean build output:

```powershell
.\build.cmd clean
```

Build output is written to:

- `build/ELRS_Rx/TK8620_ELRS_RX_P.hex`
- `build/ELRS_Tx/TK8620_ELRS_TX_P.hex`
- `build/firmware-manifest.json`

## Flash TX

Build the TX firmware first:

```powershell
.\build.cmd tx
```

Connect the TX module UART through a USB-to-UART adapter, then run:

```powershell
.\burn.cmd tx
```

The script lists detected COM ports and asks you to select one. Press the TX
module reset key when prompted.

## Stage RX Firmware For Wireless Update

Build the RX firmware first:

```powershell
.\build.cmd rx
```

Stage the RX firmware image into TX flash:

```powershell
.\burn.cmd rx-stash
```

The RX image path is fixed:

```text
build/ELRS_Rx/TK8620_ELRS_RX_P.hex
```

After staging, start the RX wireless update from the TX firmware, then reset or
power-cycle the RX module after the update completes.

## Files

- `applications/ELRS_Tx/`: TX application source
- `applications/ELRS_Rx/`: RX application source
- `applications/ELRS_Common/`: shared application source
- `open-sdk/include/`: public TK8620 SDK headers
- `open-sdk/lib/libtk86xx.a`: TK8620 binary SDK library
- `firmware/bootloader/`: bootloader image used by `burn.cmd`
- `tools/`: build and flashing tools
- `VERSION`: TK8620 ELRS release version

## Licenses

- `LICENSE.md`: GPL-3.0 license for the application source
- `LICENSE.sdk.md`: TK8620 binary SDK license
- `LICENSE.tools.md`: bundled Taolink tool license
- `THIRD_PARTY_LICENSES.md`: third-party notices
