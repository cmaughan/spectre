#include <draxul/terminal_host_base.h>

#include <draxul/terminal_key_encoder.h>
#include <draxul/terminal_sgr.h>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <draxul/alt_screen_manager.h>
#include <draxul/base64.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/unicode.h>
#include <draxul/vt_parser.h>
#include <draxul/window.h>
#include <functional>
#include <utility>
#include <unordered_map>

namespace draxul
{

namespace
{

constexpr auto kOutputCursorSettleDelay = std::chrono::milliseconds(12);
constexpr auto kAltScreenCursorMoveSettleDelay = std::chrono::milliseconds(40);

void set_grid_cell_for_alt_screen(Grid& grid, int col, int row, const Cell& cell)
{
    grid.set_cell(col, row, std::string(cell.text.view()), cell.hl_attr_id, cell.double_width);
}

std::string describe_text_for_log(std::string_view text)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 4 + 2);
    out.push_back('"');
    for (unsigned char ch : text)
    {
        switch (ch)
        {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (ch >= 0x20 && ch <= 0x7E)
            {
                out.push_back(static_cast<char>(ch));
            }
            else
            {
                out += "\\x";
                out.push_back(kHex[(ch >> 4) & 0xF]);
                out.push_back(kHex[ch & 0xF]);
            }
            break;
        }
    }
    out.push_back('"');
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

TerminalHostBase::TerminalHostBase()
    : vt_parser_(VtParser::Callbacks{
          std::bind_front(&TerminalHostBase::write_cluster, this),
          std::bind_front(&TerminalHostBase::handle_control, this),
          std::bind_front(&TerminalHostBase::handle_csi, this),
          std::bind_front(&TerminalHostBase::handle_osc, this),
          std::bind_front(&TerminalHostBase::handle_esc, this),
      })
    , alt_screen_(AltScreenManager::GridAccessors{
          std::bind_front(&TerminalHostBase::grid_cols, this),
          std::bind_front(&TerminalHostBase::grid_rows, this),
          std::bind_front(std::mem_fn(&Grid::get_cell), std::ref(grid())),
          std::bind_front(set_grid_cell_for_alt_screen, std::ref(grid())),
          std::bind_front(std::mem_fn(&Grid::clear), std::ref(grid())),
      })
{
}

// ---------------------------------------------------------------------------
// pump / key / input / action
// ---------------------------------------------------------------------------

void TerminalHostBase::pump()
{
    PERF_MEASURE();
    ensure_pty_capture_ready();
    auto chunks = do_process_drain();
    if (!chunks.empty())
    {
        // Process all available output, re-draining after each batch so that
        // closely-spaced bursts are coalesced into a single flush. Bounded by
        // an 8 ms wall-clock budget so that runaway producers (e.g. `yes`,
        // `cat /dev/urandom`) cannot trap the main thread and starve the SDL
        // event loop — any remaining output is picked up on the next frame.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
        bool budget_exceeded = false;
        begin_output_cursor_batch();
        do
        {
            for (const auto& chunk : chunks)
            {
                maybe_capture_pty_chunk(chunk);
                consume_output(chunk);
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                budget_exceeded = true;
                break;
            }
            chunks = do_process_drain();
        } while (!chunks.empty());
        end_output_cursor_batch();

        if (budget_exceeded)
        {
            DRAXUL_LOG_DEBUG(LogCategory::App,
                "TerminalHostBase::pump drain budget (8 ms) exceeded; "
                "deferring remaining output to next frame");
        }

        if (!synchronized_output_active())
            flush_grid();
    }
    const auto now = std::chrono::steady_clock::now();
    apply_deferred_cursor_visibility_if_due(now);
    advance_cursor_blink(now);
}

std::optional<std::chrono::steady_clock::time_point> TerminalHostBase::next_deadline() const
{
    const auto base_deadline = GridHostBase::next_deadline();
    if (!deferred_cursor_visibility_deadline_)
        return base_deadline;
    if (!base_deadline || *deferred_cursor_visibility_deadline_ < *base_deadline)
        return deferred_cursor_visibility_deadline_;
    return base_deadline;
}

void TerminalHostBase::on_key(const KeyEvent& event)
{
    PERF_MEASURE();
    if (!event.pressed)
        return;
    const std::string sequence = encode_terminal_key(event, vt_);
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        const std::string encoded = describe_text_for_log(sequence);
        log_printf(LogLevel::Trace, LogCategory::Input,
            "input trace: terminal_host_base on_key key=%d mod=0x%X encoded=%s",
            event.keycode,
            static_cast<unsigned int>(event.mod),
            encoded.c_str());
    }
    if (!sequence.empty())
        do_process_write(sequence);
}

