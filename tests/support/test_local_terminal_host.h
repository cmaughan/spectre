#pragma once

#include <draxul/local_terminal_host.h>

#include <string>
#include <string_view>
#include <vector>

namespace draxul::tests
{

class TestLocalTerminalHost : public LocalTerminalHost
{
public:
    void feed(std::string_view bytes)
    {
        consume_output(bytes);
    }

    std::string written;

    int cols_ = 20;
    int rows_ = 5;

protected:
    std::string_view host_name() const override
    {
        return "test";
    }

    bool initialize_host() override
    {
        highlights().set_default_fg({ 1.0f, 1.0f, 1.0f, 1.0f });
        highlights().set_default_bg({ 0.0f, 0.0f, 0.0f, 1.0f });
        apply_grid_size(cols_, rows_);
        reset_terminal_state();
        set_content_ready(true);
        return true;
    }

    bool do_process_write(std::string_view text) override
    {
        written += text;
        return true;
    }

    std::vector<std::string> do_process_drain() override
    {
        return {};
    }

    bool do_process_resize(int, int) override
    {
        return true;
    }

    bool do_process_is_running() const override
    {
        return true;
    }

    void do_process_shutdown() override {}
};

} // namespace draxul::tests
