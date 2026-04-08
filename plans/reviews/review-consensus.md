# Draxul Review Consensus — April 2026

**Synthesised from:** `review-latest.claude.md`, `review-latest.gemini.md`, `review-latest.gpt.md`, `review-bugs-latest.claude.md`, `review-bugs-latest.gemini.md`, `review-bugs-latest.gpt.md`

**Produced by:** claude-sonnet-4-6

---

## How to read this document

Each section calls out who raised the point and how strongly agents agreed. Items already in `plans/work-items/` or `plans/work-items-icebox/` are flagged **[tracked]** with their WI number. Items that are net-new as of this consensus are flagged **[NEW → WI NNN]** and have matching files created in `plans/work-items/`.

---

## Part 1 — Architecture & Module Quality

### Unanimous strengths (all 3 regular-review agents agree)

- **Clean layering / no circular deps**: draxul-types < window < renderer < font < grid < host < app. Zero platform `#ifdef`s leak into shared code. (*Claude*, *Gemini*, *GPT*)
- **Interface-first design**: `IWindow`, `IGridRenderer`, `IHost` pure-virtual bases enable testability. (*Claude*, *Gemini*, *GPT*)
- **Dependency injection via structs**: `AppDeps`, `HostManager::Deps`, `InputDispatcher::Deps` — no singletons, trivially swappable. (*Claude*, *Gemini*, *GPT*)
- **Strong test suite breadth**: 87+ test files covering VT, RPC, grid, config, input, DPI. (*Claude*, *Gemini*, *GPT*)

### Unanimous weaknesses (all 3 agree)

- **`App` is a God Object**: Startup, overlay creation, workspace lifecycle, render-tree assembly, toast routing, diagnostics, layout, input wiring, and event-loop policy all live in one class (`app.h:60`, `app.cpp:507`, `app.cpp:1180`). Every agent called this the dominant merge-conflict zone. (*Claude*, *Gemini*, *GPT*) — **[tracked: WI 125 overlay-registry-refactor partially addresses this]**

- **`InputDispatcher::Deps` is too wide**: 20+ function pointers; one missing callback silently breaks mouse input. Same chrome/panel/overlay/host routing pattern repeated three times at `input_dispatcher.cpp:303`, `:425`, `:490`. (*Claude*, *Gemini*, *GPT*) — **[NEW → WI 26]**

- **Glyph-atlas upload ownership is split**: `grid_rendering_pipeline.cpp:174/227`, `chrome_host.cpp:1451`, `command_palette_host.cpp:188`, `toast_host.cpp:175` all inspect and clear dirty state independently — ordering risk, every new text overlay is a footgun. (*GPT*, *Claude*) — **[tracked: WI 109 atlas-upload-dedup]**

- **Overlay management is hand-wired**: Adding a new overlay type requires touching multiple files in `App`. No data-driven registry. (*GPT*, *Gemini*) — **[tracked: WI 125 overlay-registry-refactor]**

---

## Part 2 — Confirmed Bugs

### Critical bugs (all agents or multi-agent confirmed)

**B1 — NvimHost partial init → `std::terminate`** *(GPT: CRITICAL)*
`NvimHost::initialize_host()` starts the RPC reader thread and resize worker before `attach_ui()` / `execute_startup_commands()`. If either later step fails, `HostManager::create_host_for_leaf()` destroys the host without calling `shutdown()`. Joinable `std::thread` members in `NvimRpc` / `UiRequestWorker` hit `std::terminate`; spawned `nvim` child is left running.
— **[NEW → WI 04]**

**B2 — `reader_thread_func` has no `try/catch`; malformed msgpack aborts process** *(Gemini: CRITICAL, GPT: CRITICAL — unanimous)*
`dispatch_rpc_message()` calls `as_int()` / `as_str()` on fixed positions after only checking array length. A wrong-shaped-but-valid-msgpack packet (`[1, "oops", nil, 0]`) throws `std::bad_variant_access` on the reader thread with no catch boundary, terminating the process.
— **[NEW → WI 05]**

**B3 — `ui_events.cpp` redraw handlers trust inner element types without checking** *(GPT: CRITICAL)*
`handle_grid_resize()`, `handle_mode_info_set()`, `handle_option_set()`, `handle_set_title()` all call `as_int()` / `as_str()` without type-checking map keys or scalar types. A malformed redraw batch such as `["grid_resize", [1, "80", "24"]]` throws on the main thread and terminates the UI.
— **[NEW → WI 06]**

