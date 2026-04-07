#!/usr/bin/env python3
"""Build all project documentation artifacts into docs/.

Runs:
  1. scripts/gen_deps.py  →  docs/deps/deps.svg   (CMake target dependency graph)
  2. scripts/gen_uml.py   →  docs/uml/*.svg        (C++ class UML diagrams)
  3. scripts/gen_api_docs.py → docs/api/index.html (local Doxygen API/reference docs)

Requirements: cmake, dot (graphviz), clang-uml, plantuml, doxygen
Usage:
    python scripts/build_docs.py                  # build everything
    python scripts/build_docs.py --deps-only
    python scripts/build_docs.py --uml-only
    python scripts/build_docs.py --api-only
    python scripts/build_docs.py --dry-run        # print commands without running
    python scripts/build_docs.py --format svg     # UML render format (default: svg)
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def run_script(script: Path, extra_args: list[str], dry_run: bool) -> int:
    cmd = [sys.executable, str(script)] + extra_args
    print(f"\n{'='*60}")
    print(f"  {script.name}")
    print(f"{'='*60}")
    print(f"> {' '.join(cmd)}\n")
    if dry_run:
        return 0
    result = subprocess.run(cmd)
    return result.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--deps-only", action="store_true", help="Only run gen_deps.py")
    parser.add_argument("--uml-only",  action="store_true", help="Only run gen_uml.py")
    parser.add_argument("--api-only",  action="store_true", help="Only run gen_api_docs.py")
    parser.add_argument(
        "--format", default="svg", choices=["png", "svg", "pdf"],
        help="UML render format (default: svg)",
    )
    parser.add_argument("--dry-run", action="store_true", help="Print commands without executing them")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    scripts   = repo_root / "scripts"

    selected_only = args.deps_only or args.uml_only or args.api_only
    run_deps = args.deps_only or not selected_only
    run_uml  = args.uml_only or not selected_only
    run_api  = args.api_only or not selected_only

    errors: list[str] = []

    if run_deps:
        rc = run_script(
            scripts / "gen_deps.py",
            [
                "--output", "docs/deps",
                "--format", "svg",
                "--prune", "SDL3-static",
                "--prune", "freetype",
                "--prune", "harfbuzz",
            ] + (["--dry-run"] if args.dry_run else []),
            dry_run=args.dry_run,
        )
        if rc != 0:
            errors.append("gen_deps.py failed")

    if run_uml:
        rc = run_script(
            scripts / "gen_uml.py",
            [
                "--format", args.format,
            ] + (["--dry-run"] if args.dry_run else []),
            dry_run=args.dry_run,
        )
        if rc != 0:
            errors.append("gen_uml.py failed")

    if run_api:
        rc = run_script(
            scripts / "gen_api_docs.py",
            [
                "--output", "docs/api",
            ] + (["--dry-run"] if args.dry_run else []),
            dry_run=args.dry_run,
        )
        if rc != 0:
            errors.append("gen_api_docs.py failed")

    print()
    if errors:
        for e in errors:
            print(f"  error: {e}", file=sys.stderr)
        return 1

    if not args.dry_run:
        print("Docs built:")
        for f in sorted((repo_root / "docs").rglob("*")):
            if f.is_file() and f.suffix in {".svg", ".png", ".pdf", ".puml"}:
                print(f"  {f.relative_to(repo_root)}")
        api_index = repo_root / "docs" / "api" / "index.html"
        if api_index.exists():
            print(f"  {api_index.relative_to(repo_root)}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
