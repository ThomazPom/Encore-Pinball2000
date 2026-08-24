#!/usr/bin/env python3
"""Select, apply and source-validate Encore's versioned QEMU patch families."""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request
from collections import defaultdict
from pathlib import Path


VERSION_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)(?:-rc(\d+))?$")
PATCH_RE = re.compile(
    r"^qemu-(\d+\.\d+\.\d+(?:-rc\d+)?)"
    r"(?:_to_(\d+\.\d+\.\d+(?:-rc\d+)?))?\.patch$"
)
DEFAULT_PATCH_ROOT = Path(__file__).resolve().parents[2] / "qemu/upstream-patches"


def version_key(text: str) -> tuple[int, int, int, int, int]:
    match = VERSION_RE.fullmatch(text)
    if not match:
        raise ValueError(f"invalid QEMU version: {text}")
    major, minor, micro = (int(match.group(i)) for i in range(1, 4))
    rc = match.group(4)
    return major, minor, micro, 0 if rc is not None else 1, int(rc or 0)


def patch_range(path: Path) -> tuple[str, str]:
    match = PATCH_RE.fullmatch(path.name)
    if not match:
        raise ValueError(
            f"{path}: expected qemu-VERSION.patch or "
            "qemu-VERSION_to_VERSION.patch"
        )
    return match.group(1), match.group(2) or match.group(1)


def covers(path: Path, version: str) -> bool:
    start, end = patch_range(path)
    # A stable-release range never implicitly claims release candidates that
    # sort between its endpoints. RC compatibility must be named and tested
    # explicitly.
    if "-rc" in version and "-rc" not in start and "-rc" not in end:
        return False
    key = version_key(version)
    return version_key(start) <= key <= version_key(end)


