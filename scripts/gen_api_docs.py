#!/usr/bin/env python3
"""Generate local API/reference docs via Doxygen.

Requirements:
  - doxygen
  - dot (Graphviz) is optional but enables richer diagrams

Usage:
    python scripts/gen_api_docs.py
    python scripts/gen_api_docs.py --output docs/api
    python scripts/gen_api_docs.py --dry-run
"""
from __future__ import annotations

import argparse
import platform
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def find_tool(name: str, *, optional: bool = False) -> str | None:
    path = shutil.which(name)
    if path:
        return path

    if platform.system() == "Windows":
        win_candidates: dict[str, list[Path]] = {
            "doxygen": [
                Path(r"C:\Program Files\doxygen\bin\doxygen.exe"),
                Path(r"C:\Program Files (x86)\doxygen\bin\doxygen.exe"),
            ],
            "dot": [
                Path(r"C:\Program Files\Graphviz\bin\dot.exe"),
                Path(r"C:\Program Files (x86)\Graphviz\bin\dot.exe"),
            ],
        }
        for candidate in win_candidates.get(name, []):
            if candidate.exists():
                print(f"  Found {name} at {candidate} (not on PATH — consider restarting your terminal)")
                return str(candidate)

    if optional:
        return None

    install_hints = {
        "doxygen": (
            "  macOS:   brew install doxygen\n"
            "  Ubuntu:  apt install doxygen\n"
            "  Windows: winget install DimitriVanHeesch.Doxygen  (then restart your terminal)"
        ),
        "dot": (
            "  macOS:   brew install graphviz\n"
            "  Ubuntu:  apt install graphviz\n"
            "  Windows: winget install Graphviz.Graphviz  (then restart your terminal)"
        ),
    }
    raise RuntimeError(f"'{name}' not found.\n" + install_hints.get(name, f"  Please install '{name}'."))


def run(cmd: list[str], *, cwd: Path | None = None, dry_run: bool) -> None:
    print(f"> {' '.join(str(c) for c in cmd)}")
    if not dry_run:
        subprocess.run(cmd, cwd=cwd, check=True)


def copy_tree(src: Path, dst: Path, *, dry_run: bool) -> None:
    if not src.exists():
        return
    print(f"> copy {src} -> {dst}")
    if dry_run:
        return
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)


def copy_generated_assets(repo_root: Path, output_dir: Path, *, dry_run: bool) -> None:
    asset_dirs = [
        ("screenshots", "screenshots"),
        ("docs/deps", "docs/deps"),
        ("docs/uml", "docs/uml"),
        ("tests/render/reference", "tests/render/reference"),
    ]
    for src_rel, dst_rel in asset_dirs:
        copy_tree(repo_root / src_rel, output_dir / dst_rel, dry_run=dry_run)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", default="docs/api", help="Output directory (default: docs/api)")
    parser.add_argument("--dry-run", action="store_true", help="Print commands without executing them")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    output_dir = (repo_root / args.output).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    doxygen = find_tool("doxygen")
    dot = find_tool("dot", optional=True)
    if dot is None:
        print("warning: 'dot' not found; Doxygen will run without Graphviz-backed diagrams")

    base_config = (repo_root / "Doxyfile").read_text(encoding="utf-8")
    output_parent = output_dir.parent.as_posix()
    html_output = output_dir.name

    overrides = [
        f"OUTPUT_DIRECTORY = {output_parent}",
        f"HTML_OUTPUT = {html_output}",
        f"HAVE_DOT = {'YES' if dot else 'NO'}",
        f"STRIP_FROM_PATH = {repo_root.as_posix()}",
    ]
    if dot:
        overrides.append(f"DOT_PATH = {Path(dot).resolve().parent.as_posix()}")

    rendered_config = base_config + "\n\n# Overrides injected by scripts/gen_api_docs.py\n" + "\n".join(overrides) + "\n"

    with tempfile.TemporaryDirectory(prefix="draxul-doxygen-") as temp_dir:
        config_path = Path(temp_dir) / "Doxyfile"
        config_path.write_text(rendered_config, encoding="utf-8")
        run([str(doxygen), str(config_path)], cwd=repo_root, dry_run=args.dry_run)
        copy_generated_assets(repo_root, output_dir, dry_run=args.dry_run)

    if not args.dry_run:
        index = output_dir / "index.html"
        print(f"\nAPI docs written to:\n  {index}")
        system = platform.system()
        if index.exists():
            if system == "Darwin":
                print(f"\nOpen with:  open {index}")
            elif system == "Windows":
                print(f"\nOpen with:  start {index}")
            else:
                print(f"\nOpen with:  xdg-open {index}")

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)