**B4 — `on_notification_available` / `on_request` assigned after reader thread starts** *(Gemini: CRITICAL, GPT: HIGH)*
`NvimHost::initialize_host()` starts the reader thread via `rpc_.initialize()` before these `std::function` members are assigned. If Neovim sends a notification at startup, the reader thread reads a partially-initialized `std::function`.
— **[NEW → WI 07]**

### High bugs

**B5 — Windows `WriteFile` doesn't retry partial writes** *(Claude: MEDIUM — but functionally HIGH)*
`nvim_process.cpp:187` returns `false` on partial write without retry. The POSIX path loops. Large pastes or full pipe buffers silently truncate RPC messages.
— **[NEW → WI 08]**

**B6 — `ToastHost::initialize()` dereferences possibly-null grid handle** *(GPT: HIGH)*
Calls `create_grid_handle()` and immediately uses `handle_` at `toast_host.cpp:25/30` without a null check. Every other host (`GridHostBase`, `ChromeHost`, `CommandPaletteHost`) guards this. Crash surface.
— **[NEW → WI 11]**

**B7 — Background-thread toast sits invisible on idle app** *(GPT: HIGH)*
`push()` appends to `pending_` but `next_deadline()` ignores pending work (`toast_host.cpp:142`) and `App::push_toast()` does not wake or request a frame (`app.cpp:1461`). A background-thread toast stays invisible until unrelated input arrives.
— **[NEW → WI 12]**

### Medium bugs

**B8 — `int64_t` msgid silently truncated to `uint32_t`** *(Claude: MEDIUM)*
`rpc.cpp:212,240` cast `as_int()` result directly to `uint32_t`. A negative or >2³²−1 ID from a corrupted stream could collide with an in-flight msgid, unblocking the wrong waiting thread.
— **[NEW → WI 09]**

**B9 — `write_bmp_rgba` throws `filesystem_error` on bare filenames** *(GPT: HIGH)*
`bmp.cpp:46` calls `std::filesystem::create_directories(path.parent_path())` unconditionally. With `--screenshot out.bmp`, `parent_path()` is empty; many stdlib implementations throw for that.
— **[NEW → WI 10]**

**B10 — `ChromeHost` measures tab/pane labels by byte count, not codepoints** *(GPT: MEDIUM)*
Tab sizing at `chrome_host.cpp:288,439`, pane-status truncation at `:943`, glyph warming at `:1018` all use byte lengths. UTF-8 tab names and pane names mis-measure, truncate incorrectly, and may split multibyte sequences into invalid one-byte clusters. Especially visible now that rename input is UTF-8-aware.
— **[NEW → WI 13]**

### Already tracked (not duplicated)

| WI | Description |
|---|---|
| WI 84 (complete) | conpty trailing-backslash escape before closing quote |
| WI 86 (complete) | signed overflow in `CapturedFrame::valid()` and `read_bmp_rgba` row offset |
| WI 89 (complete) | RPC notification callback under lock |
| WI 97 (complete) | nvim write return unchecked |
| WI 98 (complete) | scrollback cleared on resize |

---

## Part 3 — Testing Gaps

### Unanimous gaps (all 3 regular-review agents)

- **HostManager split/close stress** — 1000 rapid split/close cycles with tree-structure assertions. (*Claude*, *Gemini*) — **[NEW → WI 14]**
- **RPC notification queue backpressure** — burst >4096 notifications; verify no silent drops. (*Claude*) — **[NEW → WI 15]**
- **Host lifecycle state machine** — `init → pump → shutdown → pump` must not crash; double-shutdown must be idempotent. (*Claude*) — **[NEW → WI 16]**
- **Font atlas exhaustion** — fill cache with >10K unique glyphs; verify graceful degradation. (*Claude*) — **[NEW → WI 17]**

### GPT-specific gaps (not raised by others)

- **`ToastHost` background-thread delivery while idle** — assert wake/request behaviour. (*GPT*) — **[NEW → WI 18]**
- **`ToastHost` `create_grid_handle()` failure during initialize** — graceful failure vs crash. (*GPT*) — **[NEW → WI 19]**
- **`ChromeHost` UTF-8 tab-name width and hit-testing** (*GPT*) — **[NEW → WI 20]**
- **Config reload under host activity** — trigger `reload_config()` while synthetic key events processed; no lost events. (*Claude*) — **[NEW → WI 21]**
- **`InputDispatcher` with null dependency callbacks** — construct with `nullptr` Deps; verify graceful no-op. (*Claude*) — **[NEW → WI 22]**
- **Renderer shutdown with pending frames** — call `shutdown()` before `end_frame()` completes; run under ASan. (*Claude*) — **[NEW → WI 23]**

