# Draxul Review Consensus — 2026-03-22

*Synthesized from: `review-latest.claude.md` (Claude Sonnet 4.6), `review-latest.gpt.md` (GPT-4o).
`review-latest.gemini.md` was empty at time of synthesis — Gemini did not contribute findings this round.*

*Produced by: claude-sonnet-4-6*

---

## How to read this document

This is a planning synthesis, not a findings dump. Each section names who raised what, calls out agreements and disagreements, and ends with a recommended fix order. Interdependencies between proposed work items are flagged at the bottom.

---

## Room conversation

**Claude** and **GPT** both reviewed the current tree. Their perspectives were broadly aligned on the architecture quality — both praised the modular library split, the renderer hierarchy, the PIMPL and Deps injection patterns, and the depth of the test suite. Both noted that the "bad things" are concentrated in a small number of seams rather than spread across the whole codebase.

Where they diverged was emphasis. GPT focused more on *runtime behavioral* issues (multi-pane timing, grid-line replay safety, HostManager test gaps) while Claude focused more on *structural code quality* (duplication, const-correctness, hot-path locking, over-large functions). Both are valid; neither contradicts the other.

---

## Unanimous agreements (both agents)

### Good things both agreed on

1. **Renderer hierarchy + RendererBundle** — clean, platform-transparent, well-layered. Both praised it without reservation.
2. **Test suite breadth** — 525+ TEST_CASE/SECTION entries including lifecycle races, fuzz-style parsing, and render snapshots. Strong foundation.
3. **PIMPL + Deps injection patterns** — consistently applied; makes subsystems independently testable.
4. **Process management (UnixPtyProcess)** — unusually careful: self-pipe wakeup, SIGTERM/SIGKILL grace period, FD_CLOEXEC everywhere.
5. **`do.py` / `plans/` discipline** — shared vocabulary, discoverable commands, living engineering commentary in `learnings.md`.

### Bad things both agreed on

1. **Windows command-line quoting** — `quote_windows_arg()` is broken for trailing-backslash paths. Both flagged as a correctness and injection risk. (Active item `05-bug`; no new item needed here.)
2. **MegaCityHost in production surface** — adds build complexity and host-manager code paths disproportionate to its product value. Both said remove or quarantine. (Icebox `17`.)
3. **HostManager is under-tested** — owns important lifecycle policy but has almost no behavioral coverage. GPT was more forceful here; Claude agreed.
4. **Agent wrapper scripts duplicated** — `ask_agent_*.py` files already drifting. (Icebox `22`.)
5. **App::pump_once() is too large** — both flagged the ~110-line frame loop function as needing decomposition.

---

## Unique findings that add value

### From Claude only

- **ImGui rendering block duplicated** between `pump_once()` and `run_render_test()`. Any change must be made twice — latent divergence bug.
- **ImGui font-size expression duplicated three times** in `app.cpp`.
- **`SplitTree::find_leaf_node()` const-correctness bug** — `const` method returning `Node*` (non-const), technically UB if the result is used to mutate through the const object.
- **`log_would_emit()` mutex on hot path** — acquires a `std::mutex` on every call. In rendering paths, should be `std::atomic<LogLevel>`.
- **`CellText::kMaxLen = 32` correctness hole** — ZWJ emoji sequences can exceed 32 bytes; truncation is logged but glyph is silently corrupted.
- **`for_each_host()` allocates heap closure per frame** — `std::function` callback; should be a template parameter.
- **`GuiActionHandler::execute()` linear string dispatch** — fine at 9 actions, grows O(n) and is typo-prone. Should be a static `unordered_map`.
- **`TextInputEvent` holds raw `const char*`** — pointer into SDL's internal buffer; lifetime is implicit. Misuse is a silent UAF.
- **`app_config.h` monolith** — combines config struct, TOML parse, file I/O, override merge, and chord parsing (~400 lines). Everything recompiles when any config field changes.
- **Render scenario TOML parser is hand-rolled** when `app_config.h`'s parser already exists.
- **`startup_resize_state.h` is over-engineered** — 30 lines and a dedicated file for 3 states and 2 transitions; could live directly in `GridHostBase`.

### From GPT only

- **Multi-pane timing deadlines** — `app.cpp:657` computes next wake deadline from the *focused* host only. Unfocused-pane timers (cursor blink, deferred callbacks) stall until an unrelated event fires. This is a real runtime bug in split-pane sessions.
- **`grid_line` replay out-of-bounds writes** — `ui_events.cpp:188-249` clamps `row` and `col_start` but then advances `repeat` cells and wide-glyph columns without further bounds checking. Out-of-range writes against alternate `IGridSink` implementations are possible on malformed redraws.
- **`dynamic_cast` for I3DHost capability wiring** — host capabilities attached post-init via RTTI; hides coupling, makes it easy for multi-agent additions to miss the attach path. (Icebox `16`.)
- **Tests compile app source files directly** — `tests/CMakeLists.txt:64-71` pulls `app/*.cpp` into the test target, duplicating the source list and weakening module boundaries.
- **Diagnostics/timing focused on active host only** in a multi-host architecture (overlapping with timing issue above).

