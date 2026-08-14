# TK8620 ELRS

TK8620 ELRS is an ExpressLRS 3.5.6 firmware port and hardware reference package
for TK8620-based TX and RX modules.

This repository is for people who want to use TK8620 hardware quickly and for
developers who want to evaluate, customize, or integrate TK8620-based radio
modules into their own products.

You can use this repository to:

- buy a TK8620 ELRS module and build firmware for it
- review public hardware design files such as schematics, PCB files, BOMs, and
  manufacturing outputs
- modify the hardware reference design for your own product
- use the ELRS firmware port as a compatibility reference for TK8620 hardware

## Purchase and Contact

- Buy TK8620 ELRS TX/RX modules: [Taobao product page](https://item.taobao.com/item.htm?id=1068476636220)
- Company contact: [+8616621375462](tel:+8616621375462)

## Start Here

| Goal | Start with |
| --- | --- |
| Buy a TK8620 ELRS module | [Taobao product page](https://item.taobao.com/item.htm?id=1068476636220) |
| Choose a module and review purchase notes | [`hardware/PURCHASE.md`](hardware/PURCHASE.md) |
| Build TX/RX firmware | [Build](#build) |
| Switch between RC and AirPort | [RC/AirPort Mode](#rcairport-mode) |
| Flash a TX module | [Flash TX](#flash-tx) |
| Flash an RX module over UART | [Flash RX](#flash-rx) |
| Stage RX firmware for wireless update | [Stage RX Firmware For Wireless Update](#stage-rx-firmware-for-wireless-update) |
| Review hardware files | [`hardware/README.md`](hardware/README.md) |
| Customize your own hardware product | [`hardware/CUSTOMIZATION.md`](hardware/CUSTOMIZATION.md) |

## Supported Hardware

| Hardware | Role | Status | Hardware files | Purchase |
| --- | --- | --- | --- | --- |
| TK8620 ELRS TX Module | TX | Released | [`hardware/boards/tk8620-elrs-tx/`](hardware/boards/tk8620-elrs-tx/) | [`hardware/PURCHASE.md`](hardware/PURCHASE.md) |
| TK8620 ELRS RX Module | RX | Released | [`hardware/boards/tk8620-elrs-rx/`](hardware/boards/tk8620-elrs-rx/) | [`hardware/PURCHASE.md`](hardware/PURCHASE.md) |

Use files from the matching module directory. Do not mix TX and RX schematics,
PCB files, BOMs, placement files, or manufacturing outputs.

## Repository Contents

- `applications/ELRS_Tx/`: TX application firmware source.
- `applications/ELRS_Rx/`: RX application firmware source.
- `applications/ELRS_Common/`: shared TX/RX application source.
- `hardware/`: hardware reference files, purchase notes, manufacturing outputs,
  and customization guidance.
- `open-sdk/include/`: public TK8620 SDK headers.
- `open-sdk/lib/libtk86xx.a`: TK8620 binary SDK library.
- `firmware/bootloader/`: bootloader image used by `burn.cmd`.
- `tools/`: build, flashing, and packaging tools.
- `VERSION`: repository release version.

This repository does not include prebuilt, linked TX or RX application
firmware. The TX and RX firmware images described below are generated locally
when the user runs the build scripts.

## License Model

This is a mixed-license repository. The ELRS application firmware is derived
from ExpressLRS and remains under GPL-3.0. The TK8620 SDK headers and binary
library are provided under the custom
`LicenseRef-Taolink-TK8620-SDK-Package` license in `LICENSE.sdk.md`.

Hardware reference files under `hardware/` are licensed under the permissive
`CERN-OHL-P-2.0` hardware license unless a specific file states another license.
Customers can modify the hardware for their own products without being required
to publish those hardware changes. Scope and trademark notes are in
[`hardware/LICENSE.md`](hardware/LICENSE.md).

Taolink-owned build, flashing, and packaging tools are provided under the
custom `LicenseRef-Taolink-TK8620-Bundled-Tools` license in `LICENSE.tools.md`.
Third-party components retain their upstream licenses as listed in
`THIRD_PARTY_LICENSES.md`.

The TK8620 bootloader binary is provided under the custom
`LicenseRef-Taolink-TK8620-Bootloader-Binary` license in
`LICENSE.bootloader.md`.

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

## Build

Open PowerShell in this repository directory.

Build RX firmware:

```powershell
.\build.cmd rx
```

Build TX firmware:

```powershell
.\build.cmd tx
```

Build both TX and RX:

```powershell
.\build.cmd
```

Clean build output:

```powershell
.\build.cmd clean
```

Build output:

- `build/ELRS_Rx/TK8620_ELRS_RX_P.hex`
- `build/ELRS_Tx/TK8620_ELRS_TX_P.hex`

## RC/AirPort Mode

The same TX and RX firmware supports normal RC operation and AirPort transparent
serial transmission. Flash and bind both devices before switching modes.

Run the interactive configuration tool:

```powershell
.\configure.cmd
```

Select RC, AirPort, or status query, then restart each device when prompted.
Configure both TX and RX to the same mode. If user parameters were erased during
flashing, switch to RC and bind the pair again before using AirPort.

In AirPort mode, connect the computer to the TX module's Type-C serial interface
and connect the external device to the RX module UART TX, RX, and GND pins. Both
serial ports use `9600 baud, 8 data bits, no parity, 1 stop bit`, normal polarity,
and full duplex.

AirPort carries a byte stream, not application packet boundaries. It does not
acknowledge or retransmit lost RF packets. Applications that require complete
messages must add their own framing, length, sequence, checksum, and retry
logic. Restore RC operation by running `configure.cmd` and setting both modules
to RC.

## Flash TX

Build TX firmware first:

```powershell
.\build.cmd tx
```

Connect the TX module UART through a USB-to-UART adapter, then run:

```powershell
.\burn.cmd tx
```

The script lists detected COM ports and asks you to select the target port.
TX user settings are preserved by default. Add `-Erase` only when you
intentionally want to reset them.

## Flash RX

Use this method when you can connect the RX module UART directly to a
USB-to-UART adapter.

Build the RX firmware first:

```powershell
.\build.cmd rx
```

Connect the RX module UART through a USB-to-UART adapter, then run:

```powershell
.\burn.cmd rx
```

The script lists detected COM ports and asks you to select the RX module port.
This command flashes the RX module over its UART connection.
RX user settings and binding are preserved by default. Add `-Erase` only when
you intentionally want to reset them; the RX must then be bound again.

## Stage RX Firmware For Wireless Update

Use this method when the RX module will be updated wirelessly from the TX
module. In this flow, the computer connects to the TX module UART. The RX module
does not need to be connected to the computer by UART.

Build the RX firmware first:

```powershell
.\build.cmd rx
```

Connect the TX module UART through a USB-to-UART adapter, then stage the RX
firmware image into TX flash:

```powershell
.\burn.cmd rx-stash
```

The RX image path is fixed:

```text
build/ELRS_Rx/TK8620_ELRS_RX_P.hex
```

After staging, keep the RX module within RF range of the TX module. Start the RX
wireless update from the TX firmware by using one of these methods:

- Send this hexadecimal command to the TX module serial port:

  ```text
  EE062DEEEF0A0150
  ```

- Open the transmitter Lua menu and start the RX wireless update from the menu.

After the TX module enters wireless update mode, reset or power-cycle the RX
module. Wait for the wireless update to complete.

## Licenses

- `LICENSE.md`: GPL-3.0 license for ELRS-derived application firmware.
- `hardware/LICENSE.md`: CERN-OHL-P-2.0 license scope for hardware files.
- `LICENSE.sdk.md`: custom TK8620 SDK package license.
- `LICENSE.bootloader.md`: custom TK8620 bootloader binary license.
- `LICENSE.tools.md`: custom bundled Taolink tools license.
- `THIRD_PARTY_LICENSES.md`: third-party notices.