void TerminalHostBase::on_text_input(const TextInputEvent& event)
{
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        const std::string described = describe_text_for_log(event.text);
        log_printf(LogLevel::Trace, LogCategory::Input,
            "input trace: terminal_host_base on_text_input text=%s len=%zu",
            described.c_str(),
            event.text.size());
    }
    if (!event.text.empty())
        do_process_write(event.text);
}

void TerminalHostBase::on_config_reloaded(const HostReloadConfig& config)
{
    GridHostBase::on_config_reloaded(config);

    launch_options().terminal_fg = config.terminal_fg;
    launch_options().terminal_bg = config.terminal_bg;

    highlights().set_default_fg(
        launch_options().terminal_fg.value_or(Color(0.92f, 0.92f, 0.92f, 1.0f)));
    highlights().set_default_bg(
        launch_options().terminal_bg.value_or(Color(0.08f, 0.09f, 0.10f, 1.0f)));
    force_full_redraw();
    flush_grid();
    update_cursor_style();
}

bool TerminalHostBase::dispatch_action(std::string_view action)
{
    PERF_MEASURE();
    if (action == "paste")
    {
        const std::string clip = window().clipboard_text();
        const int threshold = launch_options().paste_confirm_lines;
        if (threshold > 0 && !clip.empty())
        {
            const int newlines = static_cast<int>(std::count(clip.begin(), clip.end(), '\n'));
            // A clipboard payload of N newlines pastes N+1 logical lines.
            if (newlines + 1 >= threshold)
            {
                if (!pending_paste_.empty())
                {
                    callbacks().push_toast(
                        1, "Previous pending paste was replaced by a new paste.");
                }
                pending_paste_ = clip;
                char msg[160];
                std::snprintf(msg, sizeof(msg),
                    "Paste %d lines? Run confirm_paste to proceed, cancel_paste to discard.",
                    newlines + 1);
                callbacks().push_toast(1, msg);
                return true;
            }
        }
        send_paste(clip);
        return true;
    }
    if (action == "confirm_paste")
    {
        if (!pending_paste_.empty())
        {
            send_paste(pending_paste_);
            pending_paste_.clear();
        }
        return true;
    }
    if (action == "cancel_paste")
    {
        if (!pending_paste_.empty())
        {
            pending_paste_.clear();
            callbacks().push_toast(0, "Paste cancelled.");
        }
        return true;
    }
    return false;
}

void TerminalHostBase::send_paste(std::string_view text)
{
    PERF_MEASURE();
    if (bracketed_paste_mode_)
    {
        std::string wrapped;
        wrapped.reserve(text.size() + 12);
        wrapped += "\x1B[200~";
        wrapped += text;
        wrapped += "\x1B[201~";
        do_process_write(wrapped);
    }
    else
    {
        do_process_write(std::string(text));
    }
}

bool TerminalHostBase::ensure_pty_capture_ready()
{
    if (!pty_capture_config_loaded_)
    {
        pty_capture_config_loaded_ = true;
        if (!launch_options().pty_capture_file.empty())
            pty_capture_path_ = launch_options().pty_capture_file;
        else if (const char* value = std::getenv("DRAXUL_CAPTURE_PTY_FILE"))
            pty_capture_path_ = value;
    }

    if (pty_capture_path_.empty())
        return false;

    if (!pty_capture_header_checked_)
    {
        pty_capture_header_checked_ = true;

        const std::filesystem::path capture_path(pty_capture_path_);
        bool needs_header = true;
        std::ifstream existing(capture_path, std::ios::binary);
        if (existing)
        {
            std::string header_line;
            if (std::getline(existing, header_line) && header_line == "draxul-pty-capture-v1")
                needs_header = false;
        }

        if (needs_header)
        {
            std::ofstream out(capture_path, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                if (!pty_capture_failure_reported_)
                {
                    pty_capture_failure_reported_ = true;
                    DRAXUL_LOG_WARN(LogCategory::App,
                        "Failed to open PTY capture file '%s' for writing",
                        pty_capture_path_.c_str());
                }
                return false;
            }
            out << "draxul-pty-capture-v1\n";
        }

        if (!pty_capture_announced_)
        {
            pty_capture_announced_ = true;
            DRAXUL_LOG_INFO(LogCategory::App,
                "PTY capture enabled for host '%.*s': %s",
                static_cast<int>(host_name().size()),
                host_name().data(),
                pty_capture_path_.c_str());
        }
    }
    return true;
}