---

## Disagreements and resolutions

### `startup_resize_state.h` — simplify vs leave?

Claude: remove the dedicated header, inline the state in `GridHostBase`.
GPT: did not comment on this specifically.
**Resolution**: Minor; not worth a dedicated work item unless a refactor sweep is already touching `GridHostBase`. Leave for now.

### BMP read/write — replace with STB?

Claude: replace with STB Image (~300 lines saved, more formats, no custom parsing bugs).
GPT: did not raise this.
**Resolution**: Valid cleanup, but not high urgency. Included as a low-priority refactor suggestion in the consensus. No separate work item created; can be bundled with a broader `render_test.cpp` cleanup if that work is ever scheduled.

### Degree of `app_config.h` split

Claude flagged the monolith; GPT did not. Both would benefit from the split (compile time, testability). Work item created.

---

## Issues already fixed or iced — not re-raised here

The following were mentioned by agents but are tracked in the icebox (`plans/work-items-icebox/`) and are deferred by design:

- Live config reload (56)
- Window-state persistence (36)
- Per-monitor DPI scaling (19)
- URL detection and click-open (20)
- Configurable ANSI palette (33)
- Configurable scrollback capacity (34)
- Performance HUD (62)
- Native tab bar (57)
- Command palette (60)
- Font fallback inspector (61)
- HostManager dynamic_cast removal (16)
- MegaCity removal (17)
- Agent scripts deduplication (22)
- Diagnostics panel render tests (13)

---

## Interdependency map

```
00-bug (multi-pane timing)
  └─→ 03-test (hostmanager lifecycle) — timing fix surfaces in lifecycle tests
  └─→ 07-test (uieventhandler boundary) — tangentially related (boundary safety)

01-bug (chord prefix stuck)
  └─→ 06-test (inputdispatcher prefix stuck) — test directly covers this bug

02-bug (grid_line replay boundary)
  └─→ 07-test (uieventhandler grid_line boundary) — test directly covers this bug

11-refactor (pump_once decomposition)
  └─→ 04-test (app smoke test) — decomposed helpers are easier to stub/test

12-refactor (app_config monolith split)
  └─→ icebox: live-config-reload (56) — split is a prerequisite for clean reload
  └─→ icebox: hierarchical-config (37) — same split unblocks config layering

13-refactor (TextInputEvent lifetime)
  └─→ no downstream deps, self-contained safety fix

03-test (hostmanager lifecycle)
  └─→ can be done in parallel with 00-bug (both touch HostManager concerns)
```

**Parallelism opportunities:**
- **Stream A (runtime correctness):** `00-bug` → `03-test` → `04-test`
- **Stream B (input correctness):** `01-bug` → `06-test`
- **Stream C (parsing safety):** `02-bug` → `07-test`
- **Stream D (refactor, independent):** `11`, `12`, `13`, `14`, `15`, `16`, `17` — all independent
- **Stream E (features, deferred):** `18`, `19`, `20`, `21`, `22` — low dependency on above

---

## Recommended fix order

| Priority | Item | Type | Why first |
|---|---|---|---|
| 0 | `00-bug` | bug | Runtime correctness in split-pane, affects all future multi-pane work |
| 1 | `01-bug` | bug | UX regression in chord input, easy to trigger accidentally |
| 2 | `02-bug` | bug | Safety/correctness for malformed redraws; IGridSink contract |
| 3 | `03-test` | test | HostManager is high-risk, low-coverage; needed before safe refactoring |
| 4 | `04-test` | test | Full-stack smoke test catches orchestrator breakage early |
| 5 | `05-test` | test | Grid::scroll directional coverage, foundation for rendering safety |
| 6 | `06-test` | test | Covers chord-prefix bug fix regression |
| 7 | `07-test` | test | Covers grid_line boundary bug fix regression |
| 8 | `08-test` | test | HighlightTable reverse-video combos, moderate complexity |
| 9 | `09-test` | test | CellText truncation — documents known correctness hole |
| 10 | `10-test` | test | Wide-char scroll render scenario — visual regression guard |
| 11 | `11-refactor` | refactor | pump_once decomposition — enables cleaner testing |
| 12 | `12-refactor` | refactor | app_config split — unblocks icebox reload/hierarchical items |
| 13 | `13-refactor` | refactor | TextInputEvent safety — silent UAF risk, small change |
| 14 | `14-refactor` | refactor | for_each_host template — zero-cost, per-frame |
| 15 | `15-refactor` | refactor | GuiActionHandler dispatch map |
| 16 | `16-refactor` | refactor | SplitTree const-correctness |
| 17 | `17-refactor` | refactor | log_would_emit atomic |
| 18 | `18-feature` | feature | Configurable terminal colors — easy, high user impact |
| 19 | `19-feature` | feature | OSC 7 CWD in shell pane title |
| 20 | `20-feature` | feature | Searchable scrollback |
| 21 | `21-feature` | feature | Per-pane env overrides |
| 22 | `22-feature` | feature | Bracketed paste confirmation |

---

*End of consensus — claude-sonnet-4-6*
