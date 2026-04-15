# 133 xterm-256color-compatibility -feature

**Type:** Feature  
**Status:** Icebox  
**Priority:** Medium  
**Owner:** Unassigned

## Summary

Draxul's Unix PTY shell hosts currently advertise `TERM=xterm` rather than
`TERM=xterm-256color`. This is deliberate: the terminal emulator supports a
useful xterm-like subset, but it does not yet implement enough of the broader
xterm/terminfo contract to safely claim full `xterm-256color` compatibility.

This item tracks the work required to raise that terminal identity safely,
without regressing shell-launched tools such as `nvim`, `vim`, `less`, `tmux`,
`fzf`, `htop`, and ncurses-based applications.

## Problem

Advertising `TERM=xterm-256color` is more than "enable 256 colors". It also
signals that applications may rely on xterm-style queries, private modes,
charset behavior, and terminfo-defined key/control capabilities. If Draxul
claims that identity before implementing those behaviors, terminal programs may:

- enter code paths that assume unsupported control/query responses
- mis-detect capabilities and render incorrectly
- break startup or repaint behavior in shell-launched full-screen apps
- diverge from user expectations for ncurses- and terminfo-driven tooling

We already support many important pieces:

- ANSI + 256-color SGR handling
- alt screen
- scroll regions
- mouse 1000/1002/1003/1006
- bracketed paste
- cursor-shape control
- OSC 7 / OSC 52
- synchronized output

But that is not yet the same as a trustworthy `xterm-256color` claim.

## Goal

Support `TERM=xterm-256color` as the default Unix PTY shell identity with good
practical compatibility for common terminal software, backed by explicit tests
and capture-based regressions.

## Scope

- audit the xterm/terminfo surface that common apps actually depend on
- implement missing high-value terminal behaviors or queries
- add regression tests for shell-launched full-screen tools
- only switch the default TERM once the compatibility bar is defensible

## Non-Goals

- shipping a custom terminfo entry in this item
- making Draxul perfectly emulate every historical xterm edge case
- changing embedded `nvim --embed` TERM handling; that path should remain
  non-terminal (`TERM=dumb`)

## Candidate Gaps To Audit

- alternate character set / line-drawing charset handling (`ESC ( 0` and friends)
- richer terminal query/response coverage beyond the current DA/DSR subset
- focus reporting and other xterm private modes commonly probed by apps
- broader key-sequence compatibility tied to xterm terminfo expectations
- additional OSC/DCS behaviors that terminfo- or app-driven probes may expect
- full-screen curses/ncurses repaint behavior under shell startup and teardown

## Implementation Sketch

- [ ] Capture and catalog startup/probe sequences from representative apps:
  - `nvim`
  - `vim`
  - `less`
  - `tmux`
  - `htop` / `btop`
  - `fzf`
  - a small ncurses probe app if needed
- [ ] Compare those captures against Draxul's implemented VT/xterm behavior.
- [ ] Prioritize missing pieces by compatibility impact, not spec completeness.
- [ ] Add focused parser/terminal-state tests for each newly implemented behavior.
- [ ] Add at least one PTY capture replay test for shell -> full-screen-app ->
  shell return transitions.
- [ ] Re-evaluate the default shell TERM once the regression set is green.

## Validation

- Common shell-launched full-screen tools start cleanly under `TERM=xterm-256color`.
- Returning from a full-screen app leaves the shell prompt correctly repainted.
- No new regressions in existing terminal VT, mouse, and cursor-refresh tests.
- PTY capture replays cover at least one real-world full-screen startup/exit flow.

## Practical Compatibility Checklist

Treat this as the minimum bar before advertising an xterm-family TERM
intentionally.

- [ ] `vim` starts, redraws, resizes, and exits cleanly from a shell pane
- [ ] `nvim` starts, redraws, resizes, and exits cleanly from a shell pane
- [ ] `less` renders and exits cleanly without leaving stale screen state
- [ ] `fzf` renders interactively and returns to the shell with a clean prompt
- [ ] `htop` or `btop` renders and exits cleanly
- [ ] `tmux` starts inside the shell pane without obvious capability mismatch
- [ ] A small ncurses test app renders borders/line drawing correctly
- [ ] Alternate character set / line-drawing mode behaves correctly
- [ ] Alt-screen enter/exit preserves and restores the shell prompt correctly
- [ ] Window resize during a full-screen app leaves the app and shell in a valid state
- [ ] Function keys and modified keys match the expected xterm behavior for common apps
- [ ] Common terminal capability probes do not push apps into visibly broken code paths
- [ ] PTY capture replay exists for at least one shell -> full-screen app -> shell return flow

## Notes

- Passing the checklist is more important than claiming spec completeness.
- If the checklist reveals that `xterm` is supportable but `xterm-256color` is
  still too aggressive, it is acceptable to split this item later into:
  - safe `xterm` support
  - later `xterm-256color` promotion

## Coordination

- Related to `plans/work-items-icebox/21 per-pane-env-overrides -feature.md`
  because that work would let users override TERM/COLORTERM explicitly.
- Related to the existing terminal VT compatibility work already completed in
  `plans/work-items-complete/64 terminal vt compatibility hardening -feature.md`.
