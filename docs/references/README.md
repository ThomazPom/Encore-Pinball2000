# Encore third-party references

This directory retains external evidence used to understand and verify Pinball
2000. Provenance and upstream rights remain visible; inclusion does not imply
that Encore owns or relicenses the material.

## Collections

| Path | Purpose |
|---|---|
| [`pinball2000/`](pinball2000/README.md) | curated knowledge archive: manuals, historical pages, repair evidence, first-hand development accounts and topic guides |
| `PinballDiag/` | BSD-licensed Linux C++ reference for the original cabinet parallel-port protocol |
| `web_archive/` | small legacy captures retained before the structured archive existed |

The Pinball 2000 archive is the preferred home for new material. It requires a
qualification decision, raw/readable evidence, provenance and SHA-256
validation; it does not accept a file merely because it mentions the platform.

> [!IMPORTANT]
> ROMs, update executables and unrelated emulator packages are not documentation
> artifacts. NuCore documentation and packages are explicitly outside Encore's
> reference archive.

## Adding or refreshing evidence

1. Apply the [qualification method](pinball2000/METHODOLOGY.md).
2. Record whether the candidate is preserved, index-only, duplicated,
   unverified or rejected.
3. Build it with [`reference-bundle.py`](../../tools/reference-preservation/README.md).
4. Validate the bundle and inspect its Markdown, assets and provenance.
5. Link durable knowledge from a topic guide without hiding uncertainty.

[Encore documentation](../README.md) ·
[Pinball 2000 source catalog](pinball2000/CATALOG.md)
