#include <draxul/session_attach.h>

#include <draxul/config_document.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>

#include <chrono>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <future>
#include <sstream>
#include <string_view>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace draxul
{

namespace
{

uint64_t fnv1a_hash(std::string_view text)
{
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char ch : text)
    {
        hash ^= static_cast<uint64_t>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string endpoint_suffix(std::string_view session_id)
{
    std::ostringstream out;
    out << std::hex << fnv1a_hash(
        ConfigDocument::default_path().parent_path().string() + "|" + std::string(session_id));
    return out.str();
}

#ifdef _WIN32

std::string mutex_name(std::string_view session_id)
{
    return "Local\\DraxulSessionAttach-" + endpoint_suffix(session_id);
}

std::string pipe_name(std::string_view session_id)
{
    return "\\\\.\\pipe\\draxul-session-attach-" + endpoint_suffix(session_id);
}

std::string win32_error_message(DWORD error)
{
    LPSTR buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD size = FormatMessageA(flags, nullptr, error, 0, reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    std::string message = size > 0 && buffer ? std::string(buffer, size) : ("Win32 error " + std::to_string(error));
    if (buffer)
        LocalFree(buffer);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
        message.pop_back();
    return message;
}

#else

std::string socket_path(std::string_view session_id)
{
    return (std::filesystem::temp_directory_path()
        / ("draxul-session-attach-" + endpoint_suffix(session_id) + ".sock"))
        .string();
}

std::string errno_message(int error)
{
    return std::strerror(error);
}

#endif

const char* command_text(SessionAttachServer::Command command)
{
    switch (command)
    {
    case SessionAttachServer::Command::Activate:
        return "activate";
    case SessionAttachServer::Command::Detach:
        return "detach";
    case SessionAttachServer::Command::Shutdown:
        return "shutdown";
    case SessionAttachServer::Command::QueryLiveSession:
        return "query-live-session";
    }
    return "activate";
}

std::string serialize_live_session_info(const SessionAttachServer::LiveSessionInfo& info)
{
    std::ostringstream out;
    out << "workspace_count=" << info.workspace_count << '\n';
    out << "pane_count=" << info.pane_count << '\n';
    out << "detached=" << (info.detached ? 1 : 0) << '\n';
    out << "owner_pid=" << info.owner_pid << '\n';
    out << "last_attached_unix_s=" << info.last_attached_unix_s << '\n';
    out << "last_detached_unix_s=" << info.last_detached_unix_s << '\n';
    return out.str();
}

template <typename T>
bool parse_integral(std::string_view text, T* value)
{
    T parsed{};
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc() || result.ptr != end)
        return false;
    *value = parsed;
    return true;
}

bool parse_live_session_info(std::string_view payload,
    SessionAttachServer::LiveSessionInfo* info, std::string* error)
{
    SessionAttachServer::LiveSessionInfo parsed;
    while (!payload.empty())
    {
        const size_t newline = payload.find('\n');
        const std::string_view line = newline == std::string_view::npos
            ? payload
            : payload.substr(0, newline);
        payload = newline == std::string_view::npos ? std::string_view{} : payload.substr(newline + 1);
        if (line.empty())
            continue;

        const size_t equals = line.find('=');
        if (equals == std::string_view::npos)
        {
            if (error)
                *error = "Malformed live-session response line.";
            return false;
        }

        const std::string_view key = line.substr(0, equals);
        const std::string_view value = line.substr(equals + 1);
        if (key == "workspace_count")
        {
            if (!parse_integral(value, &parsed.workspace_count))
                return false;
        }
        else if (key == "pane_count")
        {
            if (!parse_integral(value, &parsed.pane_count))
                return false;
        }
        else if (key == "detached")
        {
            if (value == "1" || value == "true")
                parsed.detached = true;
            else if (value == "0" || value == "false")
                parsed.detached = false;
            else
                return false;
        }
        else if (key == "owner_pid")
        {
            if (!parse_integral(value, &parsed.owner_pid))
                return false;
        }
        else if (key == "last_attached_unix_s")
        {
            if (!parse_integral(value, &parsed.last_attached_unix_s))
                return false;
        }
        else if (key == "last_detached_unix_s")
        {
            if (!parse_integral(value, &parsed.last_detached_unix_s))
                return false;
        }
    }

    if (info)
        *info = parsed;
    return true;
}

} // namespace

SessionAttachServer::~SessionAttachServer()
{
    stop();
}

bool SessionAttachServer::start(
    std::string_view session_id, CommandHandler on_command_requested, std::string* error)
{
    return start(session_id, std::move(on_command_requested), QueryHandler{}, error);
}

bool SessionAttachServer::start(std::string_view session_id, CommandHandler on_command_requested,
    QueryHandler on_query_requested, std::string* error)
{
    PERF_MEASURE();
    stop();

    session_id_ = session_id.empty() ? "default" : std::string(session_id);
    on_command_requested_ = std::move(on_command_requested);
    on_query_requested_ = std::move(on_query_requested);
    stop_requested_ = false;

#ifdef _WIN32
    HANDLE mutex = CreateMutexA(nullptr, FALSE, mutex_name(session_id_).c_str());
    if (!mutex)
    {
        if (error)
            *error = "Failed to create session-attach mutex: " + win32_error_message(GetLastError());
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(mutex);
        if (error)
            *error = "Another Draxul session-attach server is already running.";
        return false;
    }
    instance_mutex_ = mutex;

    std::promise<bool> ready_promise;
    auto ready_future = ready_promise.get_future();
    server_thread_ = std::thread([this, ready = std::move(ready_promise)]() mutable {
        const std::string name = pipe_name(session_id_);
        bool ready_signaled = false;
        while (!stop_requested_.load())
        {
            HANDLE pipe = CreateNamedPipeA(name.c_str(),
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                1,
                256,
                256,
                0,
                nullptr);
            if (pipe == INVALID_HANDLE_VALUE)
            {
                if (!ready_signaled)
                {
                    ready.set_value(false);
                    ready_signaled = true;
                }
                DRAXUL_LOG_ERROR(LogCategory::App,
                    "Session attach server failed to create pipe: %s",
                    win32_error_message(GetLastError()).c_str());
                break;
            }
            if (!ready_signaled)
            {
                ready.set_value(true);
                ready_signaled = true;
            }

            const BOOL connected = ConnectNamedPipe(pipe, nullptr)
                ? TRUE
                : (GetLastError() == ERROR_PIPE_CONNECTED);
            if (!connected)
            {
                CloseHandle(pipe);
                if (!stop_requested_.load())
                {
                    DRAXUL_LOG_WARN(LogCategory::App,
                        "Session attach server connect failed: %s",
                        win32_error_message(GetLastError()).c_str());
                }
                continue;
            }

            char buffer[64] = {};
            DWORD bytes_read = 0;
            const BOOL read_ok = ReadFile(pipe, buffer, sizeof(buffer), &bytes_read, nullptr);
            std::string response = "ok";
            if (!stop_requested_.load() && read_ok && bytes_read > 0)
            {
                const std::string_view command(buffer, bytes_read);
                if (command == "activate")
                {
                    if (on_command_requested_)
                        on_command_requested_(Command::Activate);
                }
                else if (command == "detach")
                {
                    if (on_command_requested_)
                        on_command_requested_(Command::Detach);
                }
                else if (command == "shutdown")
                {
                    if (on_command_requested_)
                        on_command_requested_(Command::Shutdown);
                    stop_requested_ = true;
                }
                else if (command == "query-live-session")
                {
                    response = serialize_live_session_info(
                        on_query_requested_ ? on_query_requested_() : LiveSessionInfo{});
                }
            }

            if (read_ok && bytes_read > 0)
            {
                DWORD bytes_written = 0;
                (void)WriteFile(pipe, response.data(), static_cast<DWORD>(response.size()), &bytes_written, nullptr);
            }

            FlushFileBuffers(pipe);
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }

        running_ = false;
    });
    if (!ready_future.get())
    {
        if (server_thread_.joinable())
            server_thread_.join();
        if (instance_mutex_)
        {
            CloseHandle(static_cast<HANDLE>(instance_mutex_));
            instance_mutex_ = nullptr;
        }
        if (error)
            *error = "Failed to create the session-attach pipe.";
        return false;
    }
    running_ = true;
#else
    socket_path_ = socket_path(session_id_);
    const int probe_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (probe_fd >= 0)
    {
        sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path_.c_str());
        if (::connect(probe_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        {
            if (errno == ENOENT || errno == ECONNREFUSED)
            {
                std::error_code ec;
                std::filesystem::remove(socket_path_, ec);
            }
        }
        ::close(probe_fd);
    }

    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
    {
        if (error)
            *error = "Failed to create session-attach socket: " + errno_message(errno);
        return false;
    }

    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path_.c_str());
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        if (error)
            *error = "Failed to bind session-attach socket: " + errno_message(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    if (::listen(listen_fd_, 4) != 0)
    {
        if (error)
            *error = "Failed to listen on session-attach socket: " + errno_message(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        std::error_code ec;
        std::filesystem::remove(socket_path_, ec);
        return false;
    }

    running_ = true;
    server_thread_ = std::thread([this]() {
        while (!stop_requested_.load())
        {
            const int client_fd = ::accept(listen_fd_, nullptr, nullptr);
            if (client_fd < 0)
            {
                if (!stop_requested_.load())
                {
                    DRAXUL_LOG_WARN(LogCategory::App,
                        "Session attach server accept failed: %s",
                        errno_message(errno).c_str());
                }
                continue;
            }

            char buffer[64] = {};
            const ssize_t bytes_read = ::read(client_fd, buffer, sizeof(buffer));
            std::string response = "ok";
            if (!stop_requested_.load() && bytes_read > 0)
            {
                const std::string_view command(buffer, static_cast<size_t>(bytes_read));
                if (command == "activate")
                {
                    if (on_command_requested_)
                        on_command_requested_(Command::Activate);
                }
                else if (command == "detach")
                {
                    if (on_command_requested_)
                        on_command_requested_(Command::Detach);
                }
                else if (command == "shutdown")
                {
                    if (on_command_requested_)
                        on_command_requested_(Command::Shutdown);
                    stop_requested_ = true;
                }
                else if (command == "query-live-session")
                {
                    response = serialize_live_session_info(
                        on_query_requested_ ? on_query_requested_() : LiveSessionInfo{});
                }
            }
            if (bytes_read > 0)
                (void)::write(client_fd, response.data(), response.size());
            ::close(client_fd);
        }

        running_ = false;
    });
#endif

    return true;
}

void SessionAttachServer::stop()
{
    PERF_MEASURE();
    stop_requested_ = true;

#ifdef _WIN32
    if (running())
    {
        const auto status = send_command(session_id_, Command::Shutdown);
        (void)status;
    }
#else
    if (listen_fd_ >= 0)
    {
        const int wake_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (wake_fd >= 0)
        {
            sockaddr_un addr = {};
            addr.sun_family = AF_UNIX;
            std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path_.c_str());
            (void)::connect(wake_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
            (void)::write(wake_fd, "x", 1);
            ::close(wake_fd);
        }
    }
#endif

    if (server_thread_.joinable())
        server_thread_.join();

#ifdef _WIN32
    if (instance_mutex_)
    {
        CloseHandle(static_cast<HANDLE>(instance_mutex_));
        instance_mutex_ = nullptr;
    }
#else
    if (listen_fd_ >= 0)
    {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (!socket_path_.empty())
    {
        std::error_code ec;
        std::filesystem::remove(socket_path_, ec);
        socket_path_.clear();
    }
#endif

    running_ = false;
}

SessionAttachServer::ProbeStatus SessionAttachServer::probe(
    std::string_view session_id, std::string* error)
{
    PERF_MEASURE();
#ifdef _WIN32
    const std::string name = pipe_name(session_id.empty() ? "default" : session_id);
    if (!WaitNamedPipeA(name.c_str(), 50))
    {
        const DWORD wait_error = GetLastError();
        if (wait_error == ERROR_FILE_NOT_FOUND)
            return ProbeStatus::NoServer;
        if (error)
            *error = "Failed waiting for session-attach pipe: " + win32_error_message(wait_error);
        return ProbeStatus::Error;
    }
    return ProbeStatus::Running;
#else
    const std::string path = socket_path(session_id.empty() ? "default" : session_id);
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        if (error)
            *error = "Failed creating session-attach client socket: " + errno_message(errno);
        return ProbeStatus::Error;
    }

    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        const int connect_error = errno;
        ::close(fd);
        if (connect_error == ENOENT || connect_error == ECONNREFUSED)
            return ProbeStatus::NoServer;
        if (error)
            *error = "Failed connecting to session-attach socket: " + errno_message(connect_error);
        return ProbeStatus::Error;
    }
    ::close(fd);
    return ProbeStatus::Running;
#endif
}

SessionAttachServer::AttachStatus SessionAttachServer::try_attach(
    std::string_view session_id, std::string* error)
{
    return send_command(session_id, Command::Activate, error);
}

SessionAttachServer::AttachStatus SessionAttachServer::send_command(
    std::string_view session_id, Command command, std::string* error)
{
    PERF_MEASURE();
#ifdef _WIN32
    const std::string name = pipe_name(session_id.empty() ? "default" : session_id);
    if (!WaitNamedPipeA(name.c_str(), 50))
    {
        const DWORD wait_error = GetLastError();
        if (wait_error == ERROR_FILE_NOT_FOUND)
            return AttachStatus::NoServer;
        if (error)
            *error = "Failed waiting for session-attach pipe: " + win32_error_message(wait_error);
        return AttachStatus::Error;
    }

    HANDLE pipe = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
    {
        const DWORD create_error = GetLastError();
        if (create_error == ERROR_FILE_NOT_FOUND)
            return AttachStatus::NoServer;
        if (error)
            *error = "Failed opening session-attach pipe: " + win32_error_message(create_error);
        return AttachStatus::Error;
    }
    DWORD bytes_written = 0;
    const char* command_name = command_text(command);
    const BOOL ok = WriteFile(
        pipe, command_name, static_cast<DWORD>(std::strlen(command_name)), &bytes_written, nullptr);
    CloseHandle(pipe);
    if (!ok)
    {
        if (error)
            *error = "Failed writing session-attach command: " + win32_error_message(GetLastError());
        return AttachStatus::Error;
    }
    return AttachStatus::Attached;
#else
    const std::string path = socket_path(session_id.empty() ? "default" : session_id);
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        if (error)
            *error = "Failed creating session-attach client socket: " + errno_message(errno);
        return AttachStatus::Error;
    }

    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        const int connect_error = errno;
        ::close(fd);
        if (connect_error == ENOENT)
            return AttachStatus::NoServer;
        if (error)
            *error = "Failed connecting to session-attach socket: " + errno_message(connect_error);
        return AttachStatus::Error;
    }

    const char* command_name = command_text(command);
    if (::write(fd, command_name, std::strlen(command_name)) < 0)
    {
        const int write_error = errno;
        ::close(fd);
        if (error)
            *error = "Failed writing session-attach command: " + errno_message(write_error);
        return AttachStatus::Error;
    }
    ::close(fd);
    return AttachStatus::Attached;
#endif
}

bool SessionAttachServer::query_live_session(
    std::string_view session_id, LiveSessionInfo* info, std::string* error)
{
    PERF_MEASURE();
    std::string response;
#ifdef _WIN32
    const std::string name = pipe_name(session_id.empty() ? "default" : session_id);
    if (!WaitNamedPipeA(name.c_str(), 50))
    {
        const DWORD wait_error = GetLastError();
        if (wait_error == ERROR_FILE_NOT_FOUND)
            return false;
        if (error)
            *error = "Failed waiting for session-attach pipe: " + win32_error_message(wait_error);
        return false;
    }

    HANDLE pipe = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
    {
        if (error)
            *error = "Failed opening session-attach pipe: " + win32_error_message(GetLastError());
        return false;
    }

    DWORD bytes_written = 0;
    const char* command_name = command_text(Command::QueryLiveSession);
    if (!WriteFile(pipe, command_name, static_cast<DWORD>(std::strlen(command_name)), &bytes_written, nullptr))
    {
        if (error)
            *error = "Failed writing live-session query: " + win32_error_message(GetLastError());
        CloseHandle(pipe);
        return false;
    }

    char buffer[256];
    DWORD bytes_read = 0;
    while (ReadFile(pipe, buffer, sizeof(buffer), &bytes_read, nullptr) && bytes_read > 0)
        response.append(buffer, buffer + bytes_read);
    const DWORD read_error = GetLastError();
    CloseHandle(pipe);
    if (response.empty() && read_error != ERROR_BROKEN_PIPE && read_error != ERROR_SUCCESS)
    {
        if (error)
            *error = "Failed reading live-session response: " + win32_error_message(read_error);
        return false;
    }
#else
    const std::string path = socket_path(session_id.empty() ? "default" : session_id);
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        if (error)
            *error = "Failed creating session-attach client socket: " + errno_message(errno);
        return false;
    }

    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        if (error)
            *error = "Failed connecting to session-attach socket: " + errno_message(errno);
        ::close(fd);
        return false;
    }

    const char* command_name = command_text(Command::QueryLiveSession);
    if (::write(fd, command_name, std::strlen(command_name)) < 0)
    {
        if (error)
            *error = "Failed writing live-session query: " + errno_message(errno);
        ::close(fd);
        return false;
    }

    char buffer[256];
    ssize_t bytes_read = 0;
    while ((bytes_read = ::read(fd, buffer, sizeof(buffer))) > 0)
        response.append(buffer, buffer + bytes_read);
    if (bytes_read < 0)
    {
        if (error)
            *error = "Failed reading live-session response: " + errno_message(errno);
        ::close(fd);
        return false;
    }
    ::close(fd);
#endif

    if (!parse_live_session_info(response, info, error))
    {
        if (error && error->empty())
            *error = "Failed to parse the live-session response.";
        return false;
    }
    return true;
}

} // namespace draxul
