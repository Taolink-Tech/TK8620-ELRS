# TK8620 ELRS RX Module

This directory contains the public hardware files for the TK8620 ELRS RX
module. TX and RX modules are separate boards, so use this directory only for
the RX receiver module.

## Module Information

| Item | Value |
| --- | --- |
| Module type | TK8620 ELRS RX Module |
| Supported role | RX receiver module |
| RF band | Sub-GHz, 890-910 MHz (`GLOBAL868_926` firmware domain) |
| Antenna connector | IPEX |
| Input voltage | 4.5-5.5 V |
| EDA tool | Altium Designer 16 |
| Firmware output | `build/ELRS_Rx/TK8620_ELRS_RX_P.hex` |
| Release status | Released |
| Known limitations | None known |
| Purchase page | [`../../PURCHASE.md`](../../PURCHASE.md) |

## Files

- [`eda/`](eda/): editable Altium Designer source project and EDA notes.
- [`fabrication/`](fabrication/): manufacturing outputs.

## Firmware Use

Build the RX firmware from the repository root:

```powershell
.\build.cmd rx
```

Flash the RX module by following [`Flash RX`](../../../README.md#flash-rx), or
stage it for wireless update by following
[`Stage RX Firmware For Wireless Update`](../../../README.md#stage-rx-firmware-for-wireless-update).

## Custom Hardware Notes

If you use this module as the starting point for your own product, keep this
module file set together and read [`../../CUSTOMIZATION.md`](../../CUSTOMIZATION.md)
before modifying the design.
