# Third Party Licenses

## ExpressLRS

- Project: https://github.com/ExpressLRS/ExpressLRS
- Baseline version: `3.5.6`
- Baseline commit: `ee188b4efb9a707f682e8b2d966cd670de92ab50`
- License: GNU General Public License v3.0

GPL-3.0 text is included in `LICENSE.md`.

## TK8620 Binary SDK

- Headers: `open-sdk/include/**`
- Library: `open-sdk/lib/libtk86xx.a`
- Copyright: Shanghai Taolink Technologies Co., Ltd
- License: `LICENSE.sdk.md`

## Bundled Tools

- `tools/intelhex2strhex.exe`
- `tools/codespace.exe`
- `tools/burn/tk8620_bootrom.exe`
- `tools/burn/tk8620_bootrom.py`

Taolink-owned bundled tools are licensed under `LICENSE.tools.md`.

`tools/burn/tk8620_bootrom.exe` is built from `tools/burn/tk8620_bootrom.py`
with PyInstaller.

Bundled Python components include:

- Python runtime: Python Software Foundation License
- pyserial 3.5: BSD license
- PyInstaller bootloader/runtime: GPLv2-or-later with the PyInstaller
  bootloader exception

## External Toolchain

The Nuclei RISC-V GCC toolchain is installed separately by the user.

- Download: https://download.nucleisys.com/upload/files/toolchain/gcc/nuclei_riscv_newlibc_prebuilt_win32_2020.08.zip
- Vendor page: https://www.nucleisys.com/download.php

The toolchain is governed by its upstream licenses.
