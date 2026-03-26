# Feature: Non-Blocking Toast Notifications for Recoverable Events

**Type:** feature
**Priority:** 22
**Source:** Gemini review

## Overview

Several recoverable error conditions in Draxul currently produce a log entry but no visible user feedback:

- Clipboard read/write failure (e.g. Wayland clipboard not available).
- Spawn failure for a secondary host (shell, agent).
- Font load warning (e.g. bold variant not found, using regular as fallback).
- Config key warning (unknown key in `config.toml`).
- Atlas overflow recovery (glyph evicted).

These silently fail or log to stderr which the user never sees. A small, auto-dismissing ImGui toast in the corner of the screen would make these visible without interrupting the workflow.

## Implementation plan

### Phase 1: Toast infrastructure

- [ ] Read `app/app.cpp` and `libs/draxul-host/` — find where ImGui is rendered each frame.
- [ ] Create a `ToastManager` class (in `libs/draxul-app-support/` or `app/`):
  ```cpp
  class ToastManager {
  public:
      enum class Level { kInfo, kWarn, kError };
      void push(Level level, std::string_view message, float duration_s = 4.0f);
      void render(float dt); // called each frame; draws and auto-dismisses
  };
  ```
- [ ] `render()` uses `ImGui::SetNextWindowPos` (corner anchored) to draw a small overlay for each active toast.
- [ ] Toasts auto-dismiss after `duration_s` seconds with a fade-out.

### Phase 2: Wire up existing silent failures

- [ ] Clipboard failure → `toasts.push(kWarn, "Clipboard write failed")`.
- [ ] Font variant not found → `toasts.push(kWarn, "Bold font not found; using regular")`.
- [ ] Config unknown key → `toasts.push(kInfo, "Unknown config key: ...")`.
- [ ] Spawn failure for secondary host → `toasts.push(kError, "Failed to spawn host: ...")`.

### Phase 3: Config

- [ ] Add `enable_toast_notifications = true` to `config.toml` (allow disabling).
- [ ] Add `toast_duration_s = 4.0` to `config.toml`.

### Phase 4: Documentation

- [ ] Document in `docs/features.md`.
- [ ] Update `CLAUDE.md` config notes section.

## Acceptance criteria

- [ ] A clipboard write failure produces a visible toast in the corner of the app window.
- [ ] The toast auto-dismisses after the configured duration.
- [ ] Multiple toasts stack vertically without overlapping.
- [ ] `enable_toast_notifications = false` suppresses all toasts.
- [ ] No crash if a toast is pushed before the renderer is initialised (guard with an `active_` flag).

## Interdependencies

- None — standalone feature using existing ImGui infrastructure.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
