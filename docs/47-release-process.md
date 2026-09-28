# Release process

Encore publishes an x86_64 Linux end-user archive built by
`.github/workflows/release.yml`. The archive contains the custom QEMU binary,
installer, runner, runtime scripts and documentation. It deliberately excludes
ROMs, updates, savedata, development sources, tests and repository history.

> [!WARNING]
> A technically reproducible archive is not automatically legally
> redistributable. The repository has no project-level licence or complete
> third-party asset notice inventory. Resolve that gate before calling a
> release generally distributable.

## Published artifacts

Each GitHub release contains:

```text
encore-pinball2000-linux-x86_64.tar.gz
encore-pinball2000-linux-x86_64.tar.gz.sha256
```

The archive expands to `Encore-Pinball2000/` with:

- `qemu-system-i386`, stripped but rechecked for the `pinball2000` machine;
- `build-info.txt` with release, commit, QEMU version, UTC build time and build
  system;
- `runtime-packages.txt`, derived from the linked libraries;
- `README.md`, `docs/`, installer and uninstaller;
- the end-user launcher, demos and runtime helpers required by it.

It must not contain `qemu/`, `scripts/build-qemu.sh`, `tools/`, tests, ROMs,
updates or savedata. Assets are acquired through the normal first-run path if
the corresponding directories are absent.

> [!NOTE]
> This exclusion also keeps the recovered legacy tournament server out of the
> end-user archive. Its missing upstream licence and Python 2 laboratory status
> are recorded in its `NOTICE.md`; do not add it to the release allow-list
> without resolving both.

## Triggers and tag selection

The release workflow runs on relevant pushes to `main`, semantic-looking
`v*.*.*` tags, a weekly schedule and manual dispatch.

| Trigger | Release tag |
|---|---|
| pushed tag | the pushed tag |
| manual dispatch with `tag` | normalized to lowercase leading `v`; an existing tag is checked out |
| main push or manual dispatch without tag | `v0.YYYYMMDD.RUN_NUMBER` |
| weekly schedule | same automatic form, unless the latest release already points at the commit |

Accepted tags match `vX.Y.Z` with an optional dotted/dashed suffix. The release
job is serialized and is not cancelled by a newer run.

> [!IMPORTANT]
> The workflow can replace assets on an existing release with `--clobber`.
> Reusing a tag therefore needs an explicit maintainer decision; consumers
> should retain the checksum and commit recorded at acquisition time.

## Publication pipeline

The job uses Debian trixie on x86_64 and performs these gates in order:

1. install pinned build/runtime dependencies;
2. fetch the selected commit without submodule recursion;
3. validate the tag and publication decision;
4. syntax-check shell entry points, run all Python units, check guest-extension
   ROM support and compile selected Python tools;
5. build the pinned QEMU 10.0.8 machine;
6. verify machine registration and the volatile-extension marker;
7. run the real switch-keymap smoke test;
8. copy and strip QEMU, then repeat registration/marker/library checks;
9. construct the allow-listed end-user tree with `git archive`;
10. produce SHA-256, re-extract the archive and assert required and forbidden
    paths;
11. run the packaged binary's `-M help` and packaged launcher's `--help`;
12. create the GitHub release or replace its two assets.

A source-compatible patch range or successful compiler run alone cannot pass
these gates.

## Reproduce before publishing

On a clean supported x86_64 Debian-like host:

```bash
bash -n install.sh uninstall.sh scripts/run-qemu.sh scripts/build-qemu.sh \
  scripts/internal/encore-session.sh tools/capture-live-crash.sh \
  tools/debian-qemu/lab.sh
python3 -m unittest discover -s scripts/tests -p 'test_*.py'
python3 guest-extensions/check-romset.py
scripts/build-qemu.sh
```

Then check the normal interactive game path and the packaging path relevant to
the change. Installation, display-manager or acquisition changes require the
disposable [Debian cabinet lab](../tools/debian-qemu/README.md), including
`test-release` and the appropriate `test-acquire` case.

For emulator behavior changes, attach the evidence required by the
[validation guide](26-testing-validation-matrix.md): exact game/update, command,
duration, exit reason and relevant timing/IRQ/IStack measurements. Do not
promote a release from benchmark data alone.

## Consumer verification

Download both assets into an empty directory and verify before extraction:

```bash
sha256sum -c encore-pinball2000-linux-x86_64.tar.gz.sha256
tar -xzf encore-pinball2000-linux-x86_64.tar.gz
cd Encore-Pinball2000
./scripts/run-qemu.sh --help
```

The automated downloader performs the same checksum check, requires x86_64,
extracts to a temporary directory, verifies the executable and machine when
host libraries allow it, then atomically replaces the cached binary:

```bash
scripts/internal/download-qemu-release.sh
```

Its default destination is
`${XDG_CACHE_HOME:-$HOME/.cache}/encore-qemu-release`. A custom mirror or fork
may be selected with `ENCORE_RELEASE_BASE_URL` or
`ENCORE_RELEASE_REPOSITORY`.

The checksum travels beside the archive from the same GitHub release. It
detects corruption or mismatch, but it is not an independent signature. For a
high-trust deployment, also pin the release tag/commit and authenticate the
publication channel.

## Asset acquisition boundary

The release does not embed ROMs or update trees. On first launch,
`fetch-assets-if-missing.sh` shallow-clones the configured repository into a
temporary directory and installs a complete missing `roms/` or `updates/`
tree. Existing directories are left alone.

`P2K_ASSETS_REPO` can redirect that source, but the current path does not
verify a signed manifest of individual assets. Operators who require
reproducibility should provision audited trees themselves and retain their
hash inventory. See [update provenance](47-community-updates.md).

## Release checklist

- [ ] final commit and intended tag are identified;
- [ ] working tree contains no local payloads or investigation artifacts;
- [ ] project and third-party licensing decision is recorded;
- [ ] CI baseline, build and machine registration pass;
- [ ] normal interactive launch passes with isolated savedata;
- [ ] risk-specific smoke/matrix/benchmark evidence is retained;
- [ ] archive allow-list and forbidden-path assertions still match the runner;
- [ ] extracted archive passes `--help` and machine checks;
- [ ] checksum matches the published bytes;
- [ ] release notes distinguish fixes, experiments and known limitations;
- [ ] rollback means publishing/retaining a known-good prior artifact, not
      silently changing documentation to match a bad binary.

## Known publication limitations

- only Linux x86_64 binaries are published;
- the binary is dynamically linked to packages recorded at build time;
- the companion SHA-256 is not a cryptographic publisher signature;
- asset acquisition depends on Git/network on a fresh archive;
- neither desktop tests nor CI certify a powered physical cabinet;
- project-level licensing/provenance remains a release blocker for broad
  redistribution claims.

---

[Download and quick start](../README.md) · [Installation](01-cabinet-installation.md) · [Development](05-development-guidelines.md) · [Update provenance](47-community-updates.md)
