# TK8620 ELRS Hardware

This directory is the user-facing entry point for TK8620 ELRS hardware
reference files. It is written for both first-time users and experienced
hardware developers.

You can use these files to identify a module, review the design, manufacture a
prototype, or adapt the reference hardware for your own product.

## Start Here

| Goal | Start with |
| --- | --- |
| Buy a module and use the open firmware | [`PURCHASE.md`](PURCHASE.md) |
| Find hardware files for a module | [`boards/`](boards/) |
| Customize hardware for your own product | [`CUSTOMIZATION.md`](CUSTOMIZATION.md) |
| Check hardware license status | [`LICENSE.md`](LICENSE.md) |

## Module File Rule

Hardware files are grouped by module. TX and RX modules are separate boards, so
use files from the matching module directory.

Do not mix schematics, PCB files, BOMs, placement files, or manufacturing
outputs from TX and RX modules. Mixing files can cause pinout
mismatches, assembly errors, or firmware incompatibility.

## Directory Layout

- `boards/`: module-specific TX and RX hardware files.
- `PURCHASE.md`: module purchase and selection notes.
- `CUSTOMIZATION.md`: guidance for adapting the reference hardware.
- `LICENSE.md`: CERN-OHL-P-2.0 license scope for hardware files.

## Current Module File Set

The released module directories include:

- editable EDA source project
- available schematic exports
- Gerber, drill, BOM, and related manufacturing outputs
- firmware compatibility notes for that module

## Relationship With Firmware

This repository can build TX and RX firmware:

- TX output: `build/ELRS_Tx/TK8620_ELRS_TX_P.hex`
- RX output: `build/ELRS_Rx/TK8620_ELRS_RX_P.hex`

Before buying or building hardware, confirm that the module supports the TX or
RX role you need. Also check the interface, power, antenna, and RF band
information in the module README.

## Custom Products

The hardware reference files are licensed under `CERN-OHL-P-2.0` unless noted
otherwise. Customers may modify the reference design and use those modifications
in their own products without being required by this hardware documentation to
publish their hardware changes. Feedback and upstream contributions are welcome,
but optional.

Firmware based on ExpressLRS is separate and remains subject to its GPL-3.0
license.

## RF And Regulatory Notice

ELRS modules involve RF transmission and reception. Before buying, modifying,
manufacturing, or using hardware, confirm that your use case complies with local
RF regulations, frequency requirements, power limits, antenna requirements, and
safety requirements.
