All 18 work items and the consensus file are written. Here's the summary:

---

## `review-consensus.md` — Replaced

A full synthesis of all three reviews covering:
- **4 unanimous agreements** (app god object, MegaCity burden, drain loop starvation, VT parser gaps, dynamic_cast capability probing)
- **Agent-specific findings** with attribution (Claude: dead Linux code, Metal header hazard; Gemini: duplicate NvimHost handler, atlas upload duplication, overlay hardwiring; GPT: IHostCallbacks width, SDL3 risk)
- **3 real disagreements** (MegaCity remove vs isolate, VT parser keep vs replace, ImGui opinion)
- **Recommended 4-phase fix order** with interdependency table

---

## 18 New Work Items (WI 115–132)

### Bugs (4) — highest priority
| WI | Description |
|-----|------------|
| **115** | NvimHost duplicate `open_file_at_type:` handler -bug |
| **116** | App debug-string dispatch heuristic (`name == "nvim"`) -bug |
| **117** | MetalRenderer dual ObjC/C++ header layout hazard -bug |
| **118** | SplitTree O(n) recursive finds + `std::function` allocations -bug |

### Tests (5)
| WI | Description |
|-----|------------|
| **119** | ChromeHost tab-bar hit-testing + DPI viewport -test |
| **120** | ToastHost lifecycle (stacking, expiry, fade, replay) -test |
| **121** | App render-tree overlay ordering -test |
| **122** | Mixed-host dispatch without debug-name heuristic -test |
| **123** | UiRequestWorker overlapping requests + cancellation -test |

### Refactors (4)
| WI | Description |
|-----|------------|
| **124** | Dead Linux code path cleanup -refactor |
| **125** | Overlay registry (data-driven overlay management) -refactor |
| **126** | Embedded Lua extraction from nvim_host.cpp -refactor |
| **127** | NvimHost dispatch handler consolidation to dispatch table -refactor |

### Features (5)
| WI | Description |
|-----|------------|
| **128** | Workspace tab name editing (double-click to rename) -feature |
| **129** | Reopen last closed pane or tab -feature |
| **130** | Keybinding inspector (show matched action in diagnostics panel) -feature |
| **131** | Clipboard history with paste picker -feature |
| **132** | Distraction-free / focus mode -feature |

**Key interdependency chains:**
- `115 → 126 → 127` (dedup first, then extract, then consolidate)
- `116 → 122` (fix dispatch bug, then write its acceptance test)
- `119 + 120 + 121 → 125` (tests must exist before overlay registry refactor)
- `125 → 132` (focus mode is trivial after overlay registry)
- `128 → 129` (tab name needed in reopen record)
