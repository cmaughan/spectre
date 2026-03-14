# Code Review — Spectre

## Findings

1. High: The RPC contract still discards Neovim errors, so callers cannot distinguish success from failure.
Refs:
`libs/spectre-nvim/include/spectre/nvim.h:106`
`libs/spectre-nvim/include/spectre/nvim.h:117`
`libs/spectre-nvim/src/rpc.cpp:35`
`app/app.cpp:217`
`app/app.cpp:360`
`app/app.cpp:404`

`RpcResponse` stores both `error` and `result`, but `IRpcChannel::request()` returns only `MpackValue`, and `NvimRpc::request()` returns only `resp.result`. That means `nvim_ui_attach`, `nvim_ui_try_resize`, and any future request path cannot surface Neovim-side errors to the caller. This is both a runtime problem and an architectural problem: agents cannot safely extend RPC usage without auditing every call site for silent failure.

2. High: The CPU-side renderer front-end is duplicated across Vulkan and Metal, which guarantees drift and double-fixes.
Refs:
`libs/spectre-renderer/src/vulkan/vk_renderer.cpp:175`
`libs/spectre-renderer/src/vulkan/vk_renderer.cpp:214`
`libs/spectre-renderer/src/vulkan/vk_renderer.cpp:389`
`libs/spectre-renderer/src/vulkan/vk_renderer.cpp:459`
`libs/spectre-renderer/src/metal/metal_renderer.mm:203`
`libs/spectre-renderer/src/metal/metal_renderer.mm:257`
`libs/spectre-renderer/src/metal/metal_renderer.mm:337`
`libs/spectre-renderer/src/metal/metal_renderer.mm:408`

`set_grid_size()`, `update_cells()`, `set_cursor()`, `apply_cursor()`, and `restore_cursor()` are effectively duplicated between the backends. This is exactly the kind of duplication that slows parallel work, because every cursor, layout, or cell-state bug has to be fixed twice and kept behaviorally identical. The recent cursor regression is a good example of this risk.

3. Medium: `App` is still carrying too much policy and concrete subsystem ownership for a "thin orchestration" layer.
Refs:
`app/app.h:41`
`app/app.h:44`
`app/app.h:49`
`app/app.cpp:294`
`app/app.cpp:431`
`app/app.cpp:459`

`App` directly owns `SdlWindow`, `GlyphCache`, `NvimRpc`, font fallback discovery, cluster-to-font resolution, highlight resolution, grid-to-renderer projection, cursor-style resolution, and startup activation behavior. The module split looks clean at the directory level, but the app layer is still the place where multiple concerns meet. That makes it a conflict hotspot for multiple agents and keeps the highest-value logic out of isolated, testable services.

4. Medium: The mouse input model cannot represent full Neovim mouse semantics.
Refs:
`libs/spectre-types/include/spectre/events.h:23`
`libs/spectre-types/include/spectre/events.h:29`
`libs/spectre-types/include/spectre/events.h:33`
`libs/spectre-nvim/src/input.cpp:194`
`libs/spectre-nvim/src/input.cpp:222`
`libs/spectre-nvim/src/input.cpp:233`
`tests/input_tests.cpp:58`

The mouse event structs do not carry modifier state, and `NvimInput` always sends an empty modifier string plus a hardcoded `"left"` drag. That blocks exact forwarding for Shift/Ctrl/Alt mouse chords and makes future input work harder because the event model itself is missing data. The tests currently cover only the reduced model, so this limitation is easy to overlook.

5. Medium: The atlas partial-update path is dead code, which increases backend surface area without any payoff.
Refs:
`libs/spectre-renderer/include/spectre/renderer.h:22`
`libs/spectre-renderer/include/spectre/renderer.h:23`
`libs/spectre-font/include/spectre/font.h:71`
`libs/spectre-font/include/spectre/font.h:97`
`app/app.cpp:324`
`app/app.cpp:332`
`app/app.cpp:334`