def families(root: Path) -> list[Path]:
    series = root / "series"
    if not series.is_file():
        raise RuntimeError(f"missing patch series: {series}")
    names = [
        line.strip()
        for line in series.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    if len(names) != len(set(names)):
        raise RuntimeError(f"duplicate family in {series}")
    result = []
    for name in names:
        if "/" in name or name in {".", ".."}:
            raise RuntimeError(f"invalid family name in {series}: {name}")
        directory = root / name
        if not directory.is_dir():
            raise RuntimeError(f"series family is not a directory: {directory}")
        result.append(directory)
    unlisted = sorted(
        entry.name for entry in root.iterdir()
        if entry.is_dir() and entry.name not in set(names)
    )
    if unlisted:
        raise RuntimeError(f"patch families missing from {series}: {', '.join(unlisted)}")
    return result


def variants(family: Path) -> list[Path]:
    result = sorted(family.glob("*.patch"))
    if not result:
        raise RuntimeError(f"patch family has no variants: {family}")
    for path in result:
        start, end = patch_range(path)
        if version_key(start) > version_key(end):
            raise RuntimeError(f"reversed compatibility range: {path}")
    return result


def declared_variants(root: Path, version: str) -> list[Path]:
    selected = []
    for family in families(root):
        matches = [path for path in variants(family) if covers(path, version)]
        if not matches:
            raise RuntimeError(
                f"{family.name}: no variant declares QEMU {version} compatibility"
            )
        if len(matches) != 1:
            raise RuntimeError(
                f"{family.name}: overlapping declarations for QEMU {version}: "
                + ", ".join(path.name for path in matches)
            )
        selected.append(matches[0])
    return selected


def patch_command(source: Path, patch: Path, dry_run: bool) -> list[str]:
    command = [
        "patch", "-d", str(source), "-p1", "--forward", "--silent", "--fuzz=0",
    ]
    if dry_run:
        command.append("--dry-run")
    command.extend(["-i", str(patch.resolve())])
    return command


def applies(source: Path, patch: Path) -> bool:
    return subprocess.run(
        patch_command(source, patch, True),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    ).returncode == 0


def apply_one(source: Path, patch: Path) -> None:
    subprocess.run(patch_command(source, patch, False), check=True)


def choose_and_apply(
    root: Path, source: Path, version: str, discover: bool = False
) -> list[Path]:
    selected = []
    declared = None if discover else declared_variants(root, version)
    for index, family in enumerate(families(root)):
        candidates = variants(family) if discover else [declared[index]]
        applicable = [path for path in candidates if applies(source, path)]
        if discover and len(applicable) > 1:
            declared = [path for path in applicable if covers(path, version)]
            if len(declared) == 1:
                applicable = declared
        if not applicable:
            names = ", ".join(path.name for path in candidates) or "none"
            raise RuntimeError(
                f"{family.name}: no zero-fuzz variant applies to QEMU {version} "
                f"(candidates: {names})"
            )
        if len(applicable) != 1:
            raise RuntimeError(
                f"{family.name}: ambiguous variants for QEMU {version}: "
                + ", ".join(path.name for path in applicable)
            )
        chosen = applicable[0]
        print(f"[qemu-patches] {family.name}: {chosen.name}")
        apply_one(source, chosen)
        selected.append(chosen)
    return selected


def remote_versions(mirror: str, include_rc: bool) -> list[str]:
    with urllib.request.urlopen(mirror.rstrip("/") + "/") as response:
        listing = response.read().decode("utf-8", errors="replace")
    suffix = r"(?:-rc\d+)?" if include_rc else ""
    found = set(re.findall(rf"qemu-(\d+\.\d+\.\d+{suffix})\.tar\.xz", listing))
    return sorted(found, key=version_key)


def requested_versions(args: argparse.Namespace) -> list[str]:
    if args.versions:
        if args.first or args.last:
            raise RuntimeError("use positional versions or --from/--to, not both")
        result = sorted(set(args.versions), key=version_key)
    else:
        if not args.first or not args.last:
            raise RuntimeError("provide versions or both --from and --to")
        low, high = version_key(args.first), version_key(args.last)
        if low > high:
            raise RuntimeError("--from must not be newer than --to")
        result = [
            version for version in remote_versions(args.mirror, args.include_rc)
            if low <= version_key(version) <= high
        ]
    if not result:
        raise RuntimeError("version selection is empty")
    for version in result:
        version_key(version)
    return result


def fetch_and_extract(version: str, mirror: str, cache: Path, work: Path) -> Path:
    archive = cache / f"qemu-{version}.tar.xz"
    cache.mkdir(parents=True, exist_ok=True)
    if not archive.is_file():
        url = f"{mirror.rstrip('/')}/{archive.name}"
        temporary = archive.with_suffix(archive.suffix + ".part")
        print(f"[qemu-patches] downloading {url}")
        urllib.request.urlretrieve(url, temporary)
        temporary.replace(archive)
    # Use the same extractor as build-qemu.sh. Official QEMU archives contain
    # an intentional absolute symlink under the edk2 submodule which Python's
    # generic safe-extraction filter rejects.
    subprocess.run(["tar", "-xf", str(archive), "-C", str(work)], check=True)
    source = work / f"qemu-{version}"
    if not source.is_dir():
        raise RuntimeError(f"archive did not create {source.name}")
    return source


def compatible_name(versions: list[str]) -> str:
    ordered = sorted(versions, key=version_key)
    if len(ordered) == 1:
        return f"qemu-{ordered[0]}.patch"
    return f"qemu-{ordered[0]}_to_{ordered[-1]}.patch"


def update_names(
    patch_root: Path, tested: list[str], selected: dict[Path, list[str]]
) -> None:
    tested_index = {version: index for index, version in enumerate(tested)}
    moves: list[tuple[Path, Path]] = []
    for family in families(patch_root):
        for patch in variants(family):
            matched = sorted(selected.get(patch, []), key=version_key)
            if not matched:
                continue
            indexes = [tested_index[version] for version in matched]
            if indexes != list(range(indexes[0], indexes[-1] + 1)):
                raise RuntimeError(
                    f"{patch}: compatibility is discontinuous in the tested range; "
                    "split it into separate variants"
                )
            destination = patch.with_name(compatible_name(matched))
            if destination != patch:
                moves.append((patch, destination))
    destinations = [destination for _, destination in moves]
    if len(destinations) != len(set(destinations)):
        raise RuntimeError("compatibility-name update would create duplicate filenames")
    for source, destination in moves:
        if destination.exists() and destination != source:
            raise RuntimeError(f"refusing to overwrite {destination}")
    for source, destination in moves:
        print(f"[qemu-patches] rename {source.name} -> {destination.name}")
        source.rename(destination)


def command_apply(args: argparse.Namespace) -> int:
    version_key(args.version)
    choose_and_apply(args.patch_root.resolve(), args.source.resolve(), args.version)
    return 0


def command_fingerprint(args: argparse.Namespace) -> int:
    version_key(args.version)
    root = args.patch_root.resolve()
    digest = hashlib.sha256()
    selected = declared_variants(root, args.version)
    inputs = [root / "series", *selected, Path(__file__).resolve()]
    for path in inputs:
        digest.update(str(path.relative_to(root) if path.is_relative_to(root) else path.name).encode())
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    print(digest.hexdigest())
    return 0


def command_validate(args: argparse.Namespace) -> int:
    if args.update_names and len(args.versions) > 1:
        raise RuntimeError(
            "--update-names with multiple explicit versions could claim untested "
            "releases between them; use an inclusive --from/--to range"
        )
    versions = requested_versions(args)
    selected: dict[Path, list[str]] = defaultdict(list)
    failures: list[tuple[str, str]] = []
    for version in versions:
        print(f"\n[qemu-patches] validating QEMU {version}")
        with tempfile.TemporaryDirectory(prefix=f"p2k-qemu-{version}-") as temporary:
            try:
                source = fetch_and_extract(
                    version, args.mirror, args.cache_dir.expanduser(), Path(temporary)
                )
                chosen = choose_and_apply(
                    args.patch_root.resolve(), source, version, discover=True
                )
                for patch in chosen:
                    selected[patch].append(version)
            except Exception as error:  # report the whole requested range
                failures.append((version, str(error)))
                print(f"[qemu-patches] FAIL {version}: {error}", file=sys.stderr)
            else:
                print(f"[qemu-patches] PASS {version}")
    print("\nQEMU source compatibility")
    for version in versions:
        failure = next((message for item, message in failures if item == version), None)
        print(f"  {version:16} {'FAIL: ' + failure if failure else 'PASS'}")
    if failures:
        return 1
    if args.update_names:
        update_names(args.patch_root.resolve(), versions, selected)
    return 0


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    subcommands = result.add_subparsers(dest="command", required=True)

    apply_parser = subcommands.add_parser("apply", help="apply the declared patch series")
    apply_parser.add_argument("--patch-root", type=Path, default=DEFAULT_PATCH_ROOT)
    apply_parser.add_argument("--source", type=Path, required=True)
    apply_parser.add_argument("--version", required=True)
    apply_parser.set_defaults(function=command_apply)

    fingerprint = subcommands.add_parser(
        "fingerprint", help="hash the variants selected for one QEMU version"
    )
    fingerprint.add_argument("--patch-root", type=Path, default=DEFAULT_PATCH_ROOT)
    fingerprint.add_argument("--version", required=True)
    fingerprint.set_defaults(function=command_fingerprint)

    validate = subcommands.add_parser(
        "validate", help="test patch bodies against clean QEMU source releases"
    )
    validate.add_argument("versions", nargs="*")
    validate.add_argument("--from", dest="first")
    validate.add_argument("--to", dest="last")
    validate.add_argument("--include-rc", action="store_true")
    validate.add_argument("--update-names", action="store_true")
    validate.add_argument("--patch-root", type=Path, default=DEFAULT_PATCH_ROOT)
    validate.add_argument(
        "--cache-dir", type=Path,
        default=Path("~/.cache/p2k-qemu-patch-validation"),
    )
    validate.add_argument("--mirror", default="https://download.qemu.org")
    validate.set_defaults(function=command_validate)
    return result


def main() -> int:
    try:
        if hasattr(sys.stdout, "reconfigure"):
            sys.stdout.reconfigure(line_buffering=True)
        args = parser().parse_args()
        return args.function(args)
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f"[qemu-patches] ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
