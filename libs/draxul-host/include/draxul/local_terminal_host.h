#pragma once

#include <draxul/mouse_reporter.h>
#include <draxul/scrollback_buffer.h>
#include <draxul/selection_manager.h>
#include <draxul/terminal_host_base.h>

namespace draxul
{

// Intermediate base for local-process terminal hosts (shell, PowerShell, WSL).
// Extends TerminalHostBase with scrollback, selection, and mouse reporting —
// features that only make sense for hosts backed by a local PTY or ConPTY.
// NvimHost derives from GridHostBase directly and does not use this class.
class LocalTerminalHost : public TerminalHostBase
{
public:
    LocalTerminalHost();

    void pump() override;
    void on_key(const KeyEvent& event) override;
    void on_text_input(const TextInputEvent& event) override;
    void on_mouse_button(const MouseButtonEvent& event) override;
    void on_mouse_move(const MouseMoveEvent& event) override;
    void on_mouse_wheel(const MouseWheelEvent& event) override;
    bool dispatch_action(std::string_view action) override;

protected:
    void on_viewport_changed() override;
    void reset_terminal_state() override;

private:
    struct GridPos
    {
        int col = 0;
        int row = 0;
    };

    GridPos pixel_to_cell(int px, int py) const;
    void on_line_scrolled_off(int row) override;
    void on_mouse_mode_changed(int mode, bool enable) override;

    MouseReporter mouse_reporter_;
    SelectionManager selection_;
    ScrollbackBuffer scrollback_;
};

} // namespace draxul
