# Customize Your Own Product

This repository supports two different customization paths:

- ELRS-derived firmware customization
- TK8620 hardware reference customization

The license obligations are different. ELRS-derived application firmware remains
under GPL-3.0. Hardware reference files are licensed under `CERN-OHL-P-2.0`
unless a specific file states another license, so customers can adapt the
hardware for their own products without being required to publish their hardware
changes. Feedback is welcome, but optional.

## Recommended Workflow

1. Select the reference module closest to your product, for example
   [`boards/tk8620-elrs-tx/`](boards/tk8620-elrs-tx/) or
   [`boards/tk8620-elrs-rx/`](boards/tk8620-elrs-rx/).
2. Record the repository version, module type, and source commit you started
   from.
3. Define your change goal, such as connector changes, antenna changes, power
   input changes, size changes, enclosure integration, or firmware behavior.
4. Modify the EDA source project and regenerate the schematic, BOM, placement
   files, and manufacturing outputs that are affected.
5. Update firmware pin, peripheral, power, or device configuration when the
   hardware change requires it.
6. Build TX/RX firmware and validate it on prototype hardware.
7. Complete power, RF, temperature, reliability, and regulatory checks for your
   product.
8. Keep an internal change record for your product. If you want to contribute a
   fix or improvement upstream, open an issue or pull request.

## Keep Module Files Together

Each module directory is a complete file set. When you customize hardware, start
from one module directory and keep its schematic, PCB source, BOM, placement,
and manufacturing files together.

Create a separate product version or board directory for your product if your
changes affect:

- schematic connections or key component choices
- PCB stackup, RF path, antenna interface, or impedance control
- power tree, input voltage range, or protection circuit
- connector definitions, pin assignments, or mechanical outline
- mounting holes, board outline, height limits, or thermal design
- production, test, or calibration flow

## Files To Check When You Customize

| Change type | Check these files |
| --- | --- |
| Firmware behavior or protocol change | `applications/ELRS_Tx/`, `applications/ELRS_Rx/`, `applications/ELRS_Common/` |
| Pin or peripheral change | firmware configuration, schematic, PCB, test notes |
| Antenna or RF path change | schematic, PCB source, RF test record, regulatory requirements |
| Power change | schematic, BOM, PCB, thermal design, power test record |
| Mechanical or mounting change | PCB source, product-specific CAD, board measurements, board images, assembly notes |
| Production data change | `fabrication/`, BOM, placement files, Gerber, drill files |

## Sharing Changes Is Optional

You are encouraged, but not required by the hardware documentation, to share
hardware improvements with Taolink or the community. Useful feedback includes:

- bug reports
- unclear documentation
- manufacturing issues
- RF or power validation findings
- improvements that you are willing to contribute back

If you distribute modified GPL firmware, the GPL firmware obligations still
apply to that firmware. This is separate from permissive hardware customization.

## Before Production

The hardware files in this repository help you start a design, but they do not
replace product validation. Before using a custom hardware product in
production, complete your own checks for:

- hardware and firmware compatibility
- schematic and PCB review
- BOM availability and approved alternates
- manufacturing test procedure
- RF calibration, RF performance, and local regulatory compliance
- power safety, thermal behavior, and reliability
