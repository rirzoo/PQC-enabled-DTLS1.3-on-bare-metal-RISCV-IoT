#!/usr/bin/env python3
"""Regenerate a LiteX sim RAM-init file (readmemh, one 32-bit word per line) from a
firmware .bin, using LiteX's own get_mem_data so the words are byte-identical to what
litex_sim bakes at gateware-build time.

Why this exists: litex_sim only (re)writes sim_main_ram.init while it *builds* the
gateware (litex_sim.py: integrated_main_ram_init = get_mem_data(args.ram_init, ...)).
A sim launch that reuses an up-to-date gateware build keeps the OLD firmware image, so
a per-cell firmware rebuild is silently ignored. The benchmark harness calls this after
each `make` to force the fresh boot.bin into the model before launching Vsim.

Usage:  gen_raminit.py <firmware.bin> <out.init> [endianness]
        endianness defaults to "little" (VexRiscv; conf_soc.cpu.endianness).
"""
import sys

from litex.soc.integration.common import get_mem_data


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: gen_raminit.py <firmware.bin> <out.init> [little|big]")
    bin_path = sys.argv[1]
    out_path = sys.argv[2]
    endianness = sys.argv[3] if len(sys.argv) > 3 else "little"

    # data_width=32 matches the sim main_ram; a single .bin region makes `offset`
    # cancel out (base == offset), so the word list starts at index 0.
    data = get_mem_data(bin_path, data_width=32, endianness=endianness)
    with open(out_path, "w") as f:
        for word in data:
            f.write("%08x\n" % word)
    print("gen_raminit: wrote %d words (%s) to %s" % (len(data), endianness, out_path))


if __name__ == "__main__":
    main()
