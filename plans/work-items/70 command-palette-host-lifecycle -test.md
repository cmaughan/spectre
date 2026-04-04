---
# WI 70 — CommandPaletteHost Open/Close/Reopen Lifecycle Tests

**Type:** test  
**Priority:** medium (atlas state may leak across open/close cycles)  
**Raised by:** [P] GPT  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

`CommandPaletteHost` is opened and closed frequently by the user. Its atlas dirty state and glyph handles may persist across cycles if not properly reset. Stale atlas handles or uncleared dirty state after close can cause:
- Glyphs from the previous session appearing in a freshly-opened palette
- Duplicate glyph rasterization on reopen
- Atlas dirty-rect uploads being skipped on the first post-open frame

---

## Investigation Steps

- [ ] Read `app/command_palette_host.cpp` — `initialize()`, `shutdown()`, and `flush_atlas_if_dirty()`
- [ ] Identify what atlas state is retained across a `shutdown()` + `initialize()` cycle
- [ ] Check whether glyph handles are released on shutdown or leaked

---

## Test Cases

- [ ] Open palette → close → reopen → verify atlas state is clean on reopen (no stale dirty bits)
- [ ] Open palette → type text (rasterize glyphs) → close → reopen → verify glyphs are rasterized fresh (not stale)
- [ ] Open palette with atlas at capacity → close → reopen → verify no atlas corruption
- [ ] Rapid open/close 100 times → verify no resource handle leak (track handle count via fake atlas)

---

## Implementation

- [ ] Locate or create `tests/command_palette_host_test.cpp`
- [ ] Use `FakeGlyphAtlas` (already in `tests/support/`) to count atlas operations
- [ ] Use `FakeWindow` and a fake renderer to drive the palette without a real GPU

---

## Acceptance Criteria

- [ ] After close + reopen, `FakeGlyphAtlas::dirty()` is false (or correctly set, depending on design)
- [ ] No glyph handle count grows unboundedly across repeated open/close cycles
- [ ] Rapid-open test completes without assertion failures

---

## Notes

Closely related to the atlas upload path duplication noted in the review (CommandPaletteHost reimplements atlas dirty-rect upload). If WI 71 (atlas centralization) lands, update these tests to use the shared upload path.
