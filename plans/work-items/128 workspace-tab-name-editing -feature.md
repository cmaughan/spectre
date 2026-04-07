# WI 128 — Workspace Tab Name Editing

**Type:** Feature  
**Severity:** Low–Medium (high user visibility, polish)  
**Source:** Claude review, Gemini review  
**Authored by:** claude-sonnet-4-6

---

## Problem / Motivation

Workspace tabs have a `name` field in `app/workspace.h` but there is currently no UI to rename a tab. Users are stuck with the auto-generated name. Both Claude and Gemini listed this as a top QoL improvement.

From Claude: "Workspace tabs have a `name` field in `app/workspace.h` but no UI to edit it. Allow double-clicking a tab to rename it; use OSC 7 working directory as a default name."

---

## Proposed UX

1. **Double-click** a tab in the tab bar → inline text-edit field appears in the tab
2. User types a name; **Enter** confirms, **Escape** cancels
3. Alternatively: right-click context menu with "Rename Tab" option (simpler first step)
4. Default name when tab is first created: derived from the active pane's host name + working directory (via OSC 7 if available)
5. Names persisted in session state (related to **WI 25** session restore)

---

## Implementation Notes

- `Workspace` already has a `name` field — just wire up the setter
- `ChromeHost` renders the tab bar — tab hit detection is already present (see **WI 119**)
- Inline editing could use an ImGui `InputText` overlay on top of the tab, or a simple SDL text-input event handler in `ChromeHost`
- OSC 7 default: `LocalTerminalHost` already receives OSC 7 → wire `on_osc7_cwd` to update the tab name if no user override has been set
- Command palette action `"rename_tab:"` for keyboard-driven rename

---

## Acceptance Criteria

- [ ] Double-click (or right-click → Rename) opens a rename UI
- [ ] Enter confirms; Escape cancels with no change
- [ ] Tab name persists across workspace switches
- [ ] Tab name saved/restored with session state (or gracefully falls back if session restore is not yet implemented)
- [ ] CI green; no render regression

---

## Interdependencies

- **WI 129** (reopen last closed pane/tab) uses the tab name when re-creating the tab — coordinate to ensure name is stored in the close record.
- **WI 119** (ChromeHost tab-bar hit-test) must be complete for confidence in double-click detection.
