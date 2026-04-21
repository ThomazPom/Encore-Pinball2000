# 31 — tools/deinterleave_rebuild.sh

`tools/deinterleave_rebuild.sh` reconstructs the four interleaved
chip-ROM bank files from pairs of individual chip dump files.

> **Status:** Behaviour described here is based on emulator testing
> only. Real-cabinet validation is pending — see
> [docs/42-cabinet-testing-call.md](42-cabinet-testing-call.md) for
> how to help verify.

## Background

The Pinball 2000 hardware stores the game code in eight physical ROM
chips per bank. Chips are wired in **2-byte interleaved pairs**: chip A
holds bytes 0, 2, 4, … of the logical bank and chip B holds bytes 1, 3,
5, …. A ROM dumper that reads each chip independently produces two files
that must be interleaved back together before Encore can load them.

See [08-rom-loading-pipeline.md](08-rom-loading-pipeline.md) for the
full picture of how Encore maps chip ROMs into guest memory.

## What the script does

For each of the four 32 MB banks the script calls an inline Python
snippet that interleaves two input files at 2-byte granularity:

```
bank[0..1] = chip_A[0], chip_A[1], chip_B[0], chip_B[1], chip_A[2], …
```

The interleaver works in 8 KB chunks for reasonable performance. Both
input files must be the same size and that size must be a multiple of 2;
the script aborts with a diagnostic message otherwise.

## Default file names (SWE1)

The script is written for the SWE1 chip naming convention:

| Bank | Even chip   | Odd chip    | Output       |
|------|-------------|-------------|--------------|
| 0    | `swe1_u100.rom` | `swe1_u101.rom` | `bank0.bin` |
| 1    | `swe1_u102.rom` | `swe1_u103.rom` | `bank1.bin` |
| 2    | `swe1_u104.rom` | `swe1_u105.rom` | `bank2.bin` |
| 3    | `swe1_u106.rom` | `swe1_u107.rom` | `bank3.bin` |

The four banks are then concatenated into a single `swe1_rebuilt.bin`:

```sh
cat bank0.bin bank1.bin bank2.bin bank3.bin > swe1_rebuilt.bin
```

For RFM the same interleave logic applies but the chip names differ
(`rfm_u100.rom` … `rfm_u107.rom`). Edit the script or run the inline
Python directly with substituted names.

## Usage

```sh
cd /path/to/chip/dumps
bash tools/deinterleave_rebuild.sh
# → bank0.bin bank1.bin bank2.bin bank3.bin swe1_rebuilt.bin
```

The script must be run from the directory that contains the `uXXX.rom`
files; it does not accept path arguments.

## Manual invocation (single bank)

To interleave one pair of chip files without the full script:

```sh
python3 -c "
import sys, os
f1, f2, out = sys.argv[1], sys.argv[2], sys.argv[3]
s = os.path.getsize(f1)
with open(f1,'rb') as a, open(f2,'rb') as b, open(out,'wb') as o:
    CHUNK = 8192
    while True:
        da, db = a.read(CHUNK), b.read(CHUNK)
        if not da: break
        buf = bytearray(len(da)+len(db))
        for i in range(0, len(da), 2):
            j = i*2
            buf[j]=da[i]; buf[j+1]=da[i+1]
            buf[j+2]=db[i]; buf[j+3]=db[i+1]
        o.write(buf)
" swe1_u100.rom swe1_u101.rom bank0.bin
```

## Notes on RFM r2 chip ROMs

RFM v1.6 and later use a revised set of chip ROMs labelled `r2`. These
are physically different chips but the interleave format is identical.
The only change is the file naming convention; the script logic is
unchanged. See [35-rfm-vs-swe1.md](35-rfm-vs-swe1.md) for details.

## Cross-references

* ROM loading pipeline: [08-rom-loading-pipeline.md](08-rom-loading-pipeline.md)
* RFM chip ROM differences: [35-rfm-vs-swe1.md](35-rfm-vs-swe1.md)
* Assembling update bundles: [30-tools-build-update-bin.md](30-tools-build-update-bin.md)

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
