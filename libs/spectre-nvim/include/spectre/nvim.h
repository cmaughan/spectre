#pragma once
#include <spectre/types.h>
#include <spectre/events.h>
#include <spectre/grid.h>
#include <mpack.h>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <thread>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include <cstdint>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/types.h>
#include <signal.h>
#endif

namespace spectre {

// --- NvimProcess ---

class NvimProcess {
public:
    bool spawn(const std::string& nvim_path = "nvim");
    void shutdown();

    bool write(const uint8_t* data, size_t len);
    int read(uint8_t* buffer, size_t max_len);

    bool is_running() const;

private:
#ifdef _WIN32
    HANDLE child_stdin_write_ = INVALID_HANDLE_VALUE;
    HANDLE child_stdout_read_ = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION proc_info_ = {};
    bool started_ = false;
#else
    int child_stdin_write_ = -1;
    int child_stdout_read_ = -1;
    pid_t child_pid_ = -1;
    bool started_ = false;
#endif
};

// --- RPC types ---

struct MpackValue {
    enum Type { Nil, Bool, Int, UInt, Float, String, Array, Map, Ext };
    Type type = Nil;

    bool bool_val = false;
    int64_t int_val = 0;
    uint64_t uint_val = 0;
    double float_val = 0;
    std::string str_val;
    std::vector<MpackValue> array_val;
    std::vector<std::pair<MpackValue, MpackValue>> map_val;

    int64_t as_int() const { return type == UInt ? (int64_t)uint_val : int_val; }
    const std::string& as_str() const { return str_val; }
    bool as_bool() const { return bool_val; }
    const std::vector<MpackValue>& as_array() const { return array_val; }
};

struct RpcNotification {
    std::string method;
    std::vector<MpackValue> params;
};

struct RpcResponse {
    uint32_t msgid;
    MpackValue error;
    MpackValue result;
};

// --- NvimRpc ---

class NvimRpc {
public:
    bool initialize(NvimProcess& process);
    void shutdown();

    MpackValue request(const std::string& method, const std::vector<MpackValue>& params);
    void notify(const std::string& method, const std::vector<MpackValue>& params);

    std::vector<RpcNotification> drain_notifications();

    static MpackValue make_int(int64_t v);
    static MpackValue make_uint(uint64_t v);
    static MpackValue make_str(const std::string& v);
    static MpackValue make_bool(bool v);
    static MpackValue make_array(std::vector<MpackValue> v);
    static MpackValue make_map(std::vector<std::pair<MpackValue, MpackValue>> v);
    static MpackValue make_nil();

private:
    void reader_thread_func();
    void write_value(mpack_writer_t* writer, const MpackValue& val);
    MpackValue read_value(mpack_reader_t* reader);

    NvimProcess* process_ = nullptr;
    std::thread reader_thread_;
    std::atomic<bool> running_{false};

    std::mutex notif_mutex_;
    std::queue<RpcNotification> notifications_;

    std::mutex response_mutex_;
    std::condition_variable response_cv_;
    std::unordered_map<uint32_t, RpcResponse> responses_;

    std::atomic<uint32_t> next_msgid_{1};

    std::vector<uint8_t> read_buf_;
    size_t read_pos_ = 0;
    size_t read_len_ = 0;
};

// --- UiEventHandler ---

struct ModeInfo {
    std::string name;
    CursorShape cursor_shape = CursorShape::Block;
    int cell_percentage = 0;
    int attr_id = 0;
};

class UiEventHandler {
public:
    void set_grid(Grid* grid) { grid_ = grid; }
    void set_highlights(HighlightTable* hl) { highlights_ = hl; }

    void process_redraw(const std::vector<MpackValue>& params);

    std::function<void()> on_flush;
    std::function<void(int, int)> on_grid_resize;
    std::function<void(int, int)> on_cursor_goto;
    std::function<void(const std::string&, const MpackValue&)> on_option_set;

    const std::vector<ModeInfo>& modes() const { return modes_; }
    int current_mode() const { return current_mode_; }
    int cursor_col() const { return cursor_col_; }
    int cursor_row() const { return cursor_row_; }

private:
    void handle_grid_line(const MpackValue& args);
    void handle_grid_cursor_goto(const MpackValue& args);
    void handle_grid_scroll(const MpackValue& args);
    void handle_grid_clear(const MpackValue& args);
    void handle_grid_resize(const MpackValue& args);
    void handle_hl_attr_define(const MpackValue& args);
    void handle_default_colors_set(const MpackValue& args);
    void handle_mode_info_set(const MpackValue& args);
    void handle_mode_change(const MpackValue& args);
    void handle_option_set(const MpackValue& args);

    Grid* grid_ = nullptr;
    HighlightTable* highlights_ = nullptr;

    std::vector<ModeInfo> modes_;
    int current_mode_ = 0;
    int cursor_col_ = 0, cursor_row_ = 0;
};

// --- NvimInput ---

class NvimInput {
public:
    void initialize(NvimRpc* rpc, int cell_w, int cell_h);
    void set_cell_size(int w, int h) { cell_w_ = w; cell_h_ = h; }

    void on_key(const KeyEvent& event);
    void on_text_input(const TextInputEvent& event);
    void on_mouse_button(const MouseButtonEvent& event);
    void on_mouse_move(const MouseMoveEvent& event);
    void on_mouse_wheel(const MouseWheelEvent& event);

private:
    void send_input(const std::string& keys);
    std::string translate_key(int keycode, uint16_t mod);

    NvimRpc* rpc_ = nullptr;
    int cell_w_ = 10, cell_h_ = 20;
    bool suppress_next_text_ = false;
    bool mouse_pressed_ = false;
};

} // namespace spectre
