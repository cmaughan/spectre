#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
OUTPUT_FILE = REPO_ROOT / "plans" / "reviews" / "all_code.md"
PREFIX_FILES = [
    "AGENTS.md",
]

CODE_EXTENSIONS = {
    ".bat",
    ".c",
    ".cc",
    ".cmd",
    ".cmake",
    ".comp",
    ".cpp",
    ".cs",
    ".cxx",
    ".frag",
    ".glsl",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".json",
    ".lua",
    ".m",
    ".mdx",
    ".metal",
    ".mm",
    ".ps1",
    ".py",
    ".rs",
    ".sh",
    ".toml",
    ".ts",
    ".tsx",
    ".vert",
    ".yaml",
    ".yml",
}

SPECIAL_FILENAMES = {
    ".clang-format",
    "CMakeLists.txt",
    "CMakePresets.json",
}

EXCLUDED_PREFIXES = (
    "build/",
    "plans/prompts/history/",
)


def language_for(path: Path) -> str:
    if path.name == "CMakeLists.txt" or path.suffix == ".cmake":
        return "cmake"

    return {
        ".bat": "bat",
        ".c": "c",
        ".cc": "cpp",
        ".cmd": "bat",
        ".comp": "glsl",
        ".cpp": "cpp",
        ".cs": "csharp",
        ".cxx": "cpp",
        ".frag": "glsl",
        ".glsl": "glsl",
        ".h": "cpp",
        ".hh": "cpp",
        ".hpp": "cpp",
        ".hxx": "cpp",
        ".json": "json",
        ".lua": "lua",
        ".m": "objective-c",
        ".mdx": "mdx",
        ".metal": "metal",
        ".mm": "objective-cpp",
        ".ps1": "powershell",
        ".py": "python",
        ".rs": "rust",
        ".sh": "bash",
        ".toml": "toml",
        ".ts": "ts",
        ".tsx": "tsx",
        ".vert": "glsl",
        ".yaml": "yaml",
        ".yml": "yaml",
    }.get(path.suffix, "")


def is_code_file(rel_path: str) -> bool:
    normalized = rel_path.replace("\\", "/")
    if any(normalized.startswith(prefix) for prefix in EXCLUDED_PREFIXES):
        return False

    path = Path(normalized)
    return path.name in SPECIAL_FILENAMES or path.suffix.lower() in CODE_EXTENSIONS


def list_tracked_files() -> list[str]:
    completed = subprocess.run(
        ["git", "ls-files"],
        cwd=REPO_ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or "git ls-files failed")

    return [line.strip() for line in completed.stdout.splitlines() if line.strip()]


def main() -> int:
    tracked_files = [path for path in list_tracked_files() if is_code_file(path)]
    tracked_files.sort()
    tracked_files = [path for path in tracked_files if path not in PREFIX_FILES]

    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)

    lines: list[str] = []
    lines.append("# All Code")
    lines.append("")
    lines.append(f"Generated from tracked source files in `{REPO_ROOT}`.")
    lines.append("")
    lines.append(f"File count: {len(tracked_files)}")
    lines.append("")

    for rel_path in PREFIX_FILES:
        file_path = REPO_ROOT / rel_path
        if not file_path.exists():
            continue

        content = file_path.read_text(encoding="utf-8", errors="replace")
        lang = language_for(Path(rel_path))
        lines.append(f"## `{rel_path}`")
        lines.append("")
        lines.append(f"```{lang}" if lang else "```")
        lines.append(content.rstrip("\n"))
        lines.append("```")
        lines.append("")

    for rel_path in tracked_files:
        file_path = REPO_ROOT / rel_path
        content = file_path.read_text(encoding="utf-8", errors="replace")
        lang = language_for(Path(rel_path))
        lines.append(f"## `{rel_path}`")
        lines.append("")
        lines.append(f"```{lang}" if lang else "```")
        lines.append(content.rstrip("\n"))
        lines.append("```")
        lines.append("")

    OUTPUT_FILE.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote {OUTPUT_FILE}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(exc, file=sys.stderr)
        raise SystemExit(1)
