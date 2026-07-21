# Fabrication Files

This directory contains manufacturing files for this hardware module. Use them
to evaluate PCB prototyping, assembly, production, and test flow.

Manufacturing files should be generated from the matching [`../eda/`](../eda/)
source project. Before production, perform your own DFM, BOM, placement, RF,
power, and regulatory checks.

## Files

- [`gerber_ELRS-RX-MODULE-V11-20260204/`](gerber_ELRS-RX-MODULE-V11-20260204/):
  Gerber layers, NC drill data, aperture data, generation reports, and
  fabrication test-point reports.
- [`BOM表.xls`](BOM表.xls): bill of materials.

Pick-and-place files, separate fabrication and assembly drawings, and
production test notes are not included in this release.

## Notes

- Do not mix these files with files from another hardware module.
- The Gerber headers identify Altium Designer 22.0.2 (36) as the generation
  software. The editable source project is documented for Altium Designer 16.
- If you modify the source project, regenerate all affected manufacturing files.
- Use production releases only after your own manufacturing validation.
