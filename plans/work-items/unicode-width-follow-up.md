# Unicode Width Follow-Up

## Context

Spectre now preserves full cell text and uses a shared `cluster_cell_width()` helper for redraw layout, but that helper is still a maintained heuristic rather than a direct copy of Neovim's width engine.

Current implementation points:

- `libs/spectre-types/include/spectre/unicode.h`
- `libs/spectre-nvim/src/ui_events.cpp`
- `libs/spectre-grid/src/grid.cpp`

## Remaining Gap Vs Neovim

Neovim owns the canonical display width behavior for terminal cell layout. Spectre currently reproduces the common cases locally:

- combining clusters
- emoji modifiers
- flags
- ZWJ emoji
- keycaps
- VS16 text vs emoji presentation
- CJK wide characters
- Indic conjuncts
- Nerd Font PUA icons

What Spectre does not yet do:

- consume `ambiwidth` from `option_set`
- mirror Neovim's full Unicode width tables and update cadence
- guarantee exact behavior for rarer emoji/tag/joiner edge cases
- guarantee exact behavior for every script-specific combining case

## Exposure

This is a rendering/layout risk, not a data integrity risk.

If Spectre disagrees with Neovim on width, likely symptoms are:

- text after the mismatched cluster shifts horizontally
- double-width continuation cells are marked incorrectly
- the cursor appears offset from the intended glyph
- statusline, popup menu, virtual text, or plugin UI alignment drifts
- redraw artifacts persist until a later redraw corrects them

The highest-risk areas are:

- CJK-heavy editing
- emoji-heavy plugin UIs
- unusual or newly added Unicode sequences

## Recommended Follow-Up

1. Plumb `ambiwidth` from `option_set` into the width decision path.
2. Add a conformance test that compares Spectre width results against headless `nvim` `strdisplaywidth()` over a fixture corpus.
3. If exact parity becomes important, replace the hand-maintained width tables with a closer mirror of Neovim's tables or a dedicated Unicode-width dependency.

## Notes

The current heuristic is materially better than the old first-codepoint path and is covered by regression tests, but it should be treated as "good coverage for known real-world cases" rather than "full Neovim parity."
