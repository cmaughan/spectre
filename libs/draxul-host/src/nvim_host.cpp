#include "nvim_host.h"
#include <draxul/clipboard_util.h>

#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/text_service.h>
#include <draxul/window.h>
#include <thread>

namespace draxul
{

namespace
{

void apply_ui_option(UiOptions& options, const std::string& name, const MpackValue& value)
{
    if (name == "ambiwidth" && value.type() == MpackValue::String)
        options.ambiwidth = (value.as_str() == "double") ? AmbiWidth::Double : AmbiWidth::Single;
}

} // namespace

bool NvimHost::initialize_host()
{
    PERF_MEASURE();
    const std::string command = launch_options().command.empty() ? "nvim" : launch_options().command;
    if (!nvim_process_.spawn(command, launch_options().args, launch_options().working_dir))
    {
        init_error_ = "Could not start nvim. Please install Neovim and ensure it is on your PATH.";
        return false;
    }

    // Set callbacks BEFORE initialize() starts the reader thread to avoid
    // a race where the thread invokes a default-constructed std::function.
    rpc_.on_notification_available = [this]() {
        callbacks().wake_window();
    };
    rpc_.on_request = [this](const std::string& method, const std::vector<MpackValue>& params) {
        return handle_rpc_request(method, params);
    };

    if (!rpc_.initialize(nvim_process_))
    {
        init_error_ = "Neovim exited unexpectedly during startup.";
        return false;
    }

    wire_ui_callbacks();

    const auto& metrics = text_service().metrics();
    input_.initialize(&rpc_, metrics.cell_width, metrics.cell_height);
    ui_request_worker_.start(&rpc_);

    apply_grid_size(viewport().grid_size.x, viewport().grid_size.y);
    if (!attach_ui())
    {
        shutdown();
        return false;
    }
    if (!execute_startup_commands())
    {
        shutdown();
        return false;
    }
    setup_clipboard_provider();
    refresh_cursor_style();

    // All startup requests done — from this point on request() must not be
    // called from the main thread (it blocks on nvim response).
    rpc_.set_main_thread_id(std::this_thread::get_id());
    return true;
}

bool NvimHost::is_running() const
{
    // Treat a broken RPC pipe as "not running": nvim closes stdout before the
    // OS process fully exits, so waitpid(WNOHANG) can still return 0 for a
    // brief window after the pipe EOF.  Checking read_failed_ avoids the main
    // loop blocking in wait_events() waiting for a user event to notice.
    return nvim_process_.is_running() && !rpc_.connection_failed();
}

std::string NvimHost::status_text() const
{
    return "nvim | " + std::to_string(grid_cols()) + "x" + std::to_string(grid_rows());
}

std::string NvimHost::init_error() const
{
    return init_error_;
}

void NvimHost::shutdown()
{
    PERF_MEASURE();
    if (nvim_process_.is_running())
        rpc_.notify("nvim_input", { NvimRpc::make_str("<C-\\><C-n>:qa!<CR>") });

    rpc_.close();
    ui_request_worker_.stop();
    nvim_process_.shutdown();
    rpc_.shutdown();
}

void NvimHost::pump()
{
    PERF_MEASURE();
    if (!nvim_process_.is_running())
        return;

    // Refresh the clipboard cache on the main thread so that
    // handle_rpc_request() (reader thread) never calls SDL directly.
    {
        auto text = window().clipboard_text();
        std::lock_guard<std::mutex> lock(clipboard_mutex_);
        clipboard_cache_ = std::move(text);
    }

    auto notifications = rpc_.drain_notifications();
    for (auto& notification : notifications)
    {
        if (notification.method == "redraw")
            ui_events_.process_redraw(notification.params);
        else if (notification.method == "clipboard_set")
            handle_clipboard_set(notification.params);
    }

    advance_cursor_blink(std::chrono::steady_clock::now());
}

void NvimHost::on_key(const KeyEvent& event)
{
    input_.on_key(event);
}

void NvimHost::on_text_input(const TextInputEvent& event)
{
    input_.on_text_input(event);
}

void NvimHost::on_text_editing(const TextEditingEvent& event)
{
    input_.on_text_editing(event);
}

void NvimHost::on_mouse_button(const MouseButtonEvent& event)
{
    input_.on_mouse_button(event);
}

void NvimHost::on_mouse_move(const MouseMoveEvent& event)
{
    input_.on_mouse_move(event);
}

void NvimHost::on_mouse_wheel(const MouseWheelEvent& event)
{
    input_.on_mouse_wheel(event);
}

bool NvimHost::dispatch_action(std::string_view action)
{
    PERF_MEASURE();
    if (action == "copy")
    {
        if (clipboard_channel_id_ >= 0)
        {
            static constexpr const char* kCopyLua = R"(
local ch = ...
local lines = vim.fn.getreg('"', true, true)
local regtype = vim.fn.getregtype('"')
vim.fn.rpcnotify(ch, 'clipboard_set', '"', lines, regtype)
)";
            rpc_.notify("nvim_exec_lua",
                { NvimRpc::make_str(kCopyLua), NvimRpc::make_array({ NvimRpc::make_int(clipboard_channel_id_) }) });
        }
        return true;
    }

