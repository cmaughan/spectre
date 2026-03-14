# Review Concensus

## Purpose

This note distills the review discussion into a single planning document for fixes. It is not a raw dump of prior findings. It tries to answer:

- what the reviewers broadly agree on
- what is still active in the current tree
- what has already been addressed and should not be reopened accidentally
- what order of fixes will make the codebase easier for multiple agents to work on

## Inputs

- `plans/reviews/claude-sonnet-4-6/code-review-latest.md`
- current code inspection while writing this note

## Broad Agreement

The codebase is fundamentally well-shaped at the top level. The module split is sensible:

- `spectre-types`
- `spectre-window`
- `spectre-renderer`
- `spectre-font`
- `spectre-grid`
- `spectre-nvim`
- `app`

The main concern is not that the repo is chaotic. The concern is that a few important seams still collapse too many responsibilities together, which creates merge pressure and makes changes harder to isolate.

The strongest points of agreement are:

1. `App` is still too large and too policy-heavy.
2. the renderer backends duplicate too much CPU-side logic.
3. the RPC interface is too weak for safe extension.
4. the Neovim integration layer still owns too much downstream behavior.
5. test coverage is good for pure logic, but thin around the hardest runtime seams.

## What Is Still Active

### 1. `App` remains the main coordination hotspot

This is still the biggest structural issue.

`App` owns:

- startup order
- resize policy
- font loading
- fallback font discovery
- font selection per cluster
- atlas upload decisions
- grid to renderer projection
- cursor styling from mode/highlight state
- wiring of input, window, renderer, RPC, and UI event handling

That makes `app.cpp` the place where multiple agents will step on each other first.

### 2. Renderer backend duplication is now a first-class maintainability risk

The Vulkan and Metal implementations still each own:

- CPU-side `GpuCell` state
- grid resize projection
- cell update mapping
- cursor save/apply/restore logic
- overlay cursor behavior

This is no longer just a code smell. It is a parallel-work problem. Any cursor fix, glyph-offset fix, or per-cell behavior change will want synchronized edits in both files.

### 3. RPC needs a richer result contract

The current RPC surface still returns only a `result` payload to callers. Neovim errors are not exposed as a typed result the caller must handle.

That means new RPC-based features will keep defaulting to:

- request something
- get `nil` or an empty value on failure
- infer what happened indirectly

That is a bad foundation for future changes and debugging.

### 4. UI options are parsed but not integrated into rendering state

The code now has a width helper and better grapheme handling, but `option_set` is still not part of a durable shared editor-options model. That leaves room for drift on settings like `ambiwidth` and any future UI-affecting editor options.

This is not the most urgent problem, but it is an example of missing state ownership.

### 5. Mouse/input fidelity is still reduced

The input path works for the current tested cases, but the event model still lacks modifier-rich mouse semantics and full Neovim mouse detail. This is a future compatibility issue more than a current correctness fire.

## What Has Changed Since Older Review Notes

Some earlier findings are now stale or partially resolved and should not be blindly copied into future work lists.

Already improved:

- Unicode width logic has been consolidated into `spectre-types/include/spectre/unicode.h`.
- There are now native tests for grid, redraw parsing, input translation, and Unicode width behavior.
- The renderer factory boundary has already been moved behind `spectre-renderer`.
- The repo now has CI, agent docs, test scripts, run scripts, and a cleaner local workflow.
- The previous font and cursor regressions discussed in review have already been addressed.

Still worth keeping from the earlier review, but with updated framing:

- the font subsystem is better than before, but font policy still leaks upward into `App`
- the Neovim integration is better tested than before, but RPC transport itself is still under-tested
- input has tests now, but the data model is still narrower than Neovim's real semantics

## Consensus On Testing Gaps

The current tests cover the logic that is easiest to unit test:

- grid behavior
- redraw parsing
- input translation
- Unicode width heuristics
- one bundled-font shaping/rasterization check

The main uncovered areas are:

1. RPC framing, transport failure, and Neovim error propagation
2. renderer behavior and backend parity
3. window and focus/activation behavior
4. app-level startup/shutdown orchestration
5. double-width edge cases during scroll/overwrite and other redraw corner cases

This means the repo is strongest at validating local logic and weakest at validating cross-module seams.

## Recommended Fix Order

### Phase 1: Strengthen the seams

These changes improve parallel development the most.

1. Introduce a richer RPC result type.
2. Extract a shared CPU renderer front-end from the Vulkan and Metal backends.
3. Extract a text or font service from `App`.
4. Introduce a small editor UI options state object fed by `option_set`.

### Phase 2: Reduce coupling between modules

1. Move `UiEventHandler` away from direct ownership of concrete downstream model behavior.
2. Narrow public module interfaces so `App` does less FreeType/HarfBuzz-aware work.
3. Remove dead or misleading abstraction paths, especially around atlas partial updates if they are not going to be used.

### Phase 3: Close the highest-value testing holes

1. Add direct RPC tests for:
   - request success
   - request error
   - timeout/transport failure
   - malformed or partial frames
2. Add renderer smoke or replay-style parity coverage for shared cell/cursor logic.
3. Add more grid redraw edge cases around double-width continuation behavior.
4. Add at least one app-level startup/shutdown smoke test path that is scriptable.

## Suggested Concrete Refactors

### A. `TextService`

Owns:

- primary font
- fallback font chain
- shapers
- glyph cache
- cluster-to-font resolution
- atlas upload policy

`App` should ask for something closer to:

- `resolve_cluster(text) -> AtlasRegion`
- `set_point_size(size)`
- `metrics()`

and stop touching font backend details directly.

### B. Shared renderer front-end

Owns:

- `GpuCell` vector
- cursor overlay state
- cell position projection
- cell update application

Backends should own:

- GPU resource creation
- swapchain/layer lifecycle
- buffer/texture upload implementation
- actual frame submission

This is probably the highest-leverage cleanup after RPC.

### C. RPC result model

Replace the current "return a raw `MpackValue`" contract with something explicit, for example:

- transport success/failure
- Neovim error payload
- result payload

This would make all future RPC code less guessy.

## Agent-Focused Suggestions

If the goal is to make the repo easier for multiple agents to work in parallel, the highest-value moves are:

1. keep major logic out of `App`
2. keep backend-specific code thin
3. give each module a narrow public contract with explicit data ownership
4. test seams, not just local logic
5. keep planning notes like this one updated when a review item becomes stale

Practical workflow suggestions:

- when a major refactor starts, create a short plan note under `plans/work-items/`
- when a review finding is fixed, mark it in the planning note instead of letting old review files silently drift
- prefer "one module owns this policy" over helper logic spread across `App`, tests, and backends

## Proposed Next Steps

If fixing this incrementally, the most sensible next sequence is:

1. RPC result contract and tests
2. shared renderer front-end extraction
3. `TextService` extraction from `App`
4. editor options state wired from `option_set`
5. renderer/app smoke coverage

That order improves correctness first, then reduces parallel-work friction, then fills the hardest testing gaps.