void TerminalHostBase::maybe_capture_pty_chunk(std::string_view bytes)
{
    if (!ensure_pty_capture_ready())
        return;

    std::ofstream out(pty_capture_path_, std::ios::binary | std::ios::app);
    if (!out)
    {
        if (!pty_capture_failure_reported_)
        {
            pty_capture_failure_reported_ = true;
            DRAXUL_LOG_WARN(LogCategory::App,
                "Failed to append PTY capture chunk to '%s'",
                pty_capture_path_.c_str());
        }
        return;
    }

    out << "chunk " << base64_encode(host_name()) << ' ' << base64_encode(bytes) << '\n';
}

// ---------------------------------------------------------------------------
// Viewport / font changes
// ---------------------------------------------------------------------------

void TerminalHostBase::on_viewport_changed()
{
    PERF_MEASURE();
    const int new_cols = std::max(1, viewport().grid_size.x);
    const int new_rows = std::max(1, viewport().grid_size.y);
    if (new_cols == grid_cols() && new_rows == grid_rows())
        return;

    DRAXUL_LOG_DEBUG(LogCategory::App,
        "terminal: on_viewport_changed %dx%d -> %dx%d",
        grid_cols(), grid_rows(), new_cols, new_rows);

    // If we are in alt-screen mode the saved main-screen snapshot must be
    // re-dimensioned to match the new terminal size so that restoring it on
    // alt-screen exit does not produce misaligned content.
    if (alt_screen_.in_alt_screen())
    {
        // The snapshot was captured at the old grid dimensions (grid_cols() x grid_rows()),
        // which are still valid here before apply_grid_size() is called.
        alt_screen_.resize_snapshot(new_cols, new_rows, grid_cols(), grid_rows());
        alt_screen_.clamp_saved_cursor(std::max(0, new_cols - 1), std::max(0, new_rows - 1));
    }

    apply_grid_size(new_cols, new_rows);
    vt_.col = std::clamp(vt_.col, 0, std::max(0, grid_cols() - 1));
    vt_.row = std::clamp(vt_.row, 0, std::max(0, grid_rows() - 1));
    vt_.saved_col = std::clamp(vt_.saved_col, 0, std::max(0, grid_cols() - 1));
    vt_.saved_row = std::clamp(vt_.saved_row, 0, std::max(0, grid_rows() - 1));
    vt_.scroll_top = 0;
    vt_.scroll_bottom = grid_rows() - 1;
    set_cursor_position(vt_.col, vt_.row, CursorBlinkUpdate::Preserve);
    do_process_resize(new_cols, new_rows);
    force_full_redraw();
    flush_grid();
    update_cursor_style();
}

void TerminalHostBase::on_font_metrics_changed_impl()
{
    PERF_MEASURE();
    force_full_redraw();
    flush_grid();
}

// ---------------------------------------------------------------------------
// Terminal state reset
// ---------------------------------------------------------------------------

void TerminalHostBase::reset_terminal_state()
{
    PERF_MEASURE();
    current_attr_ = {};
    attr_cache_.clear();
    vt_parser_.reset();
    vt_.col = 0;
    vt_.row = 0;
    vt_.saved_col = 0;
    vt_.saved_row = 0;
    vt_.cursor_visible = true;
    vt_.pending_wrap = false;
    vt_.scroll_top = 0;
    vt_.scroll_bottom = std::max(0, grid_rows() - 1);
    vt_.auto_wrap_mode = true;
    vt_.origin_mode = false;
    vt_.cursor_app_mode = false;
    vt_.cursor_shape = CursorShape::Block;
    vt_.cursor_blink = false;
    bracketed_paste_mode_ = false;
    cursor_repaint_save_active_ = false;
    cursor_repaint_display_frozen_ = false;
    cursor_repaint_chunk_activity_ = false;
    cursor_repaint_visibility_deferred_ = false;
    deferred_cursor_visibility_deadline_.reset();
    cursor_repaint_frozen_col_ = 0;
    cursor_repaint_frozen_row_ = 0;
    output_cursor_batch_active_ = false;
    output_cursor_batch_activity_ = false;
    output_cursor_batch_saw_repaint_scope_ = false;
    output_cursor_batch_ended_synchronized_output_ = false;
    output_cursor_batch_start_col_ = 0;
    output_cursor_batch_start_row_ = 0;
    output_cursor_batch_start_visible_ = true;
    synchronized_output_mode_ = false;
    set_cursor_display_override(std::nullopt);
    alt_screen_.reset();
}

