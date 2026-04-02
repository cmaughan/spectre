#pragma once

#include <draxul/terminal_host_base.h>

#include <string>
#include <string_view>
#include <vector>

namespace draxul::tests
{

class TestVtTerminalHost : public TerminalHostBase
{
public:
    void feed(std::string_view bytes)
    {
        consume_output(bytes);
    }

    std::string cell_text(int col, int row)
    {
        return std::string(grid().get_cell(col, row).text.view());
    }

    uint16_t cell_hl(int col, int row)
    {
        return grid().get_cell(col, row).hl_attr_id;
    }

    int col() const
    {
        return cursor_col();
    }

    int row() const
    {
        return cursor_row();
    }

    int scroll_top() const
    {
        return vt_state().scroll_top;
    }

    int scroll_bottom() const
    {
        return vt_state().scroll_bottom;
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
