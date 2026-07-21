# Schematics

This directory contains exported schematics for this hardware module. Use them
to review circuit connections, interfaces, power paths, and key components.

Schematic exports should be generated from the matching [`../eda/`](../eda/)
source project. When reviewing or modifying hardware, cross-check the exported
schematic with the source project.

## Files

- [`ELRS-TX-MODULE-V13-20260608.pdf`](ELRS-TX-MODULE-V13-20260608.pdf):
  exported schematic PDF.

## Typical Uses

- Confirm module interfaces, power, UART, buttons, LEDs, and RF path.
- Check hardware connections before changing firmware pins or peripherals.
- Decide which circuits can be reused and which need validation in a custom
  product.
