#!/usr/bin/env python3
from __future__ import annotations

import platform
import subprocess
import sys
from pathlib import Path


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    prompt_file = repo_root / "plans" / "prompts" / "review_bugs.md"
    output_file = repo_root / "plans" / "reviews" / "review-bugs-latest.md"
    ask_agent = repo_root / "scripts" / "ask_agent.py"

    forwarded_args = sys.argv[1:]
    if "--prompt-file" in forwarded_args:
        raise SystemExit("do_review_bugs.py fixes --prompt-file to plans/prompts/review_bugs.md; do not pass it explicitly.")

    if "--output-file" not in forwarded_args:
        forwarded_args = ["--output-file", str(output_file), *forwarded_args]

    if "--interactive" not in forwarded_args and "--parallel" not in forwarded_args:
        forwarded_args = ["--parallel", *forwarded_args]

    if platform.system() == "Darwin" and "--with-gemini" not in forwarded_args:
        forwarded_args = ["--with-gemini", *forwarded_args]

    if "--gpt-review-safe" not in forwarded_args:
        forwarded_args = ["--gpt-review-safe", *forwarded_args]

    command = [
        sys.executable,
        str(ask_agent),
        "--prompt-file",
        str(prompt_file),
        *forwarded_args,
    ]

    completed = subprocess.run(command, cwd=repo_root, check=False)
    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