    if (action == "paste")
    {
        input_.paste_text(window().clipboard_text());
        return true;
    }

    if (action.starts_with("open_file_at_function:"))
    {
        // Format: open_file_at_function:path|qualified_name|function_name
        const std::string_view rest = std::string_view(action).substr(22);
        const size_t sep2 = rest.rfind('|');
        const size_t sep1 = (sep2 != std::string_view::npos) ? rest.rfind('|', sep2 - 1) : std::string_view::npos;
        if (sep1 != std::string_view::npos && sep2 != std::string_view::npos && sep1 < sep2)
        {
            const std::string path(rest.substr(0, sep1));
            const std::string qualified(rest.substr(sep1 + 1, sep2 - sep1 - 1));
            const std::string func(rest.substr(sep2 + 1));
            // Try qualified definition first (e.g. "Class::func("), then bare "func(",
            // then struct/class definition (e.g. "struct func").
            static constexpr const char* kOpenAndSearchLua = R"(
local path, qualified, func = ...
vim.cmd.edit(vim.fn.fnameescape(path))
vim.cmd('normal! gg')
if qualified ~= '' then
    local qualified_pat = qualified .. '::' .. func .. [[\s*(]]
    if vim.fn.search(qualified_pat) > 0 then return end
end
local bare_pat = [[\<]] .. func .. [[\s*(]]
if vim.fn.search(bare_pat) > 0 then return end
local struct_pat = [[\<\(struct\|class\|enum\|union\)\s\+]] .. func .. [[\>]]
vim.fn.search(struct_pat)
)";
            rpc_.notify("nvim_exec_lua",
                { NvimRpc::make_str(kOpenAndSearchLua),
                    NvimRpc::make_array({ NvimRpc::make_str(path), NvimRpc::make_str(qualified),
                        NvimRpc::make_str(func) }) });
        }
        return true;
    }

    if (action.starts_with("open_file_at_type:"))
    {
        // Format: open_file_at_type:path|qualified_name
        const std::string_view rest = std::string_view(action).substr(18);
        const size_t sep = rest.rfind('|');
        if (sep != std::string_view::npos)
        {
            const std::string path(rest.substr(0, sep));
            const std::string qualified(rest.substr(sep + 1));
            static constexpr const char* kOpenAndSearchTypeLua = R"(
local path, qualified = ...
vim.cmd.edit(vim.fn.fnameescape(path))
vim.cmd('normal! gg')
local name = qualified
local last = qualified:match(".*::([^:]+)$")
if last ~= nil and last ~= '' then
    name = last
end
local type_pat = [[\<\(struct\|class\|enum\|union\)\s\+]] .. name .. [[\>]]
if vim.fn.search(type_pat) > 0 then return end
local using_pat = [[\<using\s\+]] .. name .. [[\>]]
if vim.fn.search(using_pat) > 0 then return end
local typedef_pat = [[\<typedef\>.\{-}\<]] .. name .. [[\>]]
vim.fn.search(typedef_pat)
)";
            rpc_.notify("nvim_exec_lua",
                { NvimRpc::make_str(kOpenAndSearchTypeLua),
                    NvimRpc::make_array({ NvimRpc::make_str(path), NvimRpc::make_str(qualified) }) });
        }
        return true;
    }

