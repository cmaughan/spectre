# Rendering Lifecycle Flowchart Prompt

Generate a clean, colorful Mermaid flowchart that shows the **full rendering lifecycle** of Draxul when the following hosts are active:

- **MegaCityHost** (3D scene in a split pane)
- **Zsh terminal host** (shell in a split pane)
- **CommandPaletteHost** (overlay, may or may not be open)
- **DiagnosticsPanelHost** (bottom split)

## Requirements

1. **Single linear top-to-bottom flow** — no subgraphs or branching layouts. Every step is one node in a straight vertical chain so you can read the exact temporal order at a glance.

2. **Clearly annotate who generates geometry vs who draws it** — use italic annotations on each node:
   - Which host owns the CPU data (grid cells, 3D scene, ImGui draw lists, palette cells)
   - When GPU upload happens (grid handle upload, atlas updates)
   - When draw commands are encoded into the live frame context
   - What the GPU actually executes after submission

3. **Show the full temporal sequence:**
   - `begin_frame()` acquires swapchain/drawable
   - Pane hosts draw in split-tree order (MegaCity first, then Zsh)
   - `flush_submit_chunk()` between each host (shown as grey nodes)
   - Diagnostics host draws (bottom split ImGui)
   - Command palette pumps + draws (full-window overlay grid)
   - `end_frame()` closes encoder/command buffer
   - Backend submits
   - GPU executes all recorded work in order
   - Present

4. **Show shared GPU resources** as a single side-note node with dotted links to begin_frame and submit.

5. **Color scheme** — use Mermaid `classDef` styles:
   - **App/orchestration**: blue (`#2196F3`)
   - **MegaCity host**: purple (`#9C27B0`)
   - **Zsh host**: green (`#4CAF50`)
   - **Diagnostics host**: orange (`#FF9800`)
   - **Command palette**: teal (`#009688`)
   - **GPU execution**: dark slate (`#37474F`) with white text
   - **Shared resources**: coral (`#E91E63`)
   - **Flush boundaries**: grey (`#78909C`)

6. **Keep it readable** — short node labels with ownership detail in `<i>` italic annotations. No subgraphs, no crossing edges.

## Output

Produce a single Markdown file with:
- A brief title and one-line description
- The full Mermaid flowchart in a ```mermaid code block
- A short legend table explaining the color coding
- A "Key Ownership" section summarizing who owns what

Save to `docs/architecture/rendering-lifecycle.md`.
