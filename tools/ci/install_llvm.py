#!/usr/bin/env python3
"""Download and unpack a prebuilt LLVM archive for CI.

The LLVMParty/llvm-builds release assets are ZIP files containing an LLVM
installation root (bin/, include/, lib/, ...). This script downloads one asset,
finds the installation root inside the archive, and installs it at a stable
path used by CMake/scikit-build.
"""

from __future__ import annotations

import argparse
import os
import stat
import shutil
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path
from zipfile import ZipFile


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True, help="LLVM version, e.g. 21.1.6")
    parser.add_argument(
        "--tag",
        help="llvm-builds release tag. Defaults to v<version>.",
    )
    parser.add_argument(
        "--repo",
        default="LLVMParty/llvm-builds",
        help="GitHub repository containing release assets.",
    )
    parser.add_argument(
        "--archive",
        required=True,
        help="Release asset name, e.g. llvm-21.1.6-linux-x86_64.zip",
    )
    parser.add_argument(
        "--dest",
        required=True,
        type=Path,
        help="Destination LLVM root directory.",
    )
    return parser.parse_args()


def download(url: str, dest: Path) -> None:
    print(f"Downloading {url}", flush=True)
    with urllib.request.urlopen(url) as response, dest.open("wb") as out:
        shutil.copyfileobj(response, out)


def extract_zip(archive: Path, dest: Path) -> None:
    """Extract a ZIP archive, preserving Unix symlinks when present."""
    dest.mkdir(parents=True, exist_ok=True)

    # On Unix, prefer unzip because it preserves symlinks and permissions in
    # LLVM archives. Fall back to a Python extractor that handles symlinks too.
    if os.name != "nt" and shutil.which("unzip"):
        subprocess.run(["unzip", "-q", str(archive), "-d", str(dest)], check=True)
        return

    with ZipFile(archive) as zf:
        for info in zf.infolist():
            target = dest / info.filename
            mode = info.external_attr >> 16

            if info.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue

            target.parent.mkdir(parents=True, exist_ok=True)

            if os.name != "nt" and stat.S_ISLNK(mode):
                link_target = zf.read(info).decode()
                if target.exists() or target.is_symlink():
                    target.unlink()
                os.symlink(link_target, target)
                continue

            with zf.open(info) as src, target.open("wb") as out:
                shutil.copyfileobj(src, out)

            if os.name != "nt" and mode:
                target.chmod(mode & 0o777)


def looks_like_llvm_root(path: Path) -> bool:
    exe = "llvm-config.exe" if os.name == "nt" else "llvm-config"
    return (
        (path / "bin" / exe).exists()
        and (path / "include" / "llvm-c").is_dir()
        and (path / "lib").is_dir()
    )


def find_llvm_root(extracted: Path) -> Path:
    if looks_like_llvm_root(extracted):
        return extracted

    candidates = [p for p in extracted.rglob("*") if p.is_dir() and looks_like_llvm_root(p)]
    if not candidates:
        raise RuntimeError(f"Could not find LLVM root in extracted archive: {extracted}")

    # Prefer the shallowest path if the archive contains nested directories.
    return sorted(candidates, key=lambda p: (len(p.parts), str(p)))[0]


def install_tree(src: Path, dest: Path) -> None:
    if dest.exists():
        shutil.rmtree(dest)
    dest.parent.mkdir(parents=True, exist_ok=True)

    # copytree with symlinks=True preserves Unix dylib/so symlink structure.
    shutil.copytree(src, dest, symlinks=True)


def main() -> int:
    args = parse_args()
    tag = args.tag or f"v{args.version}"
    url = f"https://github.com/{args.repo}/releases/download/{tag}/{args.archive}"

    with tempfile.TemporaryDirectory(prefix="llvm-download-") as tmp:
        tmp_path = Path(tmp)
        archive_path = tmp_path / args.archive
        extract_dir = tmp_path / "extract"

        download(url, archive_path)
        extract_zip(archive_path, extract_dir)
        llvm_root = find_llvm_root(extract_dir)
        install_tree(llvm_root, args.dest)

    print(f"Installed LLVM to {args.dest}", flush=True)
    llvm_config = args.dest / "bin" / ("llvm-config.exe" if os.name == "nt" else "llvm-config")
    if llvm_config.exists():
        version = subprocess.check_output([str(llvm_config), "--version"], text=True).strip()
        print(f"LLVM version: {version}", flush=True)

    return 0


if __name__ == "__main__":
    sys.exit(main())
