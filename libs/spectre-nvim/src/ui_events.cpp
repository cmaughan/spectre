#include <spectre/nvim.h>
#include <cstdio>

namespace spectre {

static uint32_t utf8_decode_first(const std::string& str) {
    if (str.empty()) return ' ';
    const uint8_t* s = reinterpret_cast<const uint8_t*>(str.data());
    uint32_t cp;
    if (s[0] < 0x80) {
        cp = s[0];
    } else if ((s[0] & 0xE0) == 0xC0) {
        cp = (s[0] & 0x1F) << 6;
        if (str.size() >= 2) cp |= (s[1] & 0x3F);
    } else if ((s[0] & 0xF0) == 0xE0) {
        cp = (s[0] & 0x0F) << 12;
        if (str.size() >= 2) cp |= (s[1] & 0x3F) << 6;
        if (str.size() >= 3) cp |= (s[2] & 0x3F);
    } else if ((s[0] & 0xF8) == 0xF0) {
        cp = (s[0] & 0x07) << 18;
        if (str.size() >= 2) cp |= (s[1] & 0x3F) << 12;
        if (str.size() >= 3) cp |= (s[2] & 0x3F) << 6;
        if (str.size() >= 4) cp |= (s[3] & 0x3F);
    } else {
        cp = '?';
    }
    return cp;
}

static bool is_double_width(uint32_t cp) {
    return (cp >= 0x1100 && cp <= 0x115F) ||
           cp == 0x2329 || cp == 0x232A ||
           (cp >= 0x2E80 && cp <= 0x303E) ||
           (cp >= 0x3040 && cp <= 0xA4CF && cp != 0x303F) ||
           (cp >= 0xAC00 && cp <= 0xD7A3) ||
           (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFE10 && cp <= 0xFE19) ||
           (cp >= 0xFE30 && cp <= 0xFE6F) ||
           (cp >= 0xFF01 && cp <= 0xFF60) ||
           (cp >= 0xFFE0 && cp <= 0xFFE6) ||
           (cp >= 0x20000 && cp <= 0x2FFFD) ||
           (cp >= 0x30000 && cp <= 0x3FFFD);
}

void UiEventHandler::process_redraw(const std::vector<MpackValue>& params) {
    for (size_t ei = 0; ei < params.size(); ei++) {
        auto& event = params[ei];
        if (event.type != MpackValue::Array || event.array_val.empty()) continue;

        const std::string& name = event.array_val[0].as_str();

        if (name == "flush") {
            if (on_flush) on_flush();
            continue;
        }

        for (size_t i = 1; i < event.array_val.size(); i++) {
            const auto& args = event.array_val[i];

            if (name == "grid_line") handle_grid_line(args);
            else if (name == "grid_cursor_goto") handle_grid_cursor_goto(args);
            else if (name == "grid_scroll") handle_grid_scroll(args);
            else if (name == "grid_clear") handle_grid_clear(args);
            else if (name == "grid_resize") handle_grid_resize(args);
            else if (name == "hl_attr_define") handle_hl_attr_define(args);
            else if (name == "default_colors_set") handle_default_colors_set(args);
            else if (name == "mode_info_set") handle_mode_info_set(args);
            else if (name == "mode_change") handle_mode_change(args);
            else if (name == "option_set") handle_option_set(args);
        }
    }
}

void UiEventHandler::handle_grid_line(const MpackValue& args) {
    if (!grid_ || args.type != MpackValue::Array || args.array_val.size() < 4) return;

    int row = (int)args.array_val[1].as_int();
    int col_start = (int)args.array_val[2].as_int();

    int col = col_start;
    uint16_t current_hl = 0;

    const auto& cells = args.array_val[3].as_array();
    for (auto& cell : cells) {
        if (cell.type != MpackValue::Array || cell.array_val.empty()) continue;

        const std::string& text = cell.array_val[0].as_str();

        if (cell.array_val.size() >= 2 && cell.array_val[1].type != MpackValue::Nil) {
            current_hl = (uint16_t)cell.array_val[1].as_int();
        }

        int repeat = 1;
        if (cell.array_val.size() >= 3) {
            repeat = (int)cell.array_val[2].as_int();
        }

        uint32_t codepoint = utf8_decode_first(text);
        bool dw = !text.empty() && text.size() > 0 && is_double_width(codepoint);

        if (text.empty()) {
            col++;
            continue;
        }

        for (int r = 0; r < repeat; r++) {
            grid_->set_cell(col, row, codepoint, current_hl, dw);
            col++;
            if (dw) col++;
        }
    }
}

void UiEventHandler::handle_grid_cursor_goto(const MpackValue& args) {
    if (args.type != MpackValue::Array || args.array_val.size() < 3) return;
    cursor_row_ = (int)args.array_val[1].as_int();
    cursor_col_ = (int)args.array_val[2].as_int();
    if (on_cursor_goto) on_cursor_goto(cursor_col_, cursor_row_);
}

void UiEventHandler::handle_grid_scroll(const MpackValue& args) {
    if (!grid_ || args.type != MpackValue::Array || args.array_val.size() < 7) return;
    int top = (int)args.array_val[1].as_int();
    int bot = (int)args.array_val[2].as_int();
    int left = (int)args.array_val[3].as_int();
    int right = (int)args.array_val[4].as_int();
    int rows = (int)args.array_val[5].as_int();

    grid_->scroll(top, bot, left, right, rows);
}

void UiEventHandler::handle_grid_clear(const MpackValue& args) {
    if (!grid_) return;
    grid_->clear();
}

void UiEventHandler::handle_grid_resize(const MpackValue& args) {
    if (args.type != MpackValue::Array || args.array_val.size() < 3) return;
    int cols = (int)args.array_val[1].as_int();
    int rows = (int)args.array_val[2].as_int();
    if (grid_) grid_->resize(cols, rows);
    if (on_grid_resize) on_grid_resize(cols, rows);
}

void UiEventHandler::handle_hl_attr_define(const MpackValue& args) {
    if (!highlights_ || args.type != MpackValue::Array || args.array_val.size() < 2) return;
    uint16_t id = (uint16_t)args.array_val[0].as_int();
    const auto& attrs = args.array_val[1];

    HlAttr hl = {};
    if (attrs.type == MpackValue::Map) {
        for (auto& [key, val] : attrs.map_val) {
            const std::string& k = key.as_str();
            if (k == "foreground") {
                hl.fg = Color::from_rgb((uint32_t)val.as_int());
            } else if (k == "background") {
                hl.bg = Color::from_rgb((uint32_t)val.as_int());
            } else if (k == "special") {
                hl.sp = Color::from_rgb((uint32_t)val.as_int());
            } else if (k == "bold") {
                hl.bold = val.as_bool();
            } else if (k == "italic") {
                hl.italic = val.as_bool();
            } else if (k == "underline") {
                hl.underline = val.as_bool();
            } else if (k == "undercurl") {
                hl.undercurl = val.as_bool();
            } else if (k == "strikethrough") {
                hl.strikethrough = val.as_bool();
            } else if (k == "reverse") {
                hl.reverse = val.as_bool();
            }
        }
    }

    highlights_->set(id, hl);
}

void UiEventHandler::handle_default_colors_set(const MpackValue& args) {
    if (!highlights_ || args.type != MpackValue::Array || args.array_val.size() < 3) return;
    uint32_t fg = (uint32_t)args.array_val[0].as_int();
    uint32_t bg = (uint32_t)args.array_val[1].as_int();
    uint32_t sp = (uint32_t)args.array_val[2].as_int();

    highlights_->set_default_fg(Color::from_rgb(fg));
    highlights_->set_default_bg(Color::from_rgb(bg));
    highlights_->set_default_sp(Color::from_rgb(sp));
}

void UiEventHandler::handle_mode_info_set(const MpackValue& args) {
    if (args.type != MpackValue::Array || args.array_val.size() < 2) return;
    const auto& modes = args.array_val[1].as_array();
    modes_.clear();
    for (auto& m : modes) {
        ModeInfo info;
        if (m.type == MpackValue::Map) {
            for (auto& [key, val] : m.map_val) {
                const std::string& k = key.as_str();
                if (k == "name") info.name = val.as_str();
                else if (k == "cursor_shape") {
                    const std::string& shape = val.as_str();
                    if (shape == "block") info.cursor_shape = CursorShape::Block;
                    else if (shape == "horizontal") info.cursor_shape = CursorShape::Horizontal;
                    else if (shape == "vertical") info.cursor_shape = CursorShape::Vertical;
                }
                else if (k == "cell_percentage") info.cell_percentage = (int)val.as_int();
                else if (k == "attr_id") info.attr_id = (int)val.as_int();
            }
        }
        modes_.push_back(std::move(info));
    }
}

void UiEventHandler::handle_mode_change(const MpackValue& args) {
    if (args.type != MpackValue::Array || args.array_val.size() < 2) return;
    current_mode_ = (int)args.array_val[1].as_int();
}

void UiEventHandler::handle_option_set(const MpackValue& args) {
    if (args.type != MpackValue::Array || args.array_val.size() < 2) return;
    const std::string& name = args.array_val[0].as_str();
    if (on_option_set) on_option_set(name, args.array_val[1]);
}

} // namespace spectre
