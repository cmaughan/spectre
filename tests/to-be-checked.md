# Manual Verification Checklist

Things that need hands-on testing before being signed off. Tick each one once confirmed.

---

## Bug 01 — Terminal mouse drag (DECSET 1002/1003)
- [ ] Open a tmux session, enable mouse mode (`set -g mouse on`), and verify pane border dragging works correctly
- [ ] Confirm that motion events in ranger/htop are received when a mouse button is held

## Bug 03 — Dynamic DPI / monitor-scale hotplug
- [ ] Move the window from a standard-DPI display to a HiDPI (Retina) display — text should sharpen, not stay blurry
- [ ] Move back — text should return to correct size

## Bug 04 — Selection silent truncation (limit raised 256 → 8192)
- [ ] Copy a large multi-line selection (>256 cells) and verify the full content arrives on the clipboard

## Bug 05 — Bracketed paste
- [ ] In bash: paste multi-line text — it should arrive as a single chunk, not trigger intermediate command executions
- [ ] In zsh: same check
- [ ] In neovim insert mode: paste multi-line text — should land cleanly without extra newlines

## Bug 06 — Alt-screen resize mismatch
- [ ] Open vim, resize the terminal window while vim is open, exit vim — shell prompt should appear correctly positioned with no garbled content

## Bug 07 — Init failure dialog
- [ ] Temporarily break the nvim path (rename the binary) and confirm an error dialog appears on launch
- [ ] Confirm the dialog message is user-readable and actionable