void TerminalHostBase::update_cursor_style()
{
    PERF_MEASURE();
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace,
            LogCategory::Input,
            "cursor trace: update_cursor_style vt_visible=%d shape=%d blink=%d logical=(%d,%d) repaint_scope=%d deferred_vis=%d",
            vt_.cursor_visible ? 1 : 0,
            static_cast<int>(vt_.cursor_shape),
            vt_.cursor_blink ? 1 : 0,
            vt_.col,
            vt_.row,
            cursor_repaint_save_active_ ? 1 : 0,
            cursor_repaint_visibility_deferred_ ? 1 : 0);
    }
    CursorStyle style = {};
    style.shape = vt_.cursor_shape;
    style.bg = highlights().default_fg();
    style.fg = highlights().default_bg();

    // Apply blink timing when the shell requests a blinking cursor shape
    // (DECSCUSR odd Ps values). Standard terminal blink cadence: 530ms.
    BlinkTiming blink{};
    if (vt_.cursor_blink)
    {
        blink.blinkwait = 530;
        blink.blinkon = 530;
        blink.blinkoff = 530;
    }
    set_cursor_style(style, blink, !vt_.cursor_visible);
}

void TerminalHostBase::begin_cursor_repaint_scope()
{
    PERF_MEASURE();
    cursor_repaint_chunk_activity_ = true;
    output_cursor_batch_saw_repaint_scope_ = true;
    if (cursor_repaint_save_active_)
        return;

    cursor_repaint_save_active_ = true;
    cursor_repaint_display_frozen_ = false;
    if (deferred_cursor_visibility_deadline_)
    {
        deferred_cursor_visibility_deadline_.reset();
        cursor_repaint_visibility_deferred_ = true;
    }
    else
    {
        cursor_repaint_visibility_deferred_ = false;
    }
    cursor_repaint_frozen_col_ = vt_.col;
    cursor_repaint_frozen_row_ = vt_.row;
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace,
            LogCategory::Input,
            "cursor trace: begin_repaint_scope freeze=(%d,%d) vt_visible=%d deferred_vis=%d",
            cursor_repaint_frozen_col_,
            cursor_repaint_frozen_row_,
            vt_.cursor_visible ? 1 : 0,
            cursor_repaint_visibility_deferred_ ? 1 : 0);
    }
}

void TerminalHostBase::end_cursor_repaint_scope()
{
    PERF_MEASURE();
    cursor_repaint_chunk_activity_ = true;
    output_cursor_batch_saw_repaint_scope_ = true;
    cursor_repaint_save_active_ = false;
    cursor_repaint_display_frozen_ = false;
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace,
            LogCategory::Input,
            "cursor trace: end_repaint_scope logical=(%d,%d) vt_visible=%d deferred_vis=%d",
            vt_.col,
            vt_.row,
            vt_.cursor_visible ? 1 : 0,
            cursor_repaint_visibility_deferred_ ? 1 : 0);
    }
    if (cursor_repaint_visibility_deferred_)
    {
        cursor_repaint_visibility_deferred_ = false;
        update_cursor_style();
    }
}

void TerminalHostBase::begin_synchronized_output()
{
    PERF_MEASURE();
    if (synchronized_output_mode_)
        return;

    synchronized_output_mode_ = true;
    begin_cursor_publish_batch();
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace,
            LogCategory::Input,
            "cursor trace: begin_synchronized_output logical=(%d,%d) visible=%d",
            vt_.col,
            vt_.row,
            vt_.cursor_visible ? 1 : 0);
    }
}

void TerminalHostBase::end_synchronized_output()
{
    PERF_MEASURE();
    if (!synchronized_output_mode_)
        return;

    synchronized_output_mode_ = false;
    output_cursor_batch_ended_synchronized_output_ = true;
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace,
            LogCategory::Input,
            "cursor trace: end_synchronized_output logical=(%d,%d) visible=%d",
            vt_.col,
            vt_.row,
            vt_.cursor_visible ? 1 : 0);
    }
    end_cursor_publish_batch();
}