    if (action.starts_with("open_file:"))
    {
        const std::string path(action.substr(10));
        rpc_.notify("nvim_exec_lua",
            { NvimRpc::make_str("vim.cmd.edit(vim.fn.fnameescape(...))"),
                NvimRpc::make_array({ NvimRpc::make_str(path) }) });
        return true;
    }

    return false;
}

void NvimHost::request_close()
{
    if (nvim_process_.is_running())
        rpc_.notify("nvim_input", { NvimRpc::make_str("<C-\\><C-n>:qa!<CR>") });
}

void NvimHost::on_viewport_changed()
{
    PERF_MEASURE();
    input_.set_viewport_origin(viewport().pixel_pos.x + renderer().padding(), viewport().pixel_pos.y + renderer().padding());
    const int new_cols = std::max(1, viewport().grid_size.x);
    const int new_rows = std::max(1, viewport().grid_size.y);
    if (grid_cols() == 0 || grid_rows() == 0)
    {
        apply_grid_size(new_cols, new_rows);
        return;
    }

    if (new_cols == grid_cols() && new_rows == grid_rows())
        return;

    mark_activity();
    if (!runtime_state().content_ready)
    {
        startup_resize_state_.defer(new_cols, new_rows);
        return;
    }

    queue_resize_request(new_cols, new_rows, "window resize");
}

void NvimHost::on_font_metrics_changed_impl()
{
    PERF_MEASURE();
    const auto& metrics = text_service().metrics();
    input_.set_cell_size(metrics.cell_width, metrics.cell_height);
    flush_grid();
    refresh_cursor_style();

    const int new_cols = std::max(1, viewport().grid_size.x);
    const int new_rows = std::max(1, viewport().grid_size.y);
    if (new_cols != grid_cols() || new_rows != grid_rows())
        queue_resize_request(new_cols, new_rows, "font resize");
}

bool NvimHost::attach_ui()
{
    PERF_MEASURE();
    auto attach = rpc_.request("nvim_ui_attach", {
                                                     NvimRpc::make_int(grid_cols()),
                                                     NvimRpc::make_int(grid_rows()),
                                                     NvimRpc::make_map({
                                                         { NvimRpc::make_str("rgb"), NvimRpc::make_bool(true) },
                                                         { NvimRpc::make_str("ext_linegrid"), NvimRpc::make_bool(true) },
                                                         { NvimRpc::make_str("ext_multigrid"), NvimRpc::make_bool(false) },
                                                     }),
                                                 });
    if (!attach.ok())
    {
        DRAXUL_LOG_ERROR(LogCategory::App, "nvim_ui_attach failed");
        init_error_ = "Neovim rejected UI attach.";
        return false;
    }
    return true;
}

bool NvimHost::execute_startup_commands()
{
    PERF_MEASURE();
    for (const auto& command : launch_options().startup_commands)
    {
        auto response = rpc_.request("nvim_command", { NvimRpc::make_str(command) });
        if (!response.ok())
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "Startup command failed: %s", command.c_str());
            init_error_ = "A startup command failed while initializing Neovim.";
            return false;
        }
    }
    return true;
}

bool NvimHost::setup_clipboard_provider()
{
    PERF_MEASURE();
    auto api_info = rpc_.request("nvim_get_api_info", {});
    if (!api_info.ok() || api_info.result.type() != MpackValue::Array || api_info.result.as_array().empty())
        return true;

    clipboard_channel_id_ = api_info.result.as_array()[0].as_int();

    static constexpr const char* kClipboardLua = R"(
local channel = ...
vim.g.clipboard = {
  name = 'draxul',
  copy = {
    ['+'] = function(lines, regtype) vim.fn.rpcnotify(channel, 'clipboard_set', '+', lines, regtype) end,
    ['*'] = function(lines, regtype) vim.fn.rpcnotify(channel, 'clipboard_set', '*', lines, regtype) end,
  },
  paste = {
    ['+'] = function() return vim.fn.rpcrequest(channel, 'clipboard_get', '+') end,
    ['*'] = function() return vim.fn.rpcrequest(channel, 'clipboard_get', '*') end,
  },
  cache_enabled = 0,
}
)";

    auto result = rpc_.request("nvim_exec_lua", {
                                                    NvimRpc::make_str(kClipboardLua),
                                                    NvimRpc::make_array({ NvimRpc::make_int(clipboard_channel_id_) }),
                                                });
    return result.ok();
}

