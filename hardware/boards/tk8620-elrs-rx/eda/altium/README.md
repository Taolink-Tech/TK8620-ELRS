# Altium Designer Project

This directory contains the Altium Designer source project for this hardware
module. Open the project in a compatible Altium Designer version to review or
modify the schematic and PCB.

## Design Source

- Altium Designer 16
- [`ELRS-RX-MODULE-V11-20260127.PrjPcb`](ELRS-RX-MODULE-V11-20260127.PrjPcb):
  project file.
- [`01_SOC.SchDoc`](01_SOC.SchDoc), [`02_RF.SchDoc`](02_RF.SchDoc),
  [`03_Power.SchDoc`](03_Power.SchDoc), [`04_UART.SchDoc`](04_UART.SchDoc),
  and [`05_Notes.SchDoc`](05_Notes.SchDoc): schematic source sheets.
- [`ELRS-RX-MODULE-V11-20260204.PcbDoc`](ELRS-RX-MODULE-V11-20260204.PcbDoc):
  PCB layout source.
- [`ELRS-RX-MODULE-V11-20260204.PcbLib`](ELRS-RX-MODULE-V11-20260204.PcbLib):
  PCB footprint library.
- [`ELRS-RX-MODULE-V11-20260127.OutJob`](ELRS-RX-MODULE-V11-20260127.OutJob):
  output job configuration.
- [`ELRS-RX-MODULE-V11-20260127.PrjPcbStructure`](ELRS-RX-MODULE-V11-20260127.PrjPcbStructure):
  project hierarchy metadata.

## Notes

- Manufacturing files are not stored here. Exported manufacturing files are in
  [`../../fabrication/`](../../fabrication/).
- The project, PCB, and manufacturing output filenames contain different dates.
  Keep this complete file set together when reviewing the released design.
- After modifying the project, update the exported schematic and manufacturing
  files.
