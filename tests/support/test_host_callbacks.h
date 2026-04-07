#pragma once

#include <draxul/host.h>

#include <string>

namespace draxul::tests
{

class TestHostCallbacks final : public IHostCallbacks
{
public:
    void request_frame() override
    {
        ++request_frame_calls;
    }

    void request_quit() override
    {
        ++request_quit_calls;
    }

    void wake_window() override
    {
        ++wake_window_calls;
    }

    void set_window_title(const std::string& title) override
    {
        last_window_title = title;
    }

    void set_text_input_area(int x, int y, int w, int h) override
    {
        last_text_input_area = { x, y, w, h };
    }

    int request_frame_calls = 0;
    int request_quit_calls = 0;
    int wake_window_calls = 0;
    std::string last_window_title;
    struct Rect
    {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    } last_text_input_area;
};

} // namespace draxul::tests