`GlyphCache` tracks atlas dirtiness and a dirty rectangle, and the renderer interface exposes `update_atlas_region()`, but the app always calls `set_atlas_texture()` for a full upload when any glyph changed. That leaves a dead abstraction path in the renderer API and forces contributors to reason about an incremental upload protocol that is not actually exercised.

6. Medium: `option_set` is parsed but not consumed by any app state, so Neovim UI option drift is still likely.
Refs:
`libs/spectre-nvim/include/spectre/nvim.h:193`
`libs/spectre-nvim/src/ui_events.cpp:253`
`app/app.cpp:178`
`libs/spectre-types/include/spectre/unicode.h:274`

`UiEventHandler` exposes `on_option_set`, but `App` wires only `on_flush` and `on_grid_resize`. The current width logic in `cluster_cell_width()` therefore cannot react to option changes like `ambiwidth`, and future Neovim UI-affecting options will have the same problem. The architecture already has the hook, but not the state object or ownership model to make it useful.

## Testing Holes

- `spectre-tests` links only `spectre-font`, `spectre-grid`, and `spectre-nvim`, so there is still no direct automated coverage for `spectre-renderer`, `spectre-window`, or `app/`.
Refs:
`tests/CMakeLists.txt:11`

- There are no tests for RPC failure behavior, response error propagation, or malformed/partial msgpack handling. That leaves the least debuggable subsystem with no direct regression coverage.
Refs:
`libs/spectre-nvim/src/rpc.cpp:35`
`tests/test_main.cpp:10`

- There is no renderer-level golden or smoke harness that verifies cursor behavior, atlas upload behavior, or backend parity. Current tests stop at `CellUpdate` production.
Refs:
`tests/ui_events_tests.cpp:71`
`libs/spectre-renderer/src/vulkan/vk_renderer.cpp:214`
`libs/spectre-renderer/src/metal/metal_renderer.mm:257`

- Grid tests do not exercise scroll or overwrite behavior around double-width continuations, which is a fragile part of terminal rendering.
Refs:
`tests/grid_tests.cpp:10`
`tests/grid_tests.cpp:25`

## Modularity Opportunities

1. Extract a shared CPU renderer front-end from the Vulkan and Metal backends.
This layer should own `gpu_cells_`, cursor overlay state, cell-size/grid-size projection, and cursor application/restoration. The backend-specific classes should be responsible only for GPU resource creation and frame submission.

2. Introduce a real RPC result type.
Replace `MpackValue request(...)` with something like `RpcResult { MpackValue error; MpackValue result; bool transport_ok; }` or a small `expected`-style type. That would make app startup, resize, and future feature work much safer.

3. Pull text and fallback-font policy out of `App`.
`FontManager`, `TextShaper`, fallback font discovery, and cluster-to-font resolution want to live in a dedicated text service. That would reduce `App` to lifecycle and wiring, and make font work easier to test without booting the whole program.

4. Add an explicit UI options state object.
`option_set` should feed a small shared state object consumed by width/layout code and input code. That gives one place to evolve editor-driven rendering behavior instead of scattering special cases.

## Suggested Plan

1. Fix the RPC API first.
Expose Neovim errors to callers, then add tests for attach, resize, and failure paths.

2. Extract the shared renderer front-end.
Move duplicated cell/cursor logic out of the Vulkan and Metal implementations before more rendering behavior lands.

3. Extract a text subsystem from `App`.
Give it ownership of fallback font selection, shaping, cache policy, and atlas update decisions.

4. Expand test coverage around the highest-risk seams.
Priority order:
RPC failure cases
double-width scroll/overwrite behavior
input modifiers and drag semantics
renderer smoke or replay-based parity checks

## Overall

The repo shape is good: the top-level module split is understandable, the build is straightforward, and the replay-style redraw tests are a solid base. The main remaining risk is not "messy code everywhere"; it is that a few key seams still collapse multiple concerns together. Those seams are exactly where parallel agent work will create the most friction until they are narrowed.
