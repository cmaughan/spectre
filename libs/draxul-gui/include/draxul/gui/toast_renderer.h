#pragma once

#include <draxul/types.h>
#include <span>
#include <string>
#include <vector>

namespace draxul
{
class TextService;
}

namespace draxul::gui
{

enum class ToastLevel : uint8_t
{
    Info,
    Warn,
    Error,
};

struct ToastEntry
{
    ToastLevel level = ToastLevel::Info;
    std::string message;
    float remaining_s = 0.0f; // seconds until dismissed
    float total_s = 0.0f; // original duration (for fade calc)
};

struct ToastViewState
{
    int grid_cols = 0;
    int grid_rows = 0;
    std::span<const ToastEntry> entries;
};

/// Generate overlay CellUpdate cells for active toast notifications.
/// Toasts stack upward from the bottom-right corner of the grid.
[[nodiscard]] std::vector<CellUpdate> render_toasts(
    const ToastViewState& state,
    draxul::TextService& text_service);

} // namespace draxul::gui
