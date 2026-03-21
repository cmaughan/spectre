#!/usr/bin/env python3
from __future__ import annotations

import json
import pathlib
import subprocess
import sys


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parent


def build_dir(root: pathlib.Path) -> pathlib.Path:
    return root / "build"


def draxul_path(root: pathlib.Path) -> pathlib.Path:
    if sys.platform.startswith("win"):
        release = build_dir(root) / "Release" / "draxul.exe"
        if release.exists():
            return release
        debug = build_dir(root) / "Debug" / "draxul.exe"
        if debug.exists():
            return debug
        return release
    # macOS bundle layout (when MACOSX_BUNDLE is set)
    bundle_exe = build_dir(root) / "draxul.app" / "Contents" / "MacOS" / "draxul"
    if bundle_exe.exists():
        return bundle_exe
    # Non-bundle fallback (legacy builds or Linux)
    return build_dir(root) / "draxul"


def scenario_path(root: pathlib.Path, name: str) -> pathlib.Path:
    return root / "tests" / "render" / f"{name}.toml"


def platform_suffix() -> str:
    if sys.platform.startswith("win"):
        return "windows"
    if sys.platform.startswith("darwin"):
        return "macos"
    return "linux"


def print_render_report(root: pathlib.Path, scenario_name: str) -> None:
    report_path = root / "tests" / "render" / "out" / f"{scenario_name}.{platform_suffix()}.report.json"
    if not report_path.exists():
        print(f"  [no report found: {report_path}]")
        return
    try:
        data = json.loads(report_path.read_text())
    except Exception as e:
        print(f"  [failed to read report: {e}]")
        return

    if "error" in data:
        print(f"  [{scenario_name}] ERROR: {data['error']}")
        return

    if "changed_pixels_pct" in data:
        passed = data.get("passed", False)
        label = "PASS" if passed else "FAIL"
        print(
            f"  [{scenario_name}] diff: {data['changed_pixels_pct']:.4f}% changed pixels"
            f" ({data['changed_pixels']}/{data['width'] * data['height']})"
            f", max_channel_delta={data['max_channel_diff']}"
            f", mean_abs={data['mean_abs_channel_diff']:.4f}"
            f" [{label}]"
        )
    elif data.get("blessed"):
        print(f"  [{scenario_name}] blessed ({data['width']}x{data['height']})")


def run(command: list[str], cwd: pathlib.Path) -> int:
    print("> " + " ".join(command))
    completed = subprocess.run(command, cwd=cwd, check=False)
    return completed.returncode


def ensure_built(root: pathlib.Path) -> int:
    exe = draxul_path(root)
    if exe.exists():
        return 0

    if sys.platform.startswith("win"):
        return run(["cmake", "--build", str(build_dir(root)), "--config", "Release", "--parallel"], root)
    return run(["cmake", "--build", str(build_dir(root)), "--parallel"], root)


def help_text() -> str:
    return """Usage:
  python do.py <command> [--skip-build]

Single-word shortcuts:
  run          Run Draxul normally with a console
  smoke        Run the app smoke test
  test         Run the full local test suite (t.bat / run_tests.sh)
  shot         Regenerate the README hero screenshot
  api          Build local Doxygen API docs
  docs         Build all docs artifacts
  review       Run AI code review (GPT + Claude; Gemini added on macOS), then
               synthesise a consensus — all in one shot
  review-gemini  Run only the Gemini reviewer
  review-claude  Run only the Claude reviewer
  review-gpt     Run only the GPT reviewer
  consensus [claude|gpt|gemini]
               Run consensus synthesis on the latest reviews (default: claude)
  syncboard    Sync work-items and icebox to the GitHub project board

Deterministic render snapshots:
  basic        Run basic-view compare
  cmdline      Run cmdline-view compare
  unicode      Run unicode-view compare
  panel        Run panel-view compare
  ligatures    Run ligatures-view compare
  renderall    Run all five compare snapshots

Bless render references:
  blessbasic   Bless basic-view
  blesscmdline Bless cmdline-view
  blessunicode Bless unicode-view
  blesspanel   Bless panel-view
  blessligatures Bless ligatures-view
  blessall     Bless all five deterministic references

Examples:
  python do.py smoke
  python do.py basic
  python do.py blessall
  python do.py shot
  python do.py api
  python do.py test
"""


