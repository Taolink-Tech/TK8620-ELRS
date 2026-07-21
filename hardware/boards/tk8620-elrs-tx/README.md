# TK8620 ELRS TX Module

This directory contains the public hardware files for the TK8620 ELRS TX
module. TX and RX modules are separate boards, so use this directory only for
the TX transmitter module.

## Module Information

| Item | Value |
| --- | --- |
| Module type | TK8620 ELRS TX Module |
| Supported role | TX transmitter module |
| RF band | Sub-GHz, 890-910 MHz (`GLOBAL868_926` firmware domain) |
| Antenna connector | SMA |
| Input voltage | 6-13 V (9 V or lower recommended) |
| EDA tool | Cadence 16.6 |
| Firmware output | `build/ELRS_Tx/TK8620_ELRS_TX_P.hex` |
| Release status | Released |
| Known limitations | None known |
| Purchase page | [`../../PURCHASE.md`](../../PURCHASE.md) |

The TX module accepts a 6-13 V input. Use 9 V or lower when possible; above
9 V, the power amplifier runs hotter and power efficiency decreases.

## Files

- [`eda/`](eda/): editable Cadence source project and EDA notes.
- [`schematics/`](schematics/): exported schematic review files.
- [`fabrication/`](fabrication/): manufacturing outputs.

## Firmware Use

Build the TX firmware from the repository root:

```powershell
.\build.cmd tx
```

Flash the TX module by following [`Flash TX`](../../../README.md#flash-tx).

## Custom Hardware Notes

If you use this module as the starting point for your own product, keep this
module file set together and read [`../../CUSTOMIZATION.md`](../../CUSTOMIZATION.md)
before modifying the design.
