# 47 — Release process

Encore publishes its custom QEMU separately from the source checkout. The
installer can therefore offer a fast verified download without turning the
binary into the authoritative version of the project. A local build remains
the recommended path and always uses the exact checked-out sources.

## Automatic publication

`.github/workflows/release.yml` builds in Debian 13 and publishes a fixed-name
x86-64 archive plus its SHA-256 checksum. It runs when relevant QEMU sources or
build integration change on `main`, and once each Monday. A scheduled run is
skipped when the latest release already represents the current commit.

Automatic releases receive a dated `v0.YYYYMMDD.RUN` tag. A pushed semantic
version tag such as `v1.0.0` publishes that tag instead. The workflow can also
be started manually with an optional semantic version tag; leaving it blank
uses the dated form.

The archive contains:

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
```

This makes regular publication useful without making old installer versions
aware of every future tag.

## Manual validation

After publication, exercise the same path used by the installer:

```sh
tmpdir=$(mktemp -d)
scripts/download-qemu-release.sh --destination "$tmpdir"
"$tmpdir/qemu-system-i386" -M help | grep pinball2000
rm -rf -- "$tmpdir"
```

For a release candidate intended for cabinet use, also run the Debian lab and
the applicable graphics, audio, input and LPT validation. Successful CI proves
the artifact is buildable and internally coherent; it does not replace a real
cabinet test.
