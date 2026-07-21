# Hardware Boards

This directory organizes TK8620 ELRS hardware reference files by module.

TX and RX modules are separate boards. Each module directory contains the EDA
source, available schematic exports, manufacturing outputs, and firmware
compatibility information for that module.

## Boards

| Module | Description | Files |
| --- | --- | --- |
| TK8620 ELRS TX Module | TX transmitter module hardware | [`tk8620-elrs-tx/`](tk8620-elrs-tx/) |
| TK8620 ELRS RX Module | RX receiver module hardware | [`tk8620-elrs-rx/`](tk8620-elrs-rx/) |

## Usage

- New users can start with [`../PURCHASE.md`](../PURCHASE.md) to identify the
  module to buy.
- Firmware developers can open a module directory to check firmware
  compatibility and interfaces.
- Hardware developers can use a module directory as the starting point for a
  custom product.
