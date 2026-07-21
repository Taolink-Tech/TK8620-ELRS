# Buy TK8620 ELRS Hardware Modules

This page helps you choose and buy TK8620 ELRS hardware modules that can be used
with firmware built from this repository.

If you only want to use the firmware and do not want to build or modify PCB
hardware, start with the ready-made module purchase page below. Select the TX
or RX variant on the product page, then build and flash the matching firmware.

## Before You Buy

Check these items before buying:

- whether you need the TX variant, the RX variant, or both variants
- whether the module type is documented in this repository
- whether the RF band, antenna connector, and input voltage match your use case
- whether you plan to build and flash firmware from this repository
- whether your region allows the selected frequency, power, and antenna setup

## Module Purchase

| Product | Variants | Status | Purchase link | Hardware files |
| --- | --- | --- | --- | --- |
| TK8620 ELRS Module | Select TX or RX on the product page; order both variants for a matched pair | Released | [Taobao product page](https://item.taobao.com/item.htm?ft=t&id=1068476636220&spm=a21dvs.23580594.0.0.4fee2c1bPYE3AY) | [`TX`](boards/tk8620-elrs-tx/) / [`RX`](boards/tk8620-elrs-rx/) |

## After You Receive A Module

1. Check your selected product variant, package label, or purchase record to
   confirm whether the module is TX or RX.
2. Open the matching module README under [`boards/`](boards/).
3. Install the toolchain and build TX or RX firmware from the repository root.
4. Connect UART, power, and antenna according to the module notes.
5. Flash or update the firmware.

## Build Or Modify Your Own Hardware

If you do not buy a ready-made module and instead want to build or modify
hardware from the reference files, read [`CUSTOMIZATION.md`](CUSTOMIZATION.md)
before using the EDA and manufacturing files for a module.

Custom hardware requires your own manufacturing checks, RF validation, power
safety validation, and regulatory review.
