# WI 60 — double-triple-click-selection

**Type**: feature  
**Priority**: 12  
**Source**: review-consensus.md §F4 [P][G]  
**Produced by**: claude-sonnet-4-6

---

## Goal

Add double-click word selection and triple-click line selection to terminal panes. These are standard terminal UX conventions universally expected by users.

---

## Background

SDL3 provides a `clicks` field on `SDL_MouseButtonEvent` (value 1, 2, 3) indicating single, double, or triple click. Currently Draxul does not use this field; all mouse button events are treated as single clicks. Double-click selects the word at the cursor position; triple-click selects the entire line.

---

## Tasks

- [ ] Read `app/input_dispatcher.cpp` — find the `SDL_EVENT_MOUSE_BUTTON_DOWN` handling path and how `SelectionManager` (or equivalent) is called to start a selection.
- [ ] Read the selection manager code (search for `selection_manager` or `SelectionManager`) — understand the API for starting a selection given a grid position, and how "select word" and "select line" would be expressed.
- [ ] Read `libs/draxul-types/include/draxul/events.h` (or SDL event wrapper) — confirm how click count is exposed in the Draxul event model. If `clicks` is not forwarded from SDL, add it.
- [ ] Implement double-click: on mouse-button-down with `clicks==2`, compute the word boundaries at the clicked grid cell (word = contiguous non-whitespace run), and set the selection to span that word.
- [ ] Implement triple-click: on mouse-button-down with `clicks==3`, select the entire row at the clicked grid cell.
- [ ] Ensure that after a double- or triple-click selection is set, mouse-drag extends the selection (standard xterm behaviour: triple-click then drag selects whole lines).
- [ ] Add `copy_on_select` awareness: if WI 59 is merged and `copy_on_select` is enabled, the double/triple-click selections should also auto-copy.
- [ ] Build and run: `cmake --build build --target draxul draxul-tests && py do.py smoke`

---

## Acceptance Criteria

- Double-click on a word in a terminal pane selects that word.
- Triple-click selects the entire line.
- Single-click-drag still works as before.
- Smoke test passes.

---

## Interdependencies

- **WI 59** (copy-on-select) — same selection code; do both in the same agent session.
- Independent of WI 48–57.

---

## Notes for Agent

- Word boundary definition: non-whitespace run. Do not try to implement shell-specific word separators (colons, slashes) in this item; keep it simple.
- If `SDL_MouseButtonEvent.clicks` is not currently surfaced in the Draxul event struct, add it minimally — just the click count integer — rather than redesigning the event types.
- Test manually with a running instance: double-click a filename or command in a terminal pane and confirm the selection covers the word.
