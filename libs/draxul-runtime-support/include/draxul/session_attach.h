#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace draxul
{

class SessionAttachServer
{
public:
    enum class AttachStatus
    {
        Attached,
        NoServer,
        Error,
    };

    SessionAttachServer() = default;
    ~SessionAttachServer();

    SessionAttachServer(const SessionAttachServer&) = delete;
    SessionAttachServer& operator=(const SessionAttachServer&) = delete;

    bool start(std::function<void()> on_attach_requested, std::string* error = nullptr);
    void stop();

    bool running() const
    {
        return running_.load();
    }

    static AttachStatus try_attach(std::string* error = nullptr);

private:
    std::function<void()> on_attach_requested_;
    std::atomic<bool> stop_requested_ = false;
    std::atomic<bool> running_ = false;
    std::thread server_thread_;

#ifdef _WIN32
    void* instance_mutex_ = nullptr;
#else
    int listen_fd_ = -1;
    std::string socket_path_;
#endif
};

} // namespace draxul
