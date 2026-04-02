# Feature: Keyboard-Driven Copy Mode for Terminal Panes

**Type:** feature
**Priority:** 21
**Source:** Gemini review

## Overview

Currently, text selection in terminal panes is mouse-only. Power users (and those in environments where mouse is unavailable) need a keyboard-driven copy mode — similar to tmux's copy mode or Vim's visual mode:

1. Press a keybinding to enter copy mode.
2. Navigate with `h/j/k/l` or arrow keys.
3. Press `v` to start a selection, `V` for line selection.
4. Press `y` to yank (copy to clipboard) and exit copy mode.
5. Press `q` or `Escape` to exit without copying.

## Implementation plan

### Phase 1: Copy mode state

- [ ] Read `libs/draxul-host/include/draxul/host.h` — find `GridHostBase` and `SelectionManager`.
- [ ] Read `libs/draxul-host/src/selection_manager.cpp` — understand how selection is managed.
- [ ] Add a `CopyModeState` to `GridHostBase`:
  ```cpp
  struct CopyModeState {
      bool active = false;
      int cursor_row = 0;
      int cursor_col = 0;
      bool selecting = false;
      int select_start_row = 0;
      int select_start_col = 0;
  };
  ```

### Phase 2: Enter/exit copy mode

- [ ] Add a `toggle_copy_mode` GUI action (register in the GUI action map alongside `copy`, `paste`, etc.).
- [ ] Add a default keybinding (e.g. `ctrl+shift+[` or user-configurable).
- [ ] On enter: save current scrollback viewport position; freeze normal input forwarding to Neovim.
- [ ] On exit: restore viewport; resume normal input forwarding.

### Phase 3: Navigation in copy mode

- [ ] While copy mode is active, intercept `h/j/k/l` and arrow keys:
  - Move the copy-mode cursor one cell/row at a time.
  - Allow scrolling into scrollback via `j/k` past the top of the visible grid.
- [ ] Show the copy-mode cursor as a distinct highlight (e.g. inverted cell or a highlight overlay).

### Phase 4: Selection and yank

- [ ] `v` → start selection at cursor position; navigate to extend.
- [ ] `V` → select entire lines.
- [ ] `y` → copy selection to clipboard (reuse existing `SelectionManager::get_text()` + clipboard write) and exit copy mode.
- [ ] `Escape`/`q` → exit without copy.

### Phase 5: Config and keybindings

- [ ] Expose `enter_copy_mode` as a named GUI action in `config.toml [keybindings]`.
- [ ] Document in `docs/features.md`.

## Acceptance criteria

- [ ] Can enter/exit copy mode via keybinding without crashing.
- [ ] Cursor moves correctly over the visible grid and into scrollback.
- [ ] Selection highlights are visible.
- [ ] Yanked text matches the selected region and is on the system clipboard.
- [ ] Normal Neovim/shell input is blocked while copy mode is active and resumes on exit.

## Interdependencies

- **Icebox `20 searchable-scrollback -feature`**: both navigate the scrollback; share navigation primitives.
- **`06 scrollback-ring-wrap -test`**: copy mode navigating past the ring boundary must be tested.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
