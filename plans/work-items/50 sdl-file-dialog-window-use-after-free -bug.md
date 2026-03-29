# Bug: Use-after-free of SDL_Window* in async file dialog callback

**Severity**: CRITICAL
**File**: `libs/draxul-window/src/sdl_file_dialog.cpp:28`
**Source**: review-bugs-consensus.md (C3)

## Description

`show_open_file_dialog` stores a raw `SDL_Window*` in the heap-allocated `Ctx` struct. The OS file-dialog callback fires asynchronously on a native thread after the dialog closes. If the user closes the main window while the dialog is still open, the `SDL_Window` is destroyed before the callback runs. Line 28 then calls `SDL_GetWindowID(c->window)` on a freed object — a use-after-free.

## Trigger Scenario

1. Invoke the file-open dialog.
2. Alt-F4 / Cmd-Q the main window before selecting a file.
3. The callback fires post-destruction and dereferences the freed `SDL_Window`.

## Investigation Steps

- [ ] Confirm `SDL_GetWindowID` dereferences the `SDL_Window*` internally (expected: yes)
- [ ] Check if SDL provides a "window destroyed" notification that could be used to cancel the dialog instead

## Fix Strategy

- [ ] In `Ctx`, replace `SDL_Window* window` with `SDL_WindowID window_id`
- [ ] At dialog-open time: `Ctx{ SDL_GetWindowID(window), result_event_type }`
- [ ] In the callback: use `c->window_id` directly — no pointer dereference needed
- [ ] Also fix M2 (path leak on push failure) in the same pass — see work item 51

## Acceptance Criteria

- [ ] `Ctx` no longer stores a raw `SDL_Window*`
- [ ] Closing the window while the dialog is open does not crash or trigger sanitizer errors
- [ ] File selection still works correctly after the fix