def main() -> int:
    root = repo_root()
    args = sys.argv[1:]

    if not args or args[0] in {"-h", "--help", "help"}:
        print(help_text())
        return 0

    command = args[0].lower()
    skip_build = "--skip-build" in args[1:]

    if command == "test":
        if sys.platform.startswith("win"):
            return run(["cmd", "/c", "t.bat"], root)
        return run(["sh", "./scripts/run_tests.sh"], root)

    if command == "shot":
        cmd = [sys.executable, str(root / "scripts" / "update_screenshot.py")]
        if skip_build:
            cmd.append("--skip-build")
        return run(cmd, root)

    if command == "api":
        return run([sys.executable, str(root / "scripts" / "build_docs.py"), "--api-only"], root)

    if command == "docs":
        return run([sys.executable, str(root / "scripts" / "build_docs.py")], root)

    if command == "review":
        rc = run([sys.executable, str(root / "scripts" / "do_review.py"), *args[1:]], root)
        if rc != 0:
            return rc
        prompt_file = root / "plans" / "prompts" / "consensus_review.md"
        output_file = root / "plans" / "reviews" / "review-consensus.md"
        return run([
            sys.executable,
            str(root / "scripts" / "ask_agent_claude.py"),
            "--prompt-file", str(prompt_file),
            "--output-file", str(output_file),
            "--full-auto",
        ], root)

    if command in {"review-gemini", "review-claude", "review-gpt"}:
        agent = command.split("-", 1)[1]
        script_map = {
            "gemini": ("ask_agent_gemini.py", "review-latest.gemini.md", ["--full-auto"]),
            "claude": ("ask_agent_claude.py", "review-latest.claude.md", ["--full-auto"]),
            "gpt":    ("ask_agent_gpt.py",    "review-latest.gpt.md",    ["--review-safe"]),
        }
        script_name, output_name, extra_flags = script_map[agent]
        prompt_file = root / "plans" / "prompts" / "review.md"
        output_file = root / "plans" / "reviews" / output_name
        cmd = [
            sys.executable,
            str(root / "scripts" / script_name),
            "--prompt-file", str(prompt_file),
            "--output-file", str(output_file),
            *extra_flags,
            *args[1:],
        ]
        return run(cmd, root)

    if command == "consensus":
        agent_scripts = {
            "claude": "ask_agent_claude.py",
            "gpt": "ask_agent_gpt.py",
            "gemini": "ask_agent_gemini.py",
        }
        extra = args[1:]
        agent = "claude"
        if extra and extra[0] in agent_scripts:
            agent = extra[0]
            extra = extra[1:]
        prompt_file = root / "plans" / "prompts" / "consensus_review.md"
        output_file = root / "plans" / "reviews" / "review-consensus.md"
        cmd = [
            sys.executable,
            str(root / "scripts" / agent_scripts[agent]),
            "--prompt-file", str(prompt_file),
            "--output-file", str(output_file),
            "--full-auto",
            *extra,
        ]
        return run(cmd, root)

    if command == "syncboard":
        return run([sys.executable, str(root / "scripts" / "sync_project_board.py")], root)

    if command == "run":
        if ensure_built(root) != 0:
            return 1
        exe = draxul_path(root)
        extra = [a for a in args[1:] if a != "--skip-build"]
        return run([str(exe), "--console"] + extra, root)

    if command == "smoke":
        if ensure_built(root) != 0:
            return 1
        exe = draxul_path(root)
        return run([str(exe), "--console", "--smoke-test"], root)

    render_map = {
        "basic": ("basic-view", False),
        "cmdline": ("cmdline-view", False),
        "unicode": ("unicode-view", False),
        "panel": ("panel-view", False),
        "ligatures": ("ligatures-view", False),
        "blessbasic": ("basic-view", True),
        "blesscmdline": ("cmdline-view", True),
        "blessunicode": ("unicode-view", True),
        "blesspanel": ("panel-view", True),
        "blessligatures": ("ligatures-view", True),
    }

    if command in render_map:
        if ensure_built(root) != 0:
            return 1
        scenario_name, bless = render_map[command]
        exe = draxul_path(root)
        cmd = [str(exe), "--console", "--render-test", str(scenario_path(root, scenario_name)),
               "--show-render-test-window"]
        if bless:
            cmd.append("--bless-render-test")
        rc = run(cmd, root)
        print_render_report(root, scenario_name)
        return rc

    if command == "renderall":
        if ensure_built(root) != 0:
            return 1
        overall_rc = 0
        for scenario_name in ("basic-view", "cmdline-view", "unicode-view", "panel-view", "ligatures-view"):
            rc = run([str(draxul_path(root)), "--console", "--render-test",
                      str(scenario_path(root, scenario_name)), "--show-render-test-window"], root)
            print_render_report(root, scenario_name)
            if rc != 0:
                overall_rc = rc
        return overall_rc

    if command == "blessall":
        if ensure_built(root) != 0:
            return 1
        for scenario_name in ("basic-view", "cmdline-view", "unicode-view", "panel-view", "ligatures-view"):
            rc = run(
                [str(draxul_path(root)), "--console", "--render-test",
                 str(scenario_path(root, scenario_name)), "--show-render-test-window", "--bless-render-test"],
                root,
            )
            if rc != 0:
                return rc
        return 0

    # If the "command" looks like a flag, the user probably meant `run <flags>`.
    if command.startswith("-"):
        if ensure_built(root) != 0:
            return 1
        exe = draxul_path(root)
        extra = [a for a in args if a != "--skip-build"]
        return run([str(exe), "--console"] + extra, root)

    print(f"Unknown command: {command}\n")
    print(help_text())
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