bool TerminalHostBase::apply_deferred_cursor_visibility_if_due(std::chrono::steady_clock::time_point now)
{
    PERF_MEASURE();
    if (!deferred_cursor_visibility_deadline_ || now < *deferred_cursor_visibility_deadline_)
        return false;

    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace,
            LogCategory::Input,
            "cursor trace: deferred_visibility_due logical=(%d,%d) vt_visible=%d",
            vt_.col,
            vt_.row,
            vt_.cursor_visible ? 1 : 0);
    }
    deferred_cursor_visibility_deadline_.reset();
    update_cursor_style();
    return true;
}

void TerminalHostBase::refresh_cursor_repaint_freeze_state()
{
    PERF_MEASURE();
    cursor_repaint_display_frozen_ = cursor_repaint_save_active_
        && (vt_.col != cursor_repaint_frozen_col_ || vt_.row != cursor_repaint_frozen_row_);
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace,
            LogCategory::Input,
            "cursor trace: refresh_freeze scope=%d frozen=%d logical=(%d,%d) freeze_anchor=(%d,%d)",
            cursor_repaint_save_active_ ? 1 : 0,
            cursor_repaint_display_frozen_ ? 1 : 0,
            vt_.col,
            vt_.row,
            cursor_repaint_frozen_col_,
            cursor_repaint_frozen_row_);
    }
}

std::pair<int, int> TerminalHostBase::presented_cursor_position() const
{
    PERF_MEASURE();
    const int max_col = std::max(0, grid_cols() - 1);
    const int max_row = std::max(0, grid_rows() - 1);
    if (cursor_repaint_display_frozen_)
    {
        return {
            std::clamp(cursor_repaint_frozen_col_, 0, max_col),
            std::clamp(cursor_repaint_frozen_row_, 0, max_row),
        };
    }
    return {
        std::clamp(vt_.col, 0, max_col),
        std::clamp(vt_.row, 0, max_row),
    };
}

void TerminalHostBase::begin_output_cursor_batch()
{
    PERF_MEASURE();
    if (output_cursor_batch_active_)
        return;
    output_cursor_batch_active_ = true;
    output_cursor_batch_activity_ = false;
    output_cursor_batch_saw_repaint_scope_ = false;
    output_cursor_batch_ended_synchronized_output_ = false;
    output_cursor_batch_start_col_ = vt_.col;
    output_cursor_batch_start_row_ = vt_.row;
    output_cursor_batch_start_visible_ = vt_.cursor_visible;
    begin_cursor_publish_batch();
}

void TerminalHostBase::end_output_cursor_batch()
{
    PERF_MEASURE();
    if (!output_cursor_batch_active_)
        return;
    output_cursor_batch_active_ = false;
    const bool alt_screen_cursor_moved = alt_screen_.in_alt_screen()
        && output_cursor_batch_start_visible_
        && vt_.cursor_visible
        && (vt_.col != output_cursor_batch_start_col_ || vt_.row != output_cursor_batch_start_row_);
    if (cursor_repaint_display_frozen_)
        set_cursor_display_override(presented_cursor_position());
    set_cursor_position(vt_.col, vt_.row, CursorBlinkUpdate::Preserve);
    if (!cursor_repaint_display_frozen_)
        set_cursor_display_override(std::nullopt);
    const bool ended_synchronized_output = output_cursor_batch_ended_synchronized_output_;
    if (alt_screen_cursor_moved
        || (ended_synchronized_output && !alt_screen_.in_alt_screen() && vt_.cursor_visible))
    {
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
        {
            log_printf(LogLevel::Trace,
                LogCategory::Input,
                "cursor trace: output_batch scheduling settle suppression alt_screen_move=%d ended_sync=%d repaint_scope=%d start=(%d,%d) end=(%d,%d)",
                alt_screen_cursor_moved ? 1 : 0,
                ended_synchronized_output ? 1 : 0,
                output_cursor_batch_saw_repaint_scope_ ? 1 : 0,
                output_cursor_batch_start_col_,
                output_cursor_batch_start_row_,
                vt_.col,
                vt_.row);
        }
        const auto settle_delay = alt_screen_cursor_moved
            ? kAltScreenCursorMoveSettleDelay
            : kOutputCursorSettleDelay;
        suppress_cursor_until(std::chrono::steady_clock::now() + settle_delay);
    }
    end_cursor_publish_batch();
    output_cursor_batch_activity_ = false;
    output_cursor_batch_saw_repaint_scope_ = false;
    output_cursor_batch_ended_synchronized_output_ = false;
}