### Already tracked (not duplicated)

| WI | Description |
|---|---|
| WI 00 (active) | workspace-tab-focus-preservation |
| WI 01 (active) | alt-screen-roundtrip-fidelity |
| WI 06 icebox | rpc-fragmentation-pipe-boundary |
| WI 10 icebox | inputdispatcher-focus-loss |
| WI 69 icebox | concurrent-host-shutdown |
| WI 108 icebox | atlas-dirty-multi-subsystem |
| WI 121 (active) | app-render-tree-overlay-ordering-test |
| WI 123 (active) | uirequestworker-overlapping-requests-test |

---

## Part 4 — Refactoring Opportunities

### Multi-agent agreement

- **`NvimHost` is a stringly-typed action router with embedded Lua** (`nvim_host.cpp:162`). Hard to test and review. (*GPT*, *Claude*) — **[tracked: WI 126 embedded-lua-extraction, WI 127 nvimhost-handler-consolidation]**
- **Config layer leaks renderer/window deps** (`draxul-config/CMakeLists.txt:15`, `app_options.h:9`). Heavier incremental builds, weak module ownership. (*GPT*) — **[tracked: WI 110 — recently completed]**
- **`InputDispatcher` mouse routing repeated 3×** — same chrome/panel/overlay/host pattern in three separate `dispatch_*` methods. (*GPT*, *Claude*) — **[NEW → WI 26]**
- **No centralised test fixtures** — each test reinvents `FakeWindow`, `FakeRenderer`, `FakeHost`. (*Claude*) — **[NEW → WI 25]**

### Claude-specific

- **No unified `Result<T, Error>` type** — `bool + error()`, `std::optional`, and silent-fail patterns all coexist. Makes error-path auditing hard. (*Claude*) — **[NEW → WI 24]**

### Disagreements

- *Gemini* called for a proper layout engine in `ChromeHost`. *Claude* and *GPT* both said adding tests (WI 20) is the right next step and a full layout engine is premature. **Consensus: add UTF-8 layout tests first; defer layout engine.**
- *Gemini* flagged SDL coupling as a weakness. *Claude* and *GPT* noted that `IWindow` abstraction is already sufficient. **Consensus: existing abstraction is fine.**
- *Gemini* said MegaCity should be removed. *GPT* and *Claude* said follow the plugin-boundary work first. **Consensus: follow WI 133 (megacity plugin boundary) before removal decisions.**

---

## Part 5 — Feature Suggestions

### Convergent features (raised by ≥2 agents)

- **Session/layout restore** (*Claude*, *Gemini*) — **[tracked: WI 25 icebox session-restore]**
- **Hierarchical/per-project config** (*Claude*, *Gemini*) — **[tracked: WI 37 icebox hierarchical-config]**
- **Configurable ANSI palette** (*Gemini*) — **[tracked: WI 33 icebox configurable-ansi-palette]**
- **OSC 8 hyperlinks** (*GPT*) — **[tracked: WI 20 active osc8-hyperlink-support]**
- **OSC 133 shell marks** (*GPT*) — **[tracked: WI 24 active osc133-shell-integration]**
- **Multi-cell ligatures** (*GPT*) — **[tracked: WI 80 active multi-cell-ligature]**
- **Command palette MRU** (*Gemini*) — **[tracked: WI 58 icebox command-palette-mru]**
- **Distraction-free mode** (*Gemini*) — **[tracked: WI 132 icebox distraction-free-focus-mode]**
- **Integrated log viewer** (*Gemini*) — **[tracked: WI 32 icebox integrated-log-viewer]**
- **Keybinding inspector** (*Gemini*) — **[tracked: WI 130 icebox keybinding-inspector]**

### Net-new features (not yet tracked)

- **Dynamic glyph atlas growth** — when the 2048×2048 atlas fills, allocate a second page; toast on emergency eviction. (*Claude*) — **[NEW → WI 27]**
- **RPC timeout user feedback** — show dismissible toast when RPC request exceeds 5s threshold. (*Claude*) — **[NEW → WI 28]**
- **Config migration framework** — version the config schema; auto-migrate old formats silently. (*Claude*) — **[NEW → WI 29]**
- **Host telemetry in diagnostics** — extend F12 overlay with per-host RPC latency, grid dirty-cell rates, memory usage. (*Claude*) — **[NEW → WI 30]**

