#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


def resolve_path(path_str: str, must_exist: bool = False) -> Path:
    path = Path(path_str).expanduser()
    if not path.is_absolute():
        path = Path.cwd() / path
    path = path.resolve()
    if must_exist and not path.exists():
        raise FileNotFoundError(f"Path does not exist: {path}")
    return path


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run a saved prompt file through gemini and write the response to a file."
    )
    parser.add_argument("--prompt-file", required=True, help="Path to the prompt markdown/text file.")
    parser.add_argument("--output-file", required=True, help="Path to write the final agent response.")
    parser.add_argument("--model", default="flash", help="Gemini model name to use. Default: flash")
    parser.add_argument("--working-dir", help="Working directory for gemini. Defaults to repo root.")
    parser.add_argument(
        "--full-auto",
        action="store_true",
        help="Use --approval-mode=yolo to skip tool confirmation prompts.",
    )
    parser.add_argument(
        "--prepend-file",
        action="append",
        default=[],
        help="File to append to the prompt as additional startup context. Repeatable.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the resolved command and exit without running gemini.",
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    gemini = shutil.which("gemini")
    if not gemini:
        raise RuntimeError("gemini CLI is not on PATH.")

    repo_root = Path(__file__).resolve().parent.parent
    prompt_file = resolve_path(args.prompt_file, must_exist=True)
    output_file = resolve_path(args.output_file)
    working_dir = resolve_path(args.working_dir, must_exist=True) if args.working_dir else repo_root

    output_file.parent.mkdir(parents=True, exist_ok=True)
    prompt_text = prompt_file.read_text(encoding="utf-8")

    for prepend_file in args.prepend_file:
        prepend_path = resolve_path(prepend_file, must_exist=True)
        prepend_text = prepend_path.read_text(encoding="utf-8", errors="replace")
        prompt_text = "\n\n".join(
            [
                prompt_text,
                f"Attached repository context from `{prepend_path}` follows.",
                prepend_text,
            ]
        )

    command = [
        gemini,
        "--model", args.model,
        "--output-format", "text",
    ]

    if args.full_auto:
        command.extend(["--approval-mode", "yolo"])

    # gemini takes the query as a positional argument or from stdin if "-" is used
    command.append("-")

    if args.dry_run:
        print(f"Prompt file : {prompt_file}")
        print(f"Output file : {output_file}")
        print(f"Working dir : {working_dir}")
        print(f"Model       : {args.model}")
        if args.prepend_file:
            print("Prepend     : " + ", ".join(str(resolve_path(path, must_exist=True)) for path in args.prepend_file))
        print("Command     : " + " ".join(command))
        return 0

    completed = subprocess.run(
        command,
        input=prompt_text,
        text=True,
        capture_output=True,
        cwd=working_dir,
        check=False,
        encoding="utf-8",
        errors="replace",
    )

    if completed.returncode != 0:
        if completed.stderr:
            print(completed.stderr, file=sys.stderr)
        raise RuntimeError(f"gemini failed with exit code {completed.returncode}.")

    output_file.write_text(completed.stdout, encoding="utf-8")
    print(f"Wrote agent output to {output_file}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(exc, file=sys.stderr)
        raise SystemExit(1)
