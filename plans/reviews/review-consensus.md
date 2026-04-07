All 13 new work items and the updated consensus review are written. Here's a summary of what was produced:

---

## `plans/reviews/review-consensus.md` — Full Replacement

A 9-section synthesis covering:
- **Unanimous agreements** (MegaCity liability, app.cpp size, threading risks, good renderer abstraction)
- **New bugs raised** (two overlooked Gemini criticals, four GPT findings)
- **Three cross-cutting topics**: atlas upload duplication, documentation drift, config layer build boundary
- **Genuine disagreements**: MegaCity (remove vs. isolate), custom VT parser (keep), do.py complexity (non-issue)
- **Recommended fix order** and full **interdependency table**

---

## 13 New Work Items (WI 100–112)

### Bugs (6)
| # | Severity | Description |
|---|----------|-------------|
| 100 | CRITICAL | RPC init-order race: reader thread starts before callbacks are assigned [Gemini, overlooked] |
| 101 | CRITICAL | `MpackValue::as_*()` uncaught exception kills reader thread silently [Gemini, overlooked] |
| 102 | HIGH | Tab action names absent from config parser/serializer allowlist [GPT] |
| 103 | HIGH | `activate_tab` is a no-op when selected from command palette [GPT] |
| 104 | MEDIUM | `apply_font_metrics` / `reload_config` skip inactive workspaces [GPT] |
| 105 | MEDIUM | Second late RPC response for same timed-out msgid re-leaks into `responses_` map [Gemini] |

### Tests (3)
| # | Description |
|---|-------------|
| 106 | Round-trip test for tab keybinding names through `config.toml` (acceptance for WI 102) |
| 107 | Inactive workspace config/font propagation test (acceptance for WI 104) |
| 108 | Atlas dirty-flag coordination across grid, chrome, and palette subsystems (acceptance for WI 109) |

### Refactors (3)
| # | Description |
|---|-------------|
| 109 | Extract single atlas-upload-if-dirty helper; remove three copy-pasted copies |
| 110 | Fix `draxul-config` public links to `draxul-renderer`/`draxul-window` (build boundary) |
| 111 | Repair stale `renderers.md`, `city_db.md` schema version, `features.md` keybinding |

### Feature (1)
| # | Description |
|---|-------------|
| 112 | Add `mac-tsan` CMake preset for ThreadSanitizer builds |

### Key interdependencies flagged in consensus
- WI 100 + 101 + 89 → fix together in one `rpc.cpp` pass
- WI 102 → WI 106 (test is the acceptance criterion)
- WI 104 → WI 107 (same)
- WI 109 → WI 108 (same)
- WI 100/101 must precede WI 112 (fix known races before enabling TSan CI gate)
- WI 71 (existing) may resolve WI 103 as a side-effect
