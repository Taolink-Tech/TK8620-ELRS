# Fabrication Files

This directory contains manufacturing files for this hardware module. Use them
to evaluate PCB prototyping, assembly, production, and test flow.

Manufacturing files should be generated from the matching [`../eda/`](../eda/)
source project. Before production, perform your own DFM, BOM, placement, RF,
power, and regulatory checks.

## Files

- [`Gerber-ELRS-TX-MODULE-V13-20260610/`](Gerber-ELRS-TX-MODULE-V13-20260610/):
  RS-274X Gerber layers, NC drill and route files, tool lists, and generation
  parameters.
- [`BOM参考.xlsx`](BOM参考.xlsx): bill of materials reference.

Pick-and-place files, separate fabrication and assembly drawings, and
production test notes are not included in this release.

## Notes

- Do not mix these files with files from another hardware module.
- If you modify the source project, regenerate all affected manufacturing files.
- Use production releases only after your own manufacturing validation.
