All done. Here's a summary of what was produced:

---

## What was done

### `review-consensus.md` — replaced

A 304-line synthesis of all 6 review files (3 general + 3 bug reviews), structured as:

1. **Unanimous strengths** — clean layering, interface-first, DI structs, test breadth, platform separation (all three agents agree, no asterisks)
2. **Unanimous weaknesses** — App god object, InputDispatcher Deps bloat, atlas upload split, ChromeHost byte-oriented layout, scattered test fixtures, toast lifecycle gaps
3. **Bug findings** — all 10 active WI bugs catalogued, 7 already-complete bugs called out to avoid re-work, 1 new bug identified (WI 31)
4. **Architectural agreements and disagreements** — ChromeHost layout engine (fix UTF-8 first, not a new engine), SDL coupling (non-issue; Gemini overreaches), MegaCity (follow WI 55), App decomposition (workspace manager after WIs 125-127)
5. **Testing gaps**, **feature gaps**, **recommended 7-phase fix order**
6. **Interdependency map** showing how all 9 new WIs connect to each other and to existing active WIs

### 9 new work items

| WI | Type | Summary |
|---|---|---|
| **31** | bug (HIGH) | POSIX `fork()` in `nvim_process.cpp` doesn't close inherited FDs before `execvp`; distinct from WI 90 (SIGPIPE only) |
| **32** | test | `ToastHost` full lifecycle: stacking, expiry ordering, fade, buffered pre-init replay — extends WIs 18/19 |
| **33** | test | `CommandPalette` UTF-8 editing: backspace over multibyte, non-ASCII paste, emoji input |
| **34** | test | `ChromeHost` tab-bar geometry under resize + DPI change; hit-test consistency |
| **35** | test | Font fallback corpus: CJK, emoji, ZWJ sequences, RTL, combining marks, missing-glyph caching |
| **36** | refactor (low) | DRY up duplicate `quote_windows_arg` in `conpty_process.cpp` / `nvim_process.cpp` (follow-up to WI 84) |
| **37** | feature | Duplicate pane — open new pane with same host type, cwd, launch options |
| **38** | feature | Pane activity badges — bell, non-zero exit, long-running indicator on tabs/pills |
| **39** | feature | Right-click context menus on tabs, pane pills, status area |

**Key interdependencies flagged:** WI 39 depends on WI 26 + WI 125; WI 38 depends on WI 24 for exit badges; WI 37 is a prerequisite action for WI 39; WI 32 requires WIs 11+12 fixed first; WI 35 informs WIs 17+27.
