# EDA Source Project

This directory contains the editable hardware source project for this hardware
module. Hardware developers can use it to review, modify, and regenerate
schematics, PCB files, BOMs, placement files, and manufacturing outputs.

Available generated PDFs, Gerbers, and BOMs are stored in sibling directories
so users can review or evaluate production without opening the EDA project.

## Project

- [`cadence/`](cadence/): Cadence source project.

## Notes

- Confirm that you are using the complete TX or RX module file set before
  modifying the project.
- After modifying the source project, regenerate affected files in
  [`../schematics/`](../schematics/) and [`../fabrication/`](../fabrication/).