---

## Part 6 — Recommended Fix Order

This is the planning-oriented path all agents can agree on:

### Phase 1 — RPC pipeline hardening (do together, same files)
1. **WI 04** — NvimHost partial init RAII rollback
2. **WI 05** — `reader_thread_func` try/catch boundary
3. **WI 06** — `ui_events.cpp` redraw handler type guards
4. **WI 07** — `on_notification_available` race (assign before thread start)

*These all touch `nvim_host.cpp`, `rpc.cpp`, `ui_events.cpp`. Batching them minimises churn.*

### Phase 2 — Toast correctness (before adding more toast tests)
5. **WI 11** — `ToastHost` null grid handle crash
6. **WI 12** — Toast idle wake gap

*WI 18 and WI 19 (toast tests) should follow immediately after these fixes.*

### Phase 3 — Chrome UTF-8 correctness
7. **WI 13** — `ChromeHost` byte-count bug
*WI 20 (chromehost-utf8-layout test) should follow.*

### Phase 4 — Windows/portable fixes
8. **WI 08** — Windows `WriteFile` partial write retry
9. **WI 09** — msgid int64→uint32 validation
10. **WI 10** — `write_bmp_rgba` bare filename guard

### Phase 5 — Test coverage wave
11. **WI 14** — HostManager split/close stress
12. **WI 15** — RPC queue backpressure
13. **WI 16** — Host lifecycle state machine
14. **WI 17** — Atlas exhaustion
15. **WI 18, 19** — Toast delivery and init-failure tests
16. **WI 21, 22, 23** — Config-reload, InputDispatcher-null-deps, renderer-shutdown tests

*Consider doing WI 25 (centralized test fixtures) before or alongside Phase 5 — it pays off immediately on every test in this wave.*

### Phase 6 — Structural refactors
17. **WI 24** — Unified `Result<T, Error>` type (touches many callers; do after bug fixes settle)
18. **WI 25** — Centralized test fixtures
19. **WI 26** — `InputDispatcher` routing consolidation
20. Continued: **WI 125, 126, 127** (overlay registry, Lua extraction, handler consolidation)

### Phase 7 — Features
21. **WI 27** — Atlas dynamic growth (depends on WI 17 having characterised the failure mode)
22. **WI 28** — RPC timeout user feedback (depends on Phase 1 being solid)
23. **WI 29** — Config migration framework (depends on WI 24 result type for error returns)
24. **WI 30** — Host telemetry in diagnostics

---

## Part 7 — Interdependency Map

```
WI 04 ──┐
WI 05 ──┤──▶ Phase 1 hardening ──▶ WI 28 (rpc timeout feedback)
WI 06 ──┤
WI 07 ──┘

WI 11 ──┐
WI 12 ──┘──▶ WI 18 (toast idle test) ──▶ WI 19 (toast init failure test)

WI 13 ──────▶ WI 20 (chromehost utf8 layout test)

WI 17 (atlas exhaustion) ──▶ WI 27 (atlas dynamic growth)
WI 109 (atlas upload dedup, active) ──▶ WI 17 (should be done first)

WI 25 (centralised fixtures) ──▶ WI 14, 15, 16, 17, 18, 19, 20, 21, 22, 23

WI 24 (Result<T,E>) ──▶ WI 29 (config migration framework)

WI 125 (overlay registry) ──▶ WI 26 (inputdispatcher routing)
WI 126 (lua extraction) ──▶ WI 127 (handler consolidation)

WI 29 (config migration) depends on: WI 24 + existing WI 37 (hierarchical config icebox)

WI 30 (host telemetry) depends on: Phase 1 RPC hardening
```

---

## Part 8 — Disagreements Between Agents

| Topic | Claude | Gemini | GPT | Consensus |
|---|---|---|---|---|
| MegaCity | Keep, add plugin boundary | Remove now | Keep, track separately | Follow plugin-boundary WI first |
| ChromeHost layout | Add tests, no engine rewrite | Needs proper layout engine | UTF-8 fix + tests, defer | Add tests (WI 20); no engine rewrite yet |
| SDL coupling | IWindow sufficient | Major weakness | IWindow sufficient | IWindow abstraction is adequate |
| `App` decomposition | Service extraction needed | Split into modules | Already tracked in WI 125 | WI 125 is the right vehicle |
| `draxul-app-support` naming | Vaguely named | Not raised | Not raised | Low priority; rename when touched |

---

*Reviewed: 2026-04-08 | Model: claude-sonnet-4-6*
