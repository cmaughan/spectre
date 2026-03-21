#pragma once

#include <cstdint>
#include <string>

namespace draxul
{

class IGridSink
{
public:
    virtual ~IGridSink() = default;

    virtual void resize(int cols, int rows) = 0;
    virtual void clear() = 0;
    virtual void set_cell(int col, int row, const std::string& text, uint16_t hl_id, bool double_width) = 0;
    virtual void scroll(int top, int bot, int left, int right, int rows, int cols = 0) = 0;

    // Optional dimension queries used for bounds-checking incoming events.
    // Return 0 if the sink does not track dimensions (bounds checks are skipped).
    virtual int sink_cols() const
    {
        return 0;
    }
    virtual int sink_rows() const
    {
        return 0;
    }
};

} // namespace draxul
