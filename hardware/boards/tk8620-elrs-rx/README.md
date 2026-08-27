# TKM-300 TK8620 ELRS RX Module

This directory contains the public hardware files for the TK8620 ELRS RX
module. TX and RX modules are separate boards, so use this directory only for
the RX receiver module.

## Module Information

| Item | Value |
| --- | --- |
| Product model | TKM-300 |
| Module type | TK8620 ELRS RX Module |
| Supported role | RX receiver module |
| Operating modes | RC receiver and AirPort transparent serial |
| RF band | 905.3-925.3 MHz (`SAW_915` firmware domain) |
| FHSS frequencies | 40; approximately 512.82 kHz spacing; approximately 916.069 MHz sync frequency |
| RF packet rates | 25, 50, 100, 200, and 250 Hz |
| RC channels | Up to 16 |
| Telemetry power setting | Configured maximum 100 mW (20 dBm) |
| Firmware sensitivity reference | -123 dBm at 25 Hz; -120 dBm at 50 Hz; -117 dBm at 100 Hz; -114 dBm at 200 Hz; -112 dBm at 250 Hz |
| RC serial protocols | CRSF or SBUS, selectable from the receiver Lua `Serial Protocol` setting |
| CRSF serial | Bidirectional, full duplex, normal polarity, 8N1, 420000 baud |
| SBUS serial | Output only on `TX/SBUS`, non-inverted, 8E2, 100000 baud |
| AirPort serial | Bidirectional TX/RX UART, full duplex, normal polarity, 8N1; selectable baud, 460800 default |
| Antenna connector | IPEX |
| Input voltage | 5 V nominal |
| Dimensions | 11 x 17 mm |
| UART pins | GND, 5V, TX/SBUS, RX |
| Firmware update | Local UART or wireless update from the TX module |
| EDA tool | Altium Designer 16 |
| Firmware output | `build/ELRS_Rx/TK8620_ELRS_RX_P.hex` |
| Release status | Released |
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
AirPort baud-rate choices and mode switching are documented under
[`RC/AirPort Mode`](../../../README.md#rcairport-mode).

## RC Serial Connection

For CRSF, connect module `TX/SBUS` to the flight-controller RX pin and module
`RX` to the flight-controller TX pin. Also connect 5V and GND, then select
`CRSF` in the receiver Lua `Serial Protocol` setting.

For SBUS, connect module `TX/SBUS` to the flight-controller SBUS input and
connect 5V and GND. Select `SBUS` in the receiver Lua `Serial Protocol` setting;
the module `RX` pin is not used in SBUS mode.

## Reset

The RX reset point is `TP5` on the bottom side of the module. It is marked
`TP5` on the bottom silkscreen.

When `configure.cmd` or `burn.cmd` displays the manual-reset prompt, briefly
touch the USB-UART adapter's 3.3 V output to `TP5` and release it immediately.
Keep the existing UART ground connection in place.

## Custom Hardware Notes

If you use this module as the starting point for your own product, keep this
module file set together and read [`../../CUSTOMIZATION.md`](../../CUSTOMIZATION.md)
before modifying the design.
