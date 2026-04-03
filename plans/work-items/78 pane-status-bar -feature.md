---
# WI 78 — Pane Status Bar / Per-Pane Info Display

**Type:** feature  
**Priority:** medium (discoverability and situational awareness)  
**Raised by:** [C] Claude, [G] Gemini, [P] GPT — all three  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

Users have no lightweight way to see:
- What host type is running in each pane (nvim, zsh, bash, megacity)
- The current working directory of a pane
- Pane dimensions (columns × rows)
- Which pane is focused

Currently they must open the diagnostics panel to get any of this information. All three review agents independently identified this gap.

---

## Investigation Steps

- [ ] Read existing host interfaces (`IHost`, `IGridHost`) to see what metadata is already exposed
- [ ] Check if `OSC 7` (cwd tracking) is already wired and where the cwd is stored
- [ ] Read the `draxul-gui` tooltip / GUI layer to understand the existing non-ImGui rendering path
- [ ] Assess: is this best as a configurable always-visible footer in the split pane chrome, or as a hover tooltip on the pane border?

---

## Proposed Design

A lightweight per-pane status footer: a thin strip (1 cell height) below each terminal grid pane showing:
- Pane number or name (e.g. "1" for focused, "2" for others)
- Host type label (e.g. "nvim", "zsh", "megacity")
- Dimensions (e.g. "80×24")
- CWD if available and not too long (truncated with `…` if > 30 chars)

Rendered using the existing `draxul-gui` layer (font engine + GPU quad renderer), not ImGui, to avoid the dependency for a simple status strip.

Optional config: `show_pane_status = true/false` (default `true`).

---

## Implementation Steps

- [ ] Add `virtual std::string status_text() const` to `IHost` (or a narrower interface) returning the status string
- [ ] Implement in each host type: `NvimHost` returns `"nvim | 80×24 | ~/projects/draxul"`, shell hosts return `"zsh | 80×24 | ~/projects/draxul"`, etc.
- [ ] In `App::render()` or the compositor, after drawing each pane, render the status strip below (or overlay at the bottom of) the pane viewport using the `draxul-gui` text path
- [ ] Add `show_pane_status` config key, default `true`
- [ ] Reduce the pane's grid height by 1 row when the status bar is visible to prevent overlap with terminal content

---

## Acceptance Criteria

- [ ] Each pane shows host type, dimensions, and cwd (when available) below the terminal content
- [ ] Focused pane's status is visually distinct (e.g. brighter colour)
- [ ] `show_pane_status = false` hides all status strips
- [ ] No overlap with terminal content
- [ ] Smoke test and render tests pass

---

## Notes

The divider drag feature (WI 77) should be implemented first or in the same pass, since both touch the pane chrome layout. The status bar affects `SplitTree` viewport height calculation (subtract 1 row height from each pane).

A subagent is appropriate — multiple files touched across `IHost`, app rendering, config, and `SplitTree`.
