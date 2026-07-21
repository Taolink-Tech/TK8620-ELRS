# Third Party Licenses

## ExpressLRS

- Project: https://github.com/ExpressLRS/ExpressLRS
- Baseline version: `3.5.6`
- Baseline commit: `ee188b4efb9a707f682e8b2d966cd670de92ab50`
- License: GNU General Public License v3.0

GPL-3.0 text is included in `LICENSE.md`.

## FIFO Buffer

- File: `applications/ELRS_Common/ELRS/src/lib/FIFO/FIFO.h`
- Copyright: Copyright (c) 2015 Daniel Eisterhold
- License: MIT

The source file retains the complete upstream copyright and MIT license notice.

## TK8620 Linker Scripts

- Files:
  - `applications/ELRS_Rx/tk8620_flashxip.ld`
  - `applications/ELRS_Tx/tk8620_flashxip.ld`
- Original copyright: Copyright (c) 2019 Nuclei Limited
- Modifications: Shanghai Taolink Technologies Co., Ltd
- License: Apache License 2.0 (`Apache-2.0`)

Both linker scripts retain their original copyright, SPDX identifier, and
license notice. The full Apache License 2.0 text is included in
[`licenses/third-party/Apache-2.0.txt`](licenses/third-party/Apache-2.0.txt).

## TK8620 Binary SDK

- Headers: `open-sdk/include/**`
- Library: `open-sdk/lib/libtk86xx.a`
- Copyright: Shanghai Taolink Technologies Co., Ltd
- License: `LicenseRef-Taolink-TK8620-SDK-Package` (`LICENSE.sdk.md`)

## Bundled Tools

- `tools/intelhex2strhex.exe`
- `tools/codespace.exe`
- `tools/burn/tk8620_flasher.exe`
- `tools/burn/tk8620_flasher.py`
- `tools/burn/bootpatch.h`
- repository build, flashing, dependency, and packaging scripts

Taolink-owned bundled tools are licensed under
`LicenseRef-Taolink-TK8620-Bundled-Tools` (`LICENSE.tools.md`).

`tools/burn/tk8620_flasher.exe` is built from `tools/burn/tk8620_flasher.py`
with PyInstaller.

Bundled Python components include:

- Python 3.12.6 runtime: Python Software Foundation License Version 2 and
  the additional notices included with the Windows binary distribution.
  Full text:
  [`licenses/third-party/Python-3.12.6.txt`](licenses/third-party/Python-3.12.6.txt).
- pyserial 3.5: BSD-3-Clause, copyright (c) 2001-2020 Chris Liechti.
  Full text:
  [`licenses/third-party/pyserial-3.5.txt`](licenses/third-party/pyserial-3.5.txt).
- PyInstaller 6.21.0 bootloader/runtime: GPL-2.0-or-later with the
  PyInstaller bootloader exception. Full text:
  [`licenses/third-party/PyInstaller-6.21.0.txt`](licenses/third-party/PyInstaller-6.21.0.txt).

The flasher packaging script pins pyserial 3.5, PyInstaller 6.21.0, and
PyInstaller hooks 2026.6. The Python interpreter version used for the bundled
executable is documented above. The script also fixes the Python hash seed and
the PyInstaller build timestamp to make repeated builds deterministic when the
operating system, Python runtime, dependency set, and inputs are unchanged.

## TK8620 Bootloader Binary

- File: `firmware/bootloader/TK8620_B_V2.0.2.hex`
- Copyright: Shanghai Taolink Technologies Co., Ltd
- License: `LicenseRef-Taolink-TK8620-Bootloader-Binary`
  (`LICENSE.bootloader.md`)

## External Toolchain

The Nuclei RISC-V GCC toolchain is installed separately by the user.

- Download: https://download.nucleisys.com/upload/files/toolchain/gcc/nuclei_riscv_newlibc_prebuilt_win32_2020.08.zip
- Vendor page: https://www.nucleisys.com/download.php

The toolchain is governed by its upstream licenses.
