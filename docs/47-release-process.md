# 47 — Release process

Encore publishes both a complete end-user package and its custom QEMU as a
smaller compatibility download. The complete package can be extracted and run
without first cloning the repository. A local QEMU build remains available and
always uses the exact packaged sources.

## Automatic publication

`.github/workflows/release.yml` builds in Debian 13 and publishes a fixed-name
x86-64 archive plus its SHA-256 checksum. It runs when relevant QEMU sources or
build integration change on `main`, and once each Monday. A scheduled run is
skipped when the latest release already represents the current commit.

Automatic releases receive a dated `v0.YYYYMMDD.RUN` tag. A pushed semantic
version tag such as `v1.0.0` publishes that tag instead. The workflow can also
be started manually with an optional semantic version tag; leaving it blank
uses the dated form.

The complete archive contains:

- `install.sh`, `uninstall.sh` and the complete runtime scripts;
- user and developer documentation;
- the update bundles tracked by the project;
- the ready-to-run stripped `qemu-system-i386` binary;
- release metadata and the generated Debian runtime-package list.

Development-only QEMU sources, build helpers, tests, forensic tools and the
Debian laboratory remain in the source repository and are not copied into the
end-user archive.

It deliberately contains no user ROMs or savedata. The normal runner uses the
existing asset downloader when the ROM directory is absent, while keeping the
updates already included in the package.

The smaller QEMU-only archive contains:

- the stripped `qemu-system-i386` binary;
- the exact release tag, commit and build environment;
- a generated list of Debian runtime packages.

Before publication, the workflow verifies that the stripped binary starts,
has no unresolved libraries and exposes the `pinball2000` machine. The
installer performs the machine check again after download.

## Why the asset name does not contain a version

The release tag provides version history. Keeping the asset name stable lets
the installer use GitHub's `/releases/latest/download/` endpoint without an API
query or a hard-coded release number:

```text
encore-qemu-linux-x86_64.tar.gz
encore-qemu-linux-x86_64.tar.gz.sha256
encore-pinball2000-linux-x86_64.tar.gz
encore-pinball2000-linux-x86_64.tar.gz.sha256
```

This makes regular publication useful without making old installer versions
aware of every future tag.

For an end-user installation, download and extract
`encore-pinball2000-linux-x86_64.tar.gz`, enter its `Encore-Pinball2000`
directory and run `./install.sh`. The QEMU-only asset remains the fixed endpoint
used by older checkouts and by the installer's fast-download option.

## Manual validation

After publication, exercise the same path used by the installer:

```sh
tmpdir=$(mktemp -d)
scripts/internal/download-qemu-release.sh --destination "$tmpdir"
"$tmpdir/qemu-system-i386" -M help | grep pinball2000
rm -rf -- "$tmpdir"
```

For a release candidate intended for cabinet use, also run the Debian lab and
the applicable graphics, audio, input and LPT validation. Successful CI proves
the artifact is buildable and internally coherent; it does not replace a real
cabinet test.