// ---------------------------------------------------------------------------
// Alternate screen
// ---------------------------------------------------------------------------

void TerminalHostBase::enter_alt_screen()
{
    PERF_MEASURE();
    cursor_repaint_save_active_ = false;
    cursor_repaint_display_frozen_ = false;
    cursor_repaint_chunk_activity_ = false;
    cursor_repaint_visibility_deferred_ = false;
    deferred_cursor_visibility_deadline_.reset();
    set_cursor_display_override(std::nullopt);
    if (synchronized_output_mode_)
        end_synchronized_output();
    alt_screen_.enter(vt_.col, vt_.row, vt_.scroll_top, vt_.scroll_bottom, vt_.pending_wrap);
    vt_.col = 0;
    vt_.row = 0;
}

void TerminalHostBase::leave_alt_screen()
{
    PERF_MEASURE();
    cursor_repaint_save_active_ = false;
    cursor_repaint_display_frozen_ = false;
    cursor_repaint_chunk_activity_ = false;
    cursor_repaint_visibility_deferred_ = false;
    deferred_cursor_visibility_deadline_.reset();
    set_cursor_display_override(std::nullopt);
    if (synchronized_output_mode_)
        end_synchronized_output();
    alt_screen_.leave(vt_.col, vt_.row, vt_.pending_wrap, vt_.scroll_top, vt_.scroll_bottom);
}

// ---------------------------------------------------------------------------
// Grid helpers
// ---------------------------------------------------------------------------

uint16_t TerminalHostBase::attr_id()
{
    PERF_MEASURE();
    return attr_cache_.get_or_insert(
        current_attr_, highlights(), [this]() { compact_attr_ids(); });
}

void TerminalHostBase::compact_attr_ids()
{
    PERF_MEASURE();

    // 1. Collect live attr IDs from all sources.
    std::unordered_map<uint16_t, HlAttr> active_attrs;
    active_attrs.reserve(attr_cache_.size());

    for (int row = 0; row < grid_rows(); ++row)
    {
        for (int col = 0; col < grid_cols(); ++col)
        {
            const uint16_t id = grid().get_cell(col, row).hl_attr_id;
            if (id == 0)
                continue;
            active_attrs.try_emplace(id, highlights().get(id));
        }
    }

    alt_screen_.for_each_saved_cell(
        [&active_attrs, this](const Cell& cell) {
            if (cell.hl_attr_id == 0)
                return;
            active_attrs.try_emplace(cell.hl_attr_id, highlights().get(cell.hl_attr_id));
        });

    collect_extra_attr_ids(active_attrs);

    // 2. Compact via the shared AttributeCache.
    const auto remap = attr_cache_.compact(active_attrs, highlights());

    // 3. Apply the remap to all ID-bearing storage.
    auto remap_fn = [&remap](uint16_t id) -> uint16_t {
        if (id == 0)
            return id;
        const auto it = remap.find(id);
        return it != remap.end() ? it->second : static_cast<uint16_t>(0);
    };

    grid().remap_highlight_ids(remap_fn);
    alt_screen_.remap_saved_highlight_ids(remap_fn);
    remap_extra_highlight_ids(remap_fn);
}

void TerminalHostBase::clear_cell(int col, int row)
{
    grid().set_cell(col, row, " ", attr_id(), false);
}

void TerminalHostBase::newline(bool carriage_return)
{
    PERF_MEASURE();
    if (carriage_return)
        vt_.col = 0;
    vt_.pending_wrap = false;

    if (vt_.row == vt_.scroll_bottom)
    {
        // Notify subclasses that a line is about to scroll off the top of the
        // visible area so they can capture it (e.g. into a scrollback buffer).
        if (!alt_screen_.in_alt_screen() && vt_.scroll_top == 0
            && vt_.scroll_bottom == grid_rows() - 1)
        {
            on_line_scrolled_off(vt_.scroll_top);
        }
        grid().scroll(vt_.scroll_top, vt_.scroll_bottom + 1, 0, grid_cols(), 1);
    }
    else if (vt_.row < grid_rows() - 1)
    {
        ++vt_.row;
    }
}

