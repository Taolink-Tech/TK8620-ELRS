# Hardware

This directory is the public entry point for TK8620 ELRS hardware design
materials.

Hardware files are organized by board name and hardware revision. Use files from
one revision only. Do not mix schematic, PCB, BOM, placement, mechanical, or
manufacturing files across revisions unless a revision note explicitly permits
it.

## Directory Layout

- `boards/`: board-specific design files, grouped by board and revision
- `common/`: reusable symbols, footprints, and 3D models shared by boards
- `docs/`: hardware development notes that are not tied to one board revision
- `LICENSE.md`: hardware licensing status and publication requirements

## Release Rule

Manufacturing outputs are derived files. When publishing a hardware release,
generate them from the matching source design files and record the source commit
or release tag in the board revision README.
