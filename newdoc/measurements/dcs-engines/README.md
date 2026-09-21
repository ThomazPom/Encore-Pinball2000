# DCS engine comparison

`run-comparison.py` applies one repeatable cabinet workload to the six current
DCS engines, sequentially:

- `pb2kslib`;
- `pb2kslib-adsp`;
- `adsp`;
- `adsp-thread`;
- `adsp-clock-thread`;
- `adsp-hybrid-thread`.

It is a scheduler/content-engine comparison tool, not the release support
matrix and not the authoritative natural-timing verdict. Use public `--bench`
for that verdict and the parent [testing guide](../../26-testing-validation-matrix.md)
for the complete validation ladder.

## Default workload

Each engine runs alone, so engines do not compete for host CPU. Defaults are:

- SWE1 update 2.10;
- read-only savedata, no display and the QEMU WAV backend;
- detailed diagnostics;
- F4 at 11 seconds, then three credits and twenty alternating volume presses;
- 90 seconds per engine;
- report windows beginning at wall time 30 seconds.

Complete `snap` windows at or after the warmup contribute to current delivery;
the partial exit window does not. Live engines also emit a shutdown health
record covering queue drainage, resets, drops, PCM frames/samples and DSP
cycles.

> [!NOTE]
> Detailed diagnostics add logging and timing-ring sorting. They are useful for
> an A/B investigation but can perturb the tail being observed. Pass
> `--lightweight` for the low-cost snapshots used by the support matrix.

## Run it

The custom QEMU must already be built and the requested update and sound assets
must exist. The harness itself uses only Python's standard library.

```bash
python3 docs/measurements/dcs-engines/run-comparison.py
```

The default six-engine run has nine minutes of requested runtime plus boot,
shutdown and possible cache-generation overhead. Select a slice by repeating
`--engine`:

```bash
python3 docs/measurements/dcs-engines/run-comparison.py \
  --game swe1 --update 0210 \
  --engine adsp --engine adsp-hybrid-thread \
  --output /tmp/p2k-dcs-adsp-ab
```

Use a new output directory. The tool refuses to overwrite a non-empty one.
Without `--output`, it creates `/tmp/p2k-dcs-comparison-TIMESTAMP`.

## Artifacts and interpretation

The output contains:

- `metadata.json`: commit, dirty-state flag, runner SHA-256, arguments and
  selected engines;
- one raw `<engine>.log` per engine;
- `report.md`: delivery, jitter, PDB distribution and live-DSP health tables.

Sample engines show `n/a` in the live-health table because their mixer does not
use the live ADSP queue. `MISSING` or `FAIL` for a live engine requires log
inspection; a timing comparison with broken audio is not a valid result.

The tool deliberately has no combined support verdict. A high detailed-mode
PDB tail can include observer cost, while a good timing row does not prove game
identity across other revisions, pixel correctness or audible sound quality.
See [DCS sound](../../25-dcs-sound.md) for the engine contracts.

## Rebuild a report

To summarize preserved compatible logs without launching QEMU:

```bash
python3 docs/measurements/dcs-engines/run-comparison.py \
  --parse-only /tmp/p2k-dcs-comparison-TIMESTAMP
```

For an older directory containing only a subset of engines, repeat the matching
`--engine` arguments. Parsing does not manufacture missing health records;
historical live logs without the record remain visibly `MISSING`.
