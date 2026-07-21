# Boards

This directory contains board-specific hardware files.

Each board directory should contain one subdirectory per hardware revision. A
revision directory is the authoritative boundary for schematic, PCB,
manufacturing, mechanical, image, and bring-up files for that revision.

Recommended naming:

- board directory: lowercase product or module name, for example
  `tk8620-elrs`
- revision directory: stable hardware revision, for example `rev-a` or `v1.0`
