# DCS engine timing comparison

`run-comparison.py` reproduces the standard DCS workload against `pb2kslib`,
the synchronous ADSP engine, and the threaded ADSP engine. Each engine runs
sequentially so they do not compete for host CPU time.

The defaults are SWE1 update 2.10, a 90-second run, and this input sequence
beginning exactly 11 seconds after the wrapper is launched:

1. F4 to open the coin door.
2. Three credit/coin pulses.
3. Twenty alternating volume-up and volume-down pulses.

Run it from anywhere in the repository:

```sh
docs/measurements/dcs-engines/run-comparison.py
```

It writes the three raw logs and `report.md` to a timestamped directory under
`/tmp`. Use `--output DIR` to retain them somewhere else. The generated tables
use full timing-audit `snap` windows at or after 30 seconds; the partial exit
window is excluded from current delivery.

To regenerate a report without rerunning QEMU:

```sh
docs/measurements/dcs-engines/run-comparison.py --parse-only /tmp/p2k-dcs-comparison-TIMESTAMP
```

The script uses only Python's standard library. It expects the custom QEMU to
have already been built with `scripts/build-qemu.sh` and the requested update
and sound ROM assets to be present.
