# Community updates (mypinballs.com)

After Williams shipped the last official Pinball 2000 firmware in
September 2003, the platform has continued to be maintained by
**Jim Askey** at <https://mypinballs.com>. He produces newer
firmware revisions for both titles that add bug fixes, audio fixes,
new lighting/colour effects, and other gameplay refinements that the
community has wanted for years.

Encore knows about these bundles, supports them at runtime, and
includes them in its compatibility/testing matrices — they are not
"unsupported." The only thing Encore does **not** do is redistribute
the bundle files, at Jim's request. Please grab the latest builds
from his site directly so the version numbers you see are always the
current ones, and so the project that is keeping these games alive
stays supported.

## Where to get them

* **Latest updates (SWE1 and RFM):** <https://mypinballs.com>
* **Hardware upgrades, replacement parts, donations / licence
  options for software-only users:** also on the same site.

If you have an original cabinet, supporting Jim is also the way to
get the hardware spares and licences you may need.

## Versions referenced in Encore docs

The following community versions are mentioned by name in the
testing matrix, ROM-loading pipeline, update-loader and tools
documentation. They behave correctly in Encore once you drop them
into `./updates/`:

| Game | Version | Bundle directory pattern              |
|------|--------:|---------------------------------------|
| SWE1 | v2.10   | `pin2000_50069_0210_*_B_10000000`     |
| RFM  | v2.50   | `pin2000_50070_0250_*_B_10000000`     |
| RFM  | v2.60   | `pin2000_50070_0260_*_B_10000000`     |

The version field follows the same `vvvv = major*100 + minor*10`
convention as the official Williams bundles, so `--update 2.1`,
`--update 210` and `--update 0210` all resolve to the same
directory.

## How to install

1. Download the bundle archive from <https://mypinballs.com>.
2. Extract it so the directory name matches the
   `pin2000_<gid>_<vvvv>_<date>_B_10000000` convention above.
3. Drop it into `./updates/` next to the official bundles.
4. Run Encore as usual, e.g.:

   ```sh
   ./build/encore --game rfm --update 2.6
   ./build/encore --game swe1 --update latest   # picks the highest version present
   ```

   `--update latest` will pick the highest version field for the
   selected game, so once you add a v2.x community bundle it
   automatically becomes "latest" for that title.

## DCS sound, savedata, symbol tables

* **DCS sound** works on community updates the same way it does on
  official Williams updates — the default `io-handled` path activates
  DCS via the game's natural post-`xinu_ready` probe, which has been
  validated on every bundle (community and official) tested. See
  [22-dcs-detection-and-modes.md](22-dcs-detection-and-modes.md).
* **Savedata** lives in `./savedata/<game>.*` and is shared between
  versions of the same game; switching between official and
  community versions does not require deleting it. If you do hit a
  checksum mismatch after switching, delete the matching
  `savedata/<game>.nvram` file. See [10-savedata.md](10-savedata.md).
* **Symbol tables** are stripped on community v2.x bundles in the
  same way as on RFM v1.6 and SWE1 v1.5 — exported API names only.
  `tools/sym_dump.py` handles both cases. See
  [29-tools-sym-dump.md](29-tools-sym-dump.md).

## Known quirks

* **Attract-screen wall-clock (community v2.1).** Both community v2.1
  firmwares draw a wall-clock in the lower-right of the attract
  screen. The displayed year, day-of-week and AM/PM may not match
  the host system clock that Encore feeds through the CMOS/RTC
  registers. Encore feeds the true host time exactly as Williams'
  BIOS expected; this is a community-firmware quirk, not an emulator
  bug. See [38-known-limitations.md](38-known-limitations.md) for
  the empirical CMOS-register table.

If you find other quirks, please report them to Jim at
<https://mypinballs.com> as well as opening an issue on Encore — it
helps tell apart "Encore bug" from "community-firmware quirk."

---

← [Back to documentation index](README.md) · [Back to project README](../README.md)
