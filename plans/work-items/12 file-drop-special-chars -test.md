# 12 file-drop-special-chars -test

**Priority:** MEDIUM
**Type:** Test
**Raised by:** Claude, Gemini
**Model:** claude-sonnet-4-6

---

## Problem

`app/input_dispatcher.cpp:111–114` constructs an `open_file:` action string by concatenating the dropped path. No test exercises paths containing:
- Colons (`:` — meaningful in the action string format)
- Spaces
- Unicode characters
- Shell-sensitive characters (`$`, `!`, backtick, etc.)

A path like `/foo:bar/file.txt` could be misparse as action `open_file` with parameter `bar/file.txt`, losing the `/foo` prefix.

Gemini similarly notes drag-drop/open-file tests with Unicode and special characters are missing.

---

## Implementation Plan

- [ ] Read `app/input_dispatcher.cpp` around the file-drop path and the `open_file:` action format.
- [ ] Understand how the path is extracted from the action string on the receiving end.
- [ ] Write `tests/file_drop_tests.cpp`:
  - Simulate an SDL drop-file event with paths:
    - `/normal/path/file.txt` (baseline)
    - `/path with spaces/file.txt`
    - `/path:with:colons/file.txt`
    - `/path/with/ünïcödé/file.txt`
    - `/path/$SPECIAL/file.txt`
  - For each: verify the host receives the full, unmodified path.
- [ ] If the action-string format is ambiguous for colon-containing paths, fix the encoding (e.g., percent-encode the path, or use a length-prefixed format instead of a colon delimiter).
- [ ] Add to `tests/CMakeLists.txt`.
- [ ] Run ctest.

---

## Acceptance

- File-drop with colon-containing path: host receives the complete original path, not a truncated/misparse version.
- All special-character variants pass.

---

## Interdependencies

- Relates to **05-bug** (Windows command-line quoting) — similar theme of path-handling fragility but on a different codepath.
