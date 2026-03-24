#pragma once

#include <optional>
#include <utility>

namespace draxul
{

class StartupResizeState
{
public:
    void defer(int cols, int rows)
    {
        pending_ = true;
        pending_cols_ = cols;
        pending_rows_ = rows;
    }

    bool pending() const
    {
        return pending_;
    }

    std::optional<std::pair<int, int>> consume_if_needed(int current_cols, int current_rows)
    {
        if (!pending_)
            return std::nullopt;

        pending_ = false;
        if (pending_cols_ == current_cols && pending_rows_ == current_rows)
            return std::nullopt;

        return std::pair{ pending_cols_, pending_rows_ };
    }

private:
    bool pending_ = false;
    int pending_cols_ = 0;
    int pending_rows_ = 0;
};

} // namespace draxul
