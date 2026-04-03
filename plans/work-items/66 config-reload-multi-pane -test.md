---
# WI 66 — Config Reload with Multiple Active Panes Tests

**Type:** test  
**Priority:** high (multi-pane config reload is a gap; rollback path untested)  
**Raised by:** [C] Claude, [P] GPT  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

`App::reload_config()` calls `for_each_host()` to propagate new font metrics. This path has no test with multiple live panes. Three untested sub-scenarios:

1. Font size change propagates to **all** panes (not just the focused one)
2. A failed font reload leaves **all** pane state coherent (no partial updates)
3. Atlas reset after font change does not lose glyphs from a concurrent `CommandPaletteHost`

---

## Investigation Steps

- [ ] Read `app/app.cpp` around `reload_config()` and `for_each_host()` call
- [ ] Read `CommandPaletteHost::flush_atlas_if_dirty()` to understand atlas lifecycle
- [ ] Identify the test seam for instantiating multiple hosts in `HostManager`
- [ ] Look for existing `config_reload` tests in `tests/`

---

## Test Cases

### Scenario A — Font metrics propagate to all panes
- [ ] Create `HostManager` with 3 fake hosts (e.g. via split)
- [ ] Call `reload_config()` with a new font size
- [ ] Verify all 3 hosts received `apply_font_metrics()` with new metrics

### Scenario B — Failed reload rollback
- [ ] Configure reload to fail (e.g. font file path made invalid)
- [ ] Call `reload_config()`
- [ ] Verify all hosts retain original font metrics
- [ ] Verify `AppConfig` state is unchanged

### Scenario C — Atlas coherence after font change
- [ ] Open `CommandPaletteHost`; flush atlas to a known dirty state
- [ ] Trigger font reload (atlas reset expected)
- [ ] Verify `CommandPaletteHost` rebuilds its glyphs correctly on next render frame

---

## Implementation

- [ ] Add test file `tests/config_reload_multi_pane_test.cpp`
- [ ] Use `FakeWindow`, `FakeGridPipelineRenderer`, and fake host implementations

---

## Acceptance Criteria

- [ ] All three scenarios have passing tests
- [ ] Rollback scenario verifies `AppConfig` field by field, not just via a flag
- [ ] No new test infrastructure required beyond existing fakes
