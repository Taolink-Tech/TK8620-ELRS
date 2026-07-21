# TK8620 SDK Binary License

Copyright (c) 2026 Shanghai Taolink Technologies Co., Ltd.

The TK8620 SDK binary package includes:

- `open-sdk/include/**`
- `open-sdk/lib/libtk86xx.a`

The SDK is owned by Shanghai Taolink Technologies Co., Ltd. It provides TK8620
chip-specific platform, PHY, radio, flash, and runtime support required to
build firmware for TK8620-based hardware.

The SDK does not contain ExpressLRS source code or other GPL-derived
application-layer code.

Permission is granted to use, copy, and redistribute the SDK binary package for
the purpose of building, modifying, flashing, testing, and redistributing
TK8620 ELRS firmware for TK8620-based hardware.

The SDK source code is not included in this public release. The SDK is provided
as a chip-specific binary support library because the underlying implementation
is tightly coupled to TK8620 silicon and radio/PHY operation.

Application firmware source derived from ExpressLRS remains licensed under
GPL-3.0. Redistributors must comply with both the GPL-3.0 license for the
application layer and this license for the TK8620 SDK binary package.

Redistributors must preserve this license notice and all copyright notices
included with the SDK package.

The SDK is provided "as is", without warranty of any kind, express or implied,
including but not limited to the warranties of merchantability, fitness for a
particular purpose, and noninfringement.