void NvimHost::queue_resize_request(int cols, int rows, const char* reason)
{
    ui_request_worker_.request_resize(cols, rows, reason);
}

void NvimHost::on_flush()
{
    PERF_MEASURE();
    const bool first_flush = !runtime_state().content_ready;
    flush_grid();
    if (first_flush && startup_resize_state_.pending())
    {
        if (auto deferred = startup_resize_state_.consume_if_needed(grid_cols(), grid_rows()))
            queue_resize_request(deferred->first, deferred->second, "startup resize");
    }
    refresh_cursor_style();
}

void NvimHost::on_busy(bool busy)
{
    mark_activity();
    set_cursor_busy(busy);
}

void NvimHost::refresh_cursor_style()
{
    PERF_MEASURE();
    CursorStyle style = {};
    style.bg = highlights().default_fg();
    style.fg = highlights().default_bg();

    const int mode = ui_events_.current_mode();
    if (mode >= 0 && mode < static_cast<int>(ui_events_.modes().size()))
    {
        const auto& mode_info = ui_events_.modes()[mode];
        style.shape = mode_info.cursor_shape;
        style.cell_percentage = mode_info.cell_percentage;
        if (mode_info.attr_id != 0)
        {
            const auto& attr = highlights().get(static_cast<uint16_t>(mode_info.attr_id));
            // Only use explicit cursor colors when the attribute carries meaningful cursor
            // styling. An attr with no reverse and no explicit fg/bg resolves to
            // (default_fg, default_bg) — normal text colors — which would make the cursor
            // invisible against the background.
            if (attr.has_fg || attr.has_bg || attr.reverse)
            {
                Color fg;
                Color bg;
                highlights().resolve(attr, fg, bg);
                style.fg = fg;
                style.bg = bg;
                style.use_explicit_colors = true;
            }
        }
    }

    set_cursor_style(style, current_blink_timing());
}

BlinkTiming NvimHost::current_blink_timing() const
{
    const int mode = ui_events_.current_mode();
    if (mode < 0 || mode >= static_cast<int>(ui_events_.modes().size()))
        return {};

    const auto& info = ui_events_.modes()[mode];
    return { info.blinkwait, info.blinkon, info.blinkoff };
}

MpackValue NvimHost::handle_rpc_request(const std::string& method, const std::vector<MpackValue>&) const
{
    if (method == "clipboard_get")
    {
        // Read from the main-thread-refreshed cache rather than calling
        // SDL_GetClipboardText() here (we run on the reader thread).
        std::string text;
        {
            std::lock_guard<std::mutex> lock(clipboard_mutex_);
            text = clipboard_cache_;
        }
        return clipboard_text_to_response(text);
    }

    return NvimRpc::make_nil();
}

void NvimHost::handle_clipboard_set(const std::vector<MpackValue>& params) const
{
    if (!window().set_clipboard_text(clipboard_params_to_text(params)))
        callbacks().push_toast(1, "Clipboard write failed");
}

void NvimHost::wire_ui_callbacks()
{
    PERF_MEASURE();
    ui_events_.set_grid(&grid());
    ui_events_.set_highlights(&highlights());
    ui_events_.set_options(&ui_options_);
    ui_events_.on_flush = [this]() { on_flush(); };
    ui_events_.on_grid_resize = [this](int cols, int rows) {
        apply_grid_size(cols, rows);
        input_.set_grid_size(cols, rows);
    };
    ui_events_.on_cursor_goto = [this](int col, int row) { set_cursor_position(col, row); };
    ui_events_.on_mode_change = [this](int) { refresh_cursor_style(); };
    ui_events_.on_option_set = [this](const std::string& name, const MpackValue& value) {
        apply_ui_option(ui_options_, name, value);
    };
    ui_events_.on_busy = [this](bool busy) { on_busy(busy); };
    ui_events_.on_title = [this](const std::string& title) {
        callbacks().set_window_title(title.empty() ? "Draxul" : title);
    };
}

} // namespace draxul
