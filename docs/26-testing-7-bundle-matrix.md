# 26 — Testing: 7-Bundle Regression Matrix

Encore's primary correctness criterion is: *every supported bundle
boots to attract mode with graphics and DCS audio*. The regression
matrix encodes this criterion as a reproducible manual test procedure.

## The seven bundles

| # | Bundle directory pattern              | Title | Version | Notes |
|---|---------------------------------------|-------|---------|-------|
| 1 | `pin2000_50069_0150_*`                | SWE1  | v1.5    | Baseline SWE1 |
| 2 | `pin2000_50069_0210_*`                | SWE1  | v2.1    | Latest SWE1 |
| 3 | `pin2000_50070_0120_*`                | RFM   | v1.2    | Oldest RFM (1999) |
| 4 | `pin2000_50070_0160_*`                | RFM   | v1.6    | |
| 5 | `pin2000_50070_0180_*`                | RFM   | v1.8    | |
| 6 | `pin2000_50070_0250_*`                | RFM   | v2.5    | |
| 7 | `pin2000_50070_0260_*`                | RFM   | v2.6    | Latest RFM |

## The two modes under test

Each bundle is run in both `--dcs-mode` variants:

| Mode            | Flag                      | Expected behaviour |
|-----------------|---------------------------|--------------------|
| `bar4-patch`    | `--dcs-mode bar4-patch`   | DCS audio via BAR4 patch |
| `io-handled`    | `--dcs-mode io-handled`   | Natural PCI probe; audio only where probe returns 1 |

`bar4-patch` is the default and is the primary delivery path for all
bundles. `io-handled` is tested to verify the alternate path does not
regress the boot (even if audio is silent on some bundles in this mode).

## Pass criteria

For **bar4-patch mode** a bundle passes when all of the following are
observed within 60 seconds of launch:

1. `[disp] first non-zero framebuffer detected` — guest wrote pixels.
2. `[disp] FPS:` line logged — display loop is running.
3. `[irq] XINU ready: timer injection enabled` — scheduler is live.
4. DCS boot-dong sound plays (audible or confirmed via
   `[sound] boot dong` log line).
5. SDL window shows the WMS logo or attract-mode graphics (no stuck
   black frame).

For **io-handled mode** criteria 1–3 and 5 are sufficient (audio may
be silent on bundles whose natural probe returns 0).

**RFM v1.2 exception:** this bundle boots to a pre-XINU crash state.
The bar4-patch-mode pass criterion is reduced to: process does not
segfault, UART serial output is visible, no infinite silent hang.
See [38-known-limitations.md](38-known-limitations.md).

## Running the matrix

No automated harness exists. The recommended manual procedure:

```sh
# For each of the 7 bundles, run in both modes:
for VER in 150 210; do
    ./build/encore --game swe1 --update $VER --dcs-mode bar4-patch \
        --no-savedata --headless --serial-tcp 4444 &
    sleep 45
    kill $!
done

for VER in 120 160 180 250 260; do
    ./build/encore --game rfm --update $VER --dcs-mode bar4-patch \
        --no-savedata --headless --serial-tcp 4444 &
    sleep 45
    kill $!
done
```

With `--headless` the SDL window is skipped; attach via
`nc localhost 4444` to see the XINU serial console.

For interactive testing (with display and audio), omit `--headless` and
`--serial-tcp`.

## Expected log excerpt (passing run)

```
[rom]  Bank 0: 8 chip files found, interleaving…
[sgc]  BT-74: nulluser idle JMP$ → HLT+JMP at 0x00xxxxxx
[sgc]  watchdog suppression active: [0x003444b0] primed=0xffff
[irq]  clkint detected: IDT[0x20]=0x00xxxxxx EIP=0x00xxxxxx exec=NN
[irq]  XINU ready: timer injection enabled EIP=0x00xxxxxx exec=NN
[init] DCS-mode pattern hit @0x001931e4 — patched
[disp] first non-zero framebuffer detected (fb_off=0x800000)
[disp] FPS: 57.0 (285 frames / 5000 ms)
```

## Current status

| Bundle    | bar4-patch | io-handled |
|-----------|-----------|------------|
| SWE1 v1.5 | ✓ pass    | ✓ partial (no audio) |
| SWE1 v2.1 | ✓ pass    | ✓ partial  |
| RFM v1.2  | ⚠ limited | ⚠ limited  |
| RFM v1.6  | ✓ pass    | ✓ partial  |
| RFM v1.8  | ✓ pass    | ✓ partial  |
| RFM v2.5  | ✓ pass    | ✓ partial  |
| RFM v2.6  | ✓ pass    | ✓ partial  |

## Cross-references

* RFM v1.2 details: [38-known-limitations.md](38-known-limitations.md)
* DCS mode duality: [13-dcs-mode-duality.md](13-dcs-mode-duality.md)
* RFM vs SWE1 differences: [35-rfm-vs-swe1.md](35-rfm-vs-swe1.md)
