# TKB-310 TK8620 ELRS TX Module

This directory contains the public hardware files for the TK8620 ELRS TX
module. TX and RX modules are separate boards, so use this directory only for
the TX transmitter module.

## Module Information

| Item | Value |
| --- | --- |
| Product model | TKB-310 |
| Module type | TK8620 ELRS TX Module |
| Supported role | TX transmitter module |
| Operating modes | RC transmitter and AirPort transparent serial |
| RF band | 905.3-925.3 MHz (`SAW_915` firmware domain) |
| FHSS frequencies | 40; approximately 512.82 kHz spacing; approximately 916.069 MHz sync frequency |
| RF packet rates | 25, 50, 100, 200, and 250 Hz |
| RC channels | Up to 16 |
| Firmware power settings | 50, 100, 250, 500, or 1000 mW; configured maximum 1000 mW (30 dBm) |
| Dynamic power | Supported in RC mode |
| Radio control input | CRSF handset input; SBUS handset input is not enabled in the standard firmware |
| CRSF serial | Half duplex, 8N1; 400000 baud default, selectable 420000 or 921600 |
| AirPort serial | Full duplex, normal polarity, 8N1; selectable baud, 460800 default |
| Antenna connector | SMA |
| Input voltage | 7-12.6 V |
| Radio module interface | Nano external-module interface |
| Onboard USB-UART | Type-C connector for configuration, flashing, and AirPort serial |
| EDA tool | Cadence 16.6 |
| Firmware output | `build/ELRS_Tx/TK8620_ELRS_TX_P.hex` |
| Release status | Released |
| Purchase page | [`../../PURCHASE.md`](../../PURCHASE.md) |

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
AirPort baud-rate choices and mode switching are documented under
[`RC/AirPort Mode`](../../../README.md#rcairport-mode).

## Reset

`configure.cmd` and `burn.cmd` reset the TX module automatically through the
onboard USB-UART interface, so no manual reset is normally required.

## Custom Hardware Notes

If you use this module as the starting point for your own product, keep this
module file set together and read [`../../CUSTOMIZATION.md`](../../CUSTOMIZATION.md)
before modifying the design.
