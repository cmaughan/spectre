#pragma once

#include <optional>
#include <string>

namespace draxul
{

struct PendingResizeRequest
{
    int cols = 0;
    int rows = 0;
    std::string reason;
};

class UiRequestWorkerState
{
public:
    void start()
    {
        running_ = true;
        pending_request_.reset();
    }

    void stop()
    {
        running_ = false;
        pending_request_.reset();
    }

    bool request_resize(int cols, int rows, std::string reason)
    {
        if (!running_)
            return false;

        pending_request_ = PendingResizeRequest{ cols, rows, std::move(reason) };
        return true;
    }

    bool running() const
    {
        return running_;
    }

    bool has_pending_request() const
    {
        return pending_request_.has_value();
    }

    std::optional<PendingResizeRequest> take_pending_request()
    {
        if (!running_ || !pending_request_)
            return std::nullopt;

        auto request = std::move(pending_request_);
        pending_request_.reset();
        return request;
    }

private:
    bool running_ = false;
    std::optional<PendingResizeRequest> pending_request_;
};

} // namespace draxul