void TerminalHostBase::write_cluster(const std::string& cluster)
{
    PERF_MEASURE();
    int width = cluster_cell_width(cluster);

    if (vt_.pending_wrap && vt_.auto_wrap_mode)
    {
        vt_.pending_wrap = false;
        newline(true);
    }

    vt_.col = std::clamp(vt_.col, 0, std::max(0, grid_cols() - 1));

    // Wide character at last available column: wrap first if auto-wrap enabled.
    if (width == 2 && vt_.col >= grid_cols() - 1)
    {
        if (vt_.auto_wrap_mode)
        {
            grid().set_cell(vt_.col, vt_.row, " ", attr_id(), false);
            newline(true);
        }
        else
        {
            width = 1;
        }
    }

    grid().set_cell(vt_.col, vt_.row, cluster, attr_id(), width == 2);
    const int new_col = vt_.col + width;

    if (new_col >= grid_cols())
    {
        vt_.pending_wrap = true;
        vt_.col = grid_cols() - 1;
    }
    else
    {
        vt_.col = new_col;
    }
}

void TerminalHostBase::erase_line(int mode)
{
    PERF_MEASURE();
    int start = 0;
    int end = grid_cols() - 1;
    if (mode == 0)
        start = vt_.col;
    else if (mode == 1)
        end = vt_.col;
    for (int col = start; col <= end; ++col)
        clear_cell(col, vt_.row);
}

void TerminalHostBase::erase_display(int mode)
{
    PERF_MEASURE();
    DRAXUL_LOG_DEBUG(LogCategory::App,
        "terminal: erase_display(%d) grid=%dx%d cursor=(%d,%d)",
        mode, grid_cols(), grid_rows(), vt_.col, vt_.row);
    if (mode == 2)
    {
        // Push non-blank visible rows to scrollback before clearing, so
        // 'clear' command output is preserved in history (matches iTerm2,
        // Windows Terminal). Only on the main screen.
        if (!alt_screen_.in_alt_screen())
        {
            for (int r = 0; r < grid_rows(); ++r)
            {
                bool blank_row = true;
                for (int c = 0; c < grid_cols(); ++c)
                {
                    const auto& cell = grid().get_cell(c, r);
                    if (!cell.text.empty() && cell.text.view() != " ")
                    {
                        blank_row = false;
                        break;
                    }
                }
                if (!blank_row)
                    on_line_scrolled_off(r);
            }
        }
        grid().clear();
        return;
    }

    if (mode == 0)
    {
        erase_line(0);
        for (int row = vt_.row + 1; row < grid_rows(); ++row)
            for (int col = 0; col < grid_cols(); ++col)
                clear_cell(col, row);
    }
    else if (mode == 1)
    {
        erase_line(1);
        for (int row = 0; row < vt_.row; ++row)
            for (int col = 0; col < grid_cols(); ++col)
                clear_cell(col, row);
    }
}

// ---------------------------------------------------------------------------
// OSC 7 — working directory change
// ---------------------------------------------------------------------------

void TerminalHostBase::on_osc_cwd(const std::string& path)
{
    PERF_MEASURE();
    // Cache the full path for the per-pane status bar (WI 78). The window
    // title (set below) only shows the basename for brevity.
    current_cwd_ = path;

    // Show the last path component (directory name) as the window title,
    // matching the convention used by most terminal emulators.
    std::string_view sv = path;

    // Strip trailing slash(es) so that "/tmp/" yields "tmp", not "".
    while (sv.size() > 1 && sv.back() == '/')
        sv.remove_suffix(1);

    const auto last_slash = sv.rfind('/');
    const std::string_view basename = (last_slash != std::string_view::npos) ? sv.substr(last_slash + 1) : sv;

    callbacks().set_window_title(basename.empty() ? "/" : std::string(basename));
}

// ---------------------------------------------------------------------------
// Virtual hooks for highlight compaction (default no-ops)
// ---------------------------------------------------------------------------

void TerminalHostBase::collect_extra_attr_ids(std::unordered_map<uint16_t, HlAttr>& /*active_attrs*/)
{
    // Intentionally empty — LocalTerminalHost overrides to scan scrollback.
}

void TerminalHostBase::remap_extra_highlight_ids(const std::function<uint16_t(uint16_t)>& /*remap_fn*/)
{
    // Intentionally empty — LocalTerminalHost overrides to remap scrollback.
}

} // namespace draxul
