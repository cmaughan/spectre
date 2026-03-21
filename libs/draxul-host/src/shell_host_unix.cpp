#include "unix_pty_process.h"
#include <draxul/terminal_host_base.h>

#include <draxul/host_kind.h>

namespace draxul
{

namespace
{

class ShellHost : public TerminalHostBase
{
public:
    std::string_view host_name() const override
    {
        return launch_options().kind == HostKind::Zsh ? "zsh" : "bash";
    }

protected:
    bool initialize_host() override
    {
        highlights().set_default_fg(Color{ 0.92f, 0.92f, 0.92f, 1.0f });
        highlights().set_default_bg(Color{ 0.08f, 0.09f, 0.10f, 1.0f });
        apply_grid_size(viewport().cols, viewport().rows);
        reset_terminal_state();

        std::string command = launch_options().command;
        if (command.empty())
            command = (launch_options().kind == HostKind::Zsh) ? "zsh" : "bash";

        auto wake = callbacks().wake_window;
        if (!process_.spawn(command, launch_options().args, launch_options().working_dir, [wake]() {
                if (wake)
                    wake(); }, viewport().cols, viewport().rows))
        {
            set_init_error("Could not start " + command
                + ". Please ensure it is installed and available on your PATH.");
            return false;
        }

        for (const auto& cmd : launch_options().startup_commands)
            process_.write(cmd + "\r");

        update_cursor_style();
        return true;
    }

    bool do_process_write(std::string_view text) override
    {
        return process_.write(text);
    }
    std::vector<std::string> do_process_drain() override
    {
        return process_.drain_output();
    }
    bool do_process_resize(int cols, int rows) override
    {
        return process_.resize(cols, rows);
    }
    bool do_process_is_running() const override
    {
        return process_.is_running();
    }
    void do_process_shutdown() override
    {
        process_.shutdown();
    }

private:
    UnixPtyProcess process_;
};

} // namespace

std::unique_ptr<IHost> create_shell_host()
{
    return std::make_unique<ShellHost>();
}

} // namespace draxul
