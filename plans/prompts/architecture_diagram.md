# Architecture Diagram Prompt

Regenerate the SVG architecture diagrams from the current codebase.

## Process

1. **Discover the real hierarchy** — do not copy class names from this prompt or from the existing diagrams. Instead, grep the codebase headers to find the actual inheritance and composition relationships:
   - `grep -rn "class .* : public" libs/ app/ --include="*.h"` for inheritance
   - Read the public headers in each `include/draxul/` directory and the private `src/*.h` headers in each library
   - Read `app/app.h` for top-level ownership
   - Check `libs/draxul-megacity/src/*.h` for MegaCity internal classes and ECS components
2. **Cross-check** — before writing a class into any diagram, confirm it actually exists in the tree. Do not invent aggregate interfaces, separate shell host classes, or other phantom nodes.
3. **Update both diagram files** (see Output Files below), preserving their distinct styles.
4. **Render Graphviz to SVG** — run `dot -Tsvg` for the `.dot` file after writing it.
5. **Verify** — spot-check that the generated SVGs are non-empty and reasonable size.

## Output Files

### 1. Hand-crafted SVG — `docs/architecture/architecture.claude.svg`

- Hand-crafted SVG with precise pixel coordinates (no Graphviz)
- Read the existing file first and preserve its visual style: gradient fills, drop shadows, marker definitions, section layout, legend
- Extend the viewBox height if new sections are needed
- Color coding:
  - Blue gradient (`gInterface`): pure virtual interfaces
  - Yellow gradient (`gAbstract`): abstract base classes
  - Green gradient (`gConcrete`): concrete implementations
  - Red gradient (`gApp`): app-layer orchestrators
  - Purple gradient (`gSupport`): support / pipeline classes
  - Green tint gradient (`gMegaCity`): MegaCity subsystem classes
- Arrow conventions: hollow arrows for inheritance, dashed filled arrows for composition/ownership
- Sections: Renderer hierarchy (left), Host hierarchy (center), Font & Grid (upper right), MegaCity subsystem (right), Window (far left), App layer (bottom)
- Data flow rows at bottom: one row per path (nvim, terminal, megacity) in distinct colors
- Threading model note at very bottom

### 2. Detailed Graphviz — `docs/architecture/architecture.dot`

- Top-to-bottom layout (`rankdir=TB`)
- Every class as its own node — no grouping
- Full inheritance edges (hollow arrows) and composition edges (dashed, filled arrows)
- Cross-link edges for uses/queries relationships (grey, `constraint=false`)
- Data flow strip and threading note at bottom
- Render: `dot -Tsvg docs/architecture/architecture.dot -o docs/architecture/architecture.svg`

## Style Rules

- Only include classes that exist in the current codebase — grep to verify
- If a class has been renamed, removed, or merged, update the diagram accordingly
- Do not add speculative or planned classes
- Keep node descriptions short (1–2 lines of subtitle text)
- The hand-crafted SVG is the primary reference diagram — spend the most care on it
