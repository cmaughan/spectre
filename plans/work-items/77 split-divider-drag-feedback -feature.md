---
# WI 77 — Split Divider Mouse-Drag Resizing with Visual Feedback

**Type:** feature  
**Priority:** high (existing invisible functionality; major UX gap)  
**Raised by:** [C] Claude, [G] Gemini  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

`SplitTree` supports `set_divider_ratio()` and `InputDispatcher` already detects divider hit-tests, but:
- The divider hit zone (4 pixels wide) is visually invisible to users
- There is no cursor change on hover (no resize cursor feedback)
- Drag-to-resize is not wired to the mouse move handler

Users have no discoverable way to resize panes. They must use command palette actions. This is a significant UX gap compared to any standard terminal multiplexer.

---

## Investigation Steps

- [ ] Read `app/split_tree.h` — `divider_hit_test()`, `set_divider_ratio()`
- [ ] Read `app/input_dispatcher.cpp` — existing divider hit detection path
- [ ] Read `libs/draxul-window/include/draxul/window.h` — `IWindow::set_cursor(CursorShape)` (check if this exists or needs to be added)
- [ ] Read `libs/draxul-types/include/draxul/types.h` — `CursorShape` enum (check if resize cursors are defined)

---

## Implementation

### Step 1 — Visual divider rendering
- [ ] In the renderer or app layer, draw the divider as a 1–2 px line in a muted fg color (configurable via config or hard-coded as `#555555`)
- [ ] The divider should be rendered as part of the BG pass, not a separate draw call

### Step 2 — Hover cursor change
- [ ] When the mouse moves over the divider hit zone, call `IWindow::set_cursor(CursorShape::ResizeLeftRight)` (or `ResizeUpDown` for horizontal dividers)
- [ ] When the mouse leaves the zone, restore `CursorShape::Default`
- [ ] Add `ResizeLeftRight` and `ResizeUpDown` to `CursorShape` if not present; implement in `SdlWindow` via `SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE)` etc.

### Step 3 — Drag-to-resize
- [ ] On `mouse_button_down` while over the divider, enter drag mode: record `drag_active = true`, `drag_divider_index`
- [ ] On `mouse_move` while `drag_active`, compute new ratio from mouse x (or y for horizontal): `ratio = (mouse_x - split_origin_x) / split_width`; clamp to `[0.1, 0.9]`; call `split_tree_.set_divider_ratio(drag_divider_index, ratio)`; request viewport recompute + repaint
- [ ] On `mouse_button_up`, clear `drag_active`

---

## Acceptance Criteria

- [ ] Divider is visible as a 1px line between panes
- [ ] Mouse cursor changes to resize cursor when over the divider hit zone
- [ ] Click-and-drag moves the divider and recomputes pane viewports in real time
- [ ] Release ends drag; cursor restores to default
- [ ] Smoke test passes; no regression in existing input dispatcher tests

---

## Notes

Consider adding a `split_divider_color` config key (icebox) for the divider colour, but ship with a hard-coded default for now. The cursor shapes may need SDL3 implementation work; check if `SdlWindow` already supports arbitrary cursor shapes.
