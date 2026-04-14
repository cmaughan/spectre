# Console Learnings

Notes from debugging terminal-host cursor churn in Draxul, especially with PowerShell-hosted agent CLIs such as `codex`.

---

## Captured `codex` cursor pattern

Raw PTY capture and matching trace logs showed that the visible scanning cursor is not an invented renderer glitch. The terminal stream really contains two distinct visible cursor states in separate batches.

Observed pattern from `out2.log` / `cursor-next.log`:

1. A synchronized output batch starts with `CSI ? 2026 h`.
2. Inside that batch, the app does `CSI ? 25 l`, redraws status-area rows, then `CSI ? 25 h`.
3. That synchronized batch ends with `CSI ? 2026 l`, and the logical cursor is visible on a status row such as row 11 or 12.
4. A separate plain batch then arrives outside sync output:
   - `CSI ? 25 l`
   - move cursor back to the prompt row
   - `CSI ? 25 h`
5. Draxul publishes both batches, so the user sees the cursor hop to the status row and then back to the prompt.

This means:

- the scanning cursor is real protocol state, not just a bad draw order
- `codex` is not wrapping the full visible refresh in a single synchronized-output frame
- strict protocol obedience will therefore still show the scan

---

## What synchronized output does and does not mean

`CSI ? 2026 h` / `CSI ? 2026 l` is the closest thing in terminal protocol to "begin frame" / "end frame".

What it means:

- the terminal should keep presenting the last rendered state while synchronized output is active
- the terminal may present the latest settled state when synchronized output ends

What it does **not** mean:

- it does not automatically include subsequent plain VT output in the same presentation frame
- it does not guarantee that an application uses it for the entire user-visible redraw cycle
- it does not give the terminal a universal "the whole UI update is done now" marker for all output

For `codex`, the problem is specifically that the status-row cursor update is inside sync output, but the prompt-row cursor restore happens in a later plain batch.

---

## Why the timeout experiments failed

Several cursor fixes were attempted with 12ms or 20ms holdbacks, hide deferrals, or quiet windows.

Those approaches failed because:

- they guessed timing instead of identifying a real semantic boundary
- they could suppress the cursor indefinitely when output kept arriving
- they hid bugs in logs and tests by making the cursor vanish instead of making the policy correct
- they solved nothing fundamental when the application split one visible refresh across two protocol batches

Lesson:

- avoid millisecond-based cursor folklore unless there is a real protocol timeout to honor
- prefer state- and boundary-based policies over delay-based ones

---

## Likely policy for Draxul

The terminal model should continue to obey the protocol immediately. The presentation layer should be slightly more conservative.

Recommended policy:

1. Keep `VTState` fully faithful to the byte stream.
2. Publish text/grid updates normally.
3. Treat synchronized output as a strong presentation boundary when present.
4. Add a narrow main-screen cursor policy for visible hide/show redraw churn:
   - if a batch contains `CSI ? 25 l` and later `CSI ? 25 h`
   - and the batch ends with the cursor visible on a different row than the previously presented cursor
   - and the host is still on the main screen
   - then treat that cursor position as **provisional**, not immediately presented
5. Release the provisional cursor on the next **quiet pump** with no new output, not after an arbitrary millisecond timeout.
6. If another output batch arrives first and returns the cursor to the prompt row, present only that later settled cursor.

Why this is better:

- it does not mutate or second-guess the VT model
- it does not rely on magic sleep durations
- it matches the captured `codex` behavior, where the intermediate status-row cursor is quickly superseded by a prompt-row cursor in the next batch
- if the shell really does go idle with the cursor on the status row, that cursor can still be shown on the next idle pump

This is still a heuristic, but it is a narrow and evidence-based one:

- cursor-only
- main-screen only
- triggered by explicit hide/show churn
- released on a real host pump boundary instead of a guessed timeout

---

## What the logs proved

Important conclusions from the capture:

- Draxul was not missing an unknown cursor-related escape sequence.
- The remaining cursor scan is not explained by blink cadence.
- The stream does not provide a single protocol marker that spans both the synchronized status redraw and the later plain prompt restore.
- Other terminals are therefore likely applying a general presentation policy, not a `codex`-specific hack.

The useful debugging combo was:

- raw PTY capture for the real bytes
- interpreted cursor trace for model-vs-presented state

Use both together when debugging terminal repaint issues again.

---

## Useful references

- Ghostty synchronized output docs: <https://ghostty.org/docs/help/synchronized-output>
- Contour synchronized output spec: <https://contour-terminal.org/vt-extensions/synchronized-output/>
- Bubble Tea discussion about adding mode 2026 support to reduce flicker on fast terminals: <https://github.com/charmbracelet/bubbletea/discussions/1320>
- xterm.js synchronized output tracker: <https://github.com/xtermjs/xterm.js/issues/3375>
- Zellij cursor flicker issue showing the general class of repaint/cursor problems: <https://github.com/zellij-org/zellij/issues/1903>
