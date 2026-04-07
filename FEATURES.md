# Draxul — Features & User Guide

Draxul is a GPU-accelerated Neovim (and shell) GUI frontend. This document covers everything
a user needs to know: configuration, keybindings, terminal behaviour, and command-line options.

---

## Table of Contents

1. [Configuration File](#configuration-file)
2. [Keybindings](#keybindings)
3. [Font Settings](#font-settings)
4. [Window Settings](#window-settings)
5. [Terminal Behaviour](#terminal-behaviour)
6. [Mouse Support](#mouse-support)
7. [Text Selection & Clipboard](#text-selection--clipboard)
8. [Scrollback](#scrollback)
9. [Host / Shell Selection](#host--shell-selection)
10. [Command-Line Options](#command-line-options)
11. [Diagnostics Panel](#diagnostics-panel)
12. [Platform Notes](#platform-notes)

---

## Configuration File

Draxul reads a `config.toml` file on startup. If the file does not exist it is created with
defaults on first run.

**Location:**

| Platform | Path |
|----------|------|
| Windows  | `%APPDATA%\draxul\config.toml` |
| macOS    | `~/Library/Application Support/draxul/config.toml` |
| Linux    | `$XDG_CONFIG_HOME/draxul/config.toml` (falls back to `~/.config/draxul/config.toml`) |

---

## Keybindings

GUI keybindings are configured under a `[keybindings]` table. They apply to the Draxul layer
only — Neovim key remapping still belongs in your Neovim config.

```toml
[keybindings]
toggle_diagnostics = "F12"
copy               = "Ctrl+Shift+C"
paste              = "Ctrl+Shift+V"
font_increase      = "Ctrl+="
font_decrease      = "Ctrl+-"
font_reset         = "Ctrl+0"
```

### Available Actions

| Action | Default | Description |
|--------|---------|-------------|
| `toggle_diagnostics` | `F12` | Show/hide the diagnostics panel |
| `copy` | `Ctrl+Shift+C` | Copy selected text to the system clipboard |
| `paste` | `Ctrl+Shift+V` | Paste from the system clipboard |
| `font_increase` | `Ctrl+=` | Increase font size by 1 point |
| `font_decrease` | `Ctrl+-` | Decrease font size by 1 point |
| `font_reset` | `Ctrl+0` | Reset font size to the configured default |

### Key Syntax

- Modifiers: `Ctrl` (or `Control`), `Shift`, `Alt`, `Super` (or `Meta` / `Gui`)
- Combine with `+`: `"Ctrl+Shift+V"`
- Symbol aliases: `=` (equals), `-` (minus), `+` (plus)
- Special keys: `F1`–`F12`, `Tab`, `Return`, `Escape`, `Space`, `Home`, `End`, `PageUp`, `PageDown`, arrow keys, etc.

To remove a default binding, set it to an empty string: `copy = ""`

---

## Font Settings

```toml
font_path      = "/path/to/font.ttf"   # required; no built-in default font
fallback_paths = [                      # optional; searched in order for missing glyphs
    "/path/to/fallback1.ttf",
    "/path/to/fallback2.ttf",
]
font_size         = 11      # points; range 6–36; default 11
enable_ligatures  = true    # programming ligatures (=>, !=, <-, etc.); default true
atlas_size        = 2048    # glyph atlas texture size in pixels; must be power of two;
                            # range 1024–8192; default 2048
```

### Font Details

- **`font_path`** — Path to the primary TTF or OTF font file. Draxul has no built-in fallback
  font; this must be set for text to render.
- **`fallback_paths`** — Fonts tried in order when the primary font does not contain a glyph
  (useful for CJK characters, emoji, Nerd Font icons, etc.).
- **`font_size`** — Base font size in points. Can also be adjusted at runtime with
  `font_increase` / `font_decrease` / `font_reset` keybindings; changes are saved automatically.
- **`enable_ligatures`** — When `true`, Draxul combines eligible two-cell programming ligatures
  during shaping. Requires an app restart to take effect after changing.
- **`atlas_size`** — Size of the glyph texture atlas. Increase if you use many different
  characters or large font sizes and see glyphs disappearing. Must be a power of two.

---

## Window Settings

```toml
window_width  = 1280   # initial window width in logical pixels; range 640–3840
window_height = 800    # initial window height in logical pixels; range 400–2160
```

- Values outside the valid range are clamped to the defaults.
- The window is also clamped to fit within the current display on startup.
- Window size is not persisted between sessions (see work item `36 window-state-persistence`).

---

## Terminal Behaviour

Draxul implements a VT/xterm-compatible terminal emulator. The following behaviours are
controlled by escape sequences sent from the running program (Neovim, shell, etc.) and are
listed here so you know what to expect.

### Screen Modes

| Mode | Behaviour |
|------|-----------|
| Normal screen | Standard terminal grid |
| Alternate screen (`DECSET 1049`) | Full-screen applications (Neovim, less, htop). Content saved on enter, restored on exit. If the window is resized while in alt-screen, the saved content is re-dimensioned before restore. |

### Cursor

- Shape: block, underline, or vertical bar — set by the host application.
- Blink: on or off — set by the host application.
- Visibility: can be hidden by the host during busy operations.

### Text Attributes (SGR)

Bold, italic, underline, undercurl, strikethrough, reverse video, and 24-bit true colour
are all supported. The full 256-colour xterm palette is also supported.

### Bracketed Paste (`DECSET 2004`)

When a program enables bracketed paste mode, Draxul wraps clipboard pastes with
`ESC[200~` … `ESC[201~` so the program can distinguish a paste from typed input.
Shells (bash, zsh, fish) and Neovim use this automatically.

### Line Wrap & Origin Mode

- Auto-wrap (`DECSET 7`) and origin mode (`DECSET 6`) behave as per xterm specification.
- Scroll regions (top/bottom margins) are supported.

---

## Mouse Support

Mouse reporting is controlled by the running program via DECSET escape sequences.

| Mode | DECSET code | Behaviour |
|------|-------------|-----------|
| Button tracking | 1000 | Reports button press and release only |
| Drag tracking | 1002 | Reports press, release, and motion while a button is held |
| Any-event tracking | 1003 | Reports all motion, with or without button |
| SGR extended format | 1006 | Uses SGR encoding for mouse reports (required for large terminals) |

When no mouse mode is active, the mouse is used only for Draxul-level text selection (see below).

---

## Text Selection & Clipboard

- **Select:** Click and drag with the left mouse button.
- **Copy:** Press the `copy` keybinding (`Ctrl+Shift+C` by default) while text is selected.
- **Paste:** Press the `paste` keybinding (`Ctrl+Shift+V` by default).
- Selection is cleared on any key press or new terminal output.
- Maximum selection size: **8192 cells**. Larger selections are truncated.

> **Note:** When a program has enabled terminal mouse reporting (e.g., Neovim with mouse
> support), mouse clicks and drags are forwarded to the program rather than creating a
> GUI selection. Hold `Shift` to force GUI selection in that case (if supported).

---

## Scrollback

- Draxul keeps up to **2000 lines** of scrollback history per session.
- Scroll with the **mouse wheel** or **Page Up / Page Down**.
- Any key press or new output returns the view to the live terminal.
- Scrollback is not persisted between sessions.
- The alternate screen (Neovim, less, etc.) does not accumulate scrollback — only the normal
  shell screen does.

---

## Host / Shell Selection

By default Draxul spawns `nvim --embed`. You can override this with the `--host` flag:

| Value | Program spawned |
|-------|-----------------|
| `nvim` | Neovim (default) |
| `powershell` / `pwsh` | PowerShell |
| `bash` | Bash |
| `zsh` | Zsh |

Example: `draxul --host zsh`

Draxul looks for the host binary on your `PATH`. Make sure `nvim` (or the chosen shell) is
accessible before launching.

---

## Command-Line Options

```
draxul [options]
```

| Option | Description |
|--------|-------------|
| `--host <kind>` | Override the host process: `nvim`, `powershell`, `pwsh`, `bash`, `zsh` |
| `--console` | **(Windows only)** Allocate a debug console window for log output |
| `--smoke-test` | Run a quick automated startup test and exit (used in CI) |
| `--render-test <scenario.toml>` | Run a visual render test against a reference image |
| `--bless-render-test` | Update the reference image (use with `--render-test`) |
| `--export-render-test <out.png>` | Export the captured frame to a PNG (use with `--render-test`) |

---

## Diagnostics Panel

Press `F12` (or your configured `toggle_diagnostics` binding) to show the diagnostics panel.

The panel displays live runtime state across three tabs:
- **Window** — window dimensions, terminal region, display PPI, cell size, and grid size
- **Renderer** — frame timing, rolling average, dirty-cell count, and glyph atlas statistics
- **Startup** — per-phase initialisation timing and total startup time

The diagnostics panel does not intercept any keyboard input — the terminal remains fully interactive while it is visible.

---

## Platform Notes

### Windows
- Renderer: **Vulkan** (requires a Vulkan-capable GPU and driver)
- Neovim must be on `PATH` or the full path specified
- Run with `--console` if you need to see log output in a terminal

### macOS
- Renderer: **Metal** (built-in; no external SDK required)
- Neovim must be on `PATH` (install via Homebrew: `brew install neovim`)
- High-DPI / Retina displays are handled automatically; moving the window between displays
  of different scales re-initialises the font metrics so text stays sharp

### Linux
*(Not yet officially supported; Vulkan backend may work but is untested)*
