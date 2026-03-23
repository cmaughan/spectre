# 09 osc7-malformed-uri -test

**Type:** test
**Priority:** 9
**Source:** Claude review (review-latest.claude.md)

## Problem

`TerminalHostBase::handle_osc()` processes OSC 7 (`\e]7;<uri>\a`) to update the current working directory. A shell emitting a malformed URI like `\e]7;not-a-url\a` should be silently ignored (or produce a `WARN` log) rather than causing a parse error, crash, or silent state corruption. There is no test for this path.

## Acceptance Criteria

- [ ] Locate `handle_osc()` in `libs/draxul-host/src/terminal_host_base.cpp` (or equivalent).
- [ ] Add tests using `replay_fixture.h` or direct sequence injection for:
  - [ ] `\e]7;not-a-url\a` — malformed URI, should produce a WARN or be silently ignored.
  - [ ] `\e]7;\a` — empty URI, should be handled gracefully.
  - [ ] `\e]7;file:///valid/path\a` — valid URI, should update the CWD correctly.
  - [ ] `\e]7;file://host/path\a` — remote host URI (should be ignored or handled appropriately).
- [ ] Verify no crash under `mac-asan` for any of the above.
- [ ] Run under `ctest`.

## Implementation Notes

- Use `ScopedLogCapture` if the project has one, to assert on the WARN output for malformed URIs.
- If the handler does not yet gracefully handle malformed URIs, add the guard as part of this item.

## Interdependencies

- No blockers. Independent test item.

---

*Authored by claude-sonnet-4-6 — 2026-03-23*
