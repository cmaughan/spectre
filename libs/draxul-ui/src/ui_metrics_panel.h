#pragma once

namespace draxul
{

struct PanelLayout;
struct DiagnosticPanelState;

void render_window_sections(const PanelLayout& layout, const DiagnosticPanelState& state);
void render_renderer_sections(const DiagnosticPanelState& state);
void render_startup_sections(const DiagnosticPanelState& state);

} // namespace draxul
