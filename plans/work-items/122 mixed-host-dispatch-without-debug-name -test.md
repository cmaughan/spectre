# WI 122 — Mixed-Host Dispatch Test (Without Debug-Name Heuristic)

**Type:** Test  
**Severity:** Medium (acceptance gate for WI 116 fix)  
**Source:** Gemini review  
**Authored by:** claude-sonnet-4-6

---

## Problem

`App::dispatch_to_nvim_host()` currently uses `host.debug_state().name == "nvim"` to find the target Neovim pane (see **WI 116**). There is no test that:
- Proves dispatch targets the correct pane when Neovim and shell hosts coexist in the same workspace
- Catches regression to the debug-name heuristic after a fix

From Gemini: "A mixed-host dispatch test proving app actions target the intended Neovim pane without debug-name heuristics."

---

## What to Test

```
Setup:
  - Workspace with 2 panes: pane A = NvimHost, pane B = LocalTerminalHost

Dispatch:
  - Focus pane A → dispatch "open_file_at_type:" action → goes to pane A
  - Focus pane B → dispatch "open_file_at_type:" action → goes to pane A (nvim-specific action)
  - Focus pane B → dispatch terminal-specific action → goes to pane B

No-nvim scenario:
  - Workspace with only LocalTerminalHost panes
  - Dispatch nvim-specific action → no crash, action dropped or error logged

Rename scenario (regression for WI 116):
  - Temporarily change NvimHost debug name to "notanvim"
  - Dispatch still targets NvimHost (because fix uses typed interface, not string)
```

---

## Implementation Notes

- Use fake `NvimHost` and `LocalTerminalHost` with injectable `Deps`
- Track dispatched actions via a recording fake
- Test file: `tests/app_dispatch_tests.cpp`
- This test should FAIL before WI 116 is fixed (on the rename scenario), and PASS after

---

## Acceptance Criteria

- [ ] All dispatch scenarios above are asserted
- [ ] Rename scenario fails on old code, passes after WI 116 fix
- [ ] Tests run under `ctest`
- [ ] CI green

---

## Interdependencies

- **Depends on WI 116** being fixed first (or write the test to document current broken behaviour and flip the assertion after the fix).
