#include <draxul/log.h>
#include <draxul/nvim_rpc.h>
#include <draxul/perf_timing.h>

#include <algorithm>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#endif

namespace draxul
{

struct NvimProcess::Impl
{
#ifdef _WIN32
    HANDLE child_stdin_write_ = INVALID_HANDLE_VALUE;
    HANDLE child_stdout_read_ = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION proc_info_ = {};
#else
    int child_stdin_write_ = -1;
    int child_stdout_read_ = -1;
    pid_t child_pid_ = -1;
#endif
    bool started_ = false;
};

NvimProcess::NvimProcess()
    : impl_(std::make_unique<Impl>())
{
}
NvimProcess::~NvimProcess() = default;

#ifdef _WIN32

namespace
{

std::string quote_windows_arg(const std::string& value)
{
    PERF_MEASURE();
    if (value.find_first_of(" \t\"") == std::string::npos)
        return value;

    std::string quoted = "\"";
    size_t backslashes = 0;
    for (char ch : value)
    {
        if (ch == '\\')
        {
            ++backslashes;
            quoted.push_back(ch);
            continue;
        }
        if (ch == '"')
        {
            quoted.append(backslashes, '\\');
            quoted.push_back('\\');
        }
        backslashes = 0;
        quoted.push_back(ch);
    }
    quoted.append(backslashes, '\\');
    quoted.push_back('"');
    return quoted;
}

} // namespace

Result<void, Error> NvimProcess::spawn(const std::string& nvim_path, const std::vector<std::string>& extra_args, const std::string& working_dir)
{
    PERF_MEASURE();
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE stdin_read, stdin_write, stdout_read, stdout_write;

    if (!CreatePipe(&stdin_read, &stdin_write, &sa, 0))
    {
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to create stdin pipe");
        return Result<void, Error>::err(Error::io("Failed to create stdin pipe"));
    }
    SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0))
    {
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to create stdout pipe");
        CloseHandle(stdin_read);
        CloseHandle(stdin_write);
        return Result<void, Error>::err(Error::io("Failed to create stdout pipe"));
    }
    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);

    HANDLE nul_handle = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE,
        &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.hStdInput = stdin_read;
    si.hStdOutput = stdout_write;
    si.hStdError = nul_handle;
    si.dwFlags |= STARTF_USESTDHANDLES;

    std::ostringstream command;
    command << quote_windows_arg(nvim_path) << " --embed";
    for (const auto& arg : extra_args)
        command << ' ' << quote_windows_arg(arg);
    std::string cmd = command.str();

    const char* cwd = working_dir.empty() ? nullptr : working_dir.c_str();
    if (!CreateProcessA(
            nullptr,
            cmd.data(),
            nullptr, nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr, cwd,
            &si, &impl_->proc_info_))
    {
        const DWORD err = GetLastError();
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to spawn nvim: error %lu", err);
        CloseHandle(stdin_read);
        CloseHandle(stdin_write);
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        if (nul_handle != INVALID_HANDLE_VALUE)
            CloseHandle(nul_handle);
        return Result<void, Error>::err(Error::spawn(
            "CreateProcess failed (GetLastError=" + std::to_string(err) + ")"));
    }

    CloseHandle(stdin_read);
    CloseHandle(stdout_write);
    if (nul_handle != INVALID_HANDLE_VALUE)
        CloseHandle(nul_handle);

    impl_->child_stdin_write_ = stdin_write;
    impl_->child_stdout_read_ = stdout_read;
    impl_->started_ = true;

    DRAXUL_LOG_INFO(LogCategory::Nvim, "nvim spawned (PID %lu)", impl_->proc_info_.dwProcessId);
    return Result<void, Error>::ok();
}

void NvimProcess::shutdown()
{
    PERF_MEASURE();
    if (!impl_->started_)
        return;

    if (impl_->child_stdin_write_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(impl_->child_stdin_write_);
        impl_->child_stdin_write_ = INVALID_HANDLE_VALUE;
    }
    if (impl_->child_stdout_read_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(impl_->child_stdout_read_);
        impl_->child_stdout_read_ = INVALID_HANDLE_VALUE;
    }

    if (impl_->proc_info_.hProcess)
    {
        WaitForSingleObject(impl_->proc_info_.hProcess, 2000);
        TerminateProcess(impl_->proc_info_.hProcess, 0);
        CloseHandle(impl_->proc_info_.hProcess);
        CloseHandle(impl_->proc_info_.hThread);
    }

    impl_->started_ = false;
}

bool NvimProcess::write(const uint8_t* data, size_t len) const
{
    // WriteFile on an anonymous/named pipe is allowed to perform a partial
    // write: it may return TRUE with `written` < `to_write` when the pipe's
    // kernel buffer is nearly full (e.g. large msgpack-RPC payloads such as
    // nvim_buf_set_lines with pasted content). A single call is therefore
    // not sufficient — we must loop until the whole buffer has been
    // delivered, otherwise the tail of the message is silently dropped and
    // the RPC stream becomes corrupt for every subsequent request.
    //
    // Failure cases:
    //  * WriteFile returns FALSE -> hard error (broken pipe, child exited).
    //  * WriteFile returns TRUE with written == 0 -> should not happen on a
    //    blocking handle, but we treat it as a hard error to avoid spinning
    //    forever if the kernel ever violates that contract.
    size_t total_written = 0;
    while (total_written < len)
    {
        DWORD written = 0;
        DWORD to_write = static_cast<DWORD>(
            std::min<size_t>(len - total_written, MAXDWORD));
        if (!WriteFile(impl_->child_stdin_write_,
                data + total_written, to_write, &written, nullptr))
            return false;
        if (written == 0)
            return false;
        total_written += written;
    }
    return true;
}

int NvimProcess::read(uint8_t* buffer, size_t max_len) const
{
    DWORD bytes_read;
    if (!ReadFile(impl_->child_stdout_read_, buffer, (DWORD)max_len, &bytes_read, nullptr))
    {
        return -1;
    }
    return (bytes_read > static_cast<DWORD>(INT_MAX)) ? -1 : static_cast<int>(bytes_read);
}

bool NvimProcess::is_running() const
{
    if (!impl_->started_)
        return false;
    DWORD exit_code;
    GetExitCodeProcess(impl_->proc_info_.hProcess, &exit_code);
    return exit_code == STILL_ACTIVE;
}

#else // POSIX (macOS, Linux)

Result<void, Error> NvimProcess::spawn(const std::string& nvim_path, const std::vector<std::string>& extra_args, const std::string& working_dir)
{
    PERF_MEASURE();
    std::array<int, 2> stdin_pipe;
    std::array<int, 2> stdout_pipe;
    std::array<int, 2> exec_status_pipe;

    if (pipe(stdin_pipe.data()) != 0)
    {
        const int e = errno;
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to create stdin pipe: %s", strerror(e));
        return Result<void, Error>::err(Error::io(
            std::string("Failed to create stdin pipe: ") + strerror(e)));
    }
    if (pipe(stdout_pipe.data()) != 0)
    {
        const int e = errno;
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to create stdout pipe: %s", strerror(e));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        return Result<void, Error>::err(Error::io(
            std::string("Failed to create stdout pipe: ") + strerror(e)));
    }
    if (pipe(exec_status_pipe.data()) != 0)
    {
        const int e = errno;
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to create exec-status pipe: %s", strerror(e));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return Result<void, Error>::err(Error::io(
            std::string("Failed to create exec-status pipe: ") + strerror(e)));
    }

    if (fcntl(exec_status_pipe[1], F_SETFD, FD_CLOEXEC) != 0)
    {
        const int e = errno;
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to configure exec-status pipe: %s", strerror(e));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(exec_status_pipe[0]);
        close(exec_status_pipe[1]);
        return Result<void, Error>::err(Error::io(
            std::string("Failed to configure exec-status pipe: ") + strerror(e)));
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        const int e = errno;
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to fork: %s", strerror(e));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(exec_status_pipe[0]);
        close(exec_status_pipe[1]);
        return Result<void, Error>::err(Error::spawn(
            std::string("fork() failed: ") + strerror(e)));
    }

    if (pid == 0)
    {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(exec_status_pipe[0]);

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0)
        {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);

        // Close all inherited file descriptors above stderr to prevent FD
        // leakage into the child (log files, SDL/GPU FDs, other pipe ends).
        // exec_status_pipe[1] is kept open (it has FD_CLOEXEC so exec will
        // close it automatically, but we still need it for pre-exec error
        // reporting).
        {
            const int max_fd = static_cast<int>(sysconf(_SC_OPEN_MAX));
            const int limit = (max_fd > 0) ? max_fd : 1024;
            for (int fd = STDERR_FILENO + 1; fd < limit; ++fd)
            {
                if (fd == exec_status_pipe[1])
                    continue;
                close(fd); // harmless if fd is not open
            }
        }

        // Restore SIGPIPE to default so child processes terminate correctly
        // on broken pipes. The parent GUI may have set SIG_IGN.
        signal(SIGPIPE, SIG_DFL);

        if (!working_dir.empty() && chdir(working_dir.c_str()) != 0)
        {
            int chdir_errno = errno;
            (void)!::write(exec_status_pipe[1], &chdir_errno, sizeof(chdir_errno));
            close(exec_status_pipe[1]);
            _exit(127);
        }

        std::vector<std::string> argv_storage;
        argv_storage.reserve(extra_args.size() + 2);
        argv_storage.push_back(nvim_path);
        argv_storage.emplace_back("--embed");
        for (const auto& arg : extra_args)
            argv_storage.push_back(arg);

        std::vector<char*> argv;
        argv.reserve(argv_storage.size() + 1);
        for (auto& arg : argv_storage)
            argv.push_back(arg.data());
        argv.push_back(nullptr);

        execvp(nvim_path.c_str(), argv.data());
        int exec_errno = errno;
        (void)!::write(exec_status_pipe[1], &exec_errno, sizeof(exec_errno));
        close(exec_status_pipe[1]);
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(exec_status_pipe[1]);

    int exec_errno = 0;
    ssize_t status_bytes = ::read(exec_status_pipe[0], &exec_errno, sizeof(exec_errno));
    close(exec_status_pipe[0]);
    if (status_bytes > 0)
    {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        int status = 0;
        waitpid(pid, &status, 0);
        DRAXUL_LOG_ERROR(LogCategory::Nvim, "Failed to spawn nvim: %s", strerror(exec_errno));
        return Result<void, Error>::err(Error::spawn(
            std::string("execvp(") + nvim_path + ") failed: " + strerror(exec_errno)));
    }

    impl_->child_stdin_write_ = stdin_pipe[1];
    impl_->child_stdout_read_ = stdout_pipe[0];
    impl_->child_pid_ = pid;
    impl_->started_ = true;

    DRAXUL_LOG_INFO(LogCategory::Nvim, "nvim spawned (PID %d)", (int)impl_->child_pid_);
    return Result<void, Error>::ok();
}

void NvimProcess::shutdown()
{
    PERF_MEASURE();
    if (!impl_->started_)
        return;

    if (impl_->child_stdin_write_ >= 0)
    {
        close(impl_->child_stdin_write_);
        impl_->child_stdin_write_ = -1;
    }
    if (impl_->child_stdout_read_ >= 0)
    {
        close(impl_->child_stdout_read_);
        impl_->child_stdout_read_ = -1;
    }

    if (impl_->child_pid_ > 0)
    {
        int status;
        pid_t result = waitpid(impl_->child_pid_, &status, WNOHANG);
        if (result == 0)
        {
            // Send SIGTERM, then offload the timed wait + SIGKILL escalation
            // to a detached thread so the main thread does not block on a
            // stuck child. CLAUDE.md: "Keep shutdown paths non-blocking; a
            // stuck Neovim child must not hang the UI on exit."
            kill(impl_->child_pid_, SIGTERM);
            pid_t pid_copy = impl_->child_pid_;
            std::thread([pid_copy]() {
                using namespace std::chrono;
                const auto deadline = steady_clock::now() + milliseconds(500);
                int s = 0;
                while (steady_clock::now() < deadline)
                {
                    if (waitpid(pid_copy, &s, WNOHANG) != 0)
                        return;
                    std::this_thread::sleep_for(milliseconds(20));
                }
                kill(pid_copy, SIGKILL);
                waitpid(pid_copy, &s, 0);
            }).detach();
        }
        impl_->child_pid_ = -1;
    }

    impl_->started_ = false;
}

bool NvimProcess::write(const uint8_t* data, size_t len) const
{
    size_t total_written = 0;
    while (total_written < len)
    {
        ssize_t n = ::write(impl_->child_stdin_write_, data + total_written, len - total_written);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (n == 0)
            return false;
        total_written += (size_t)n;
    }
    return true;
}

int NvimProcess::read(uint8_t* buffer, size_t max_len) const
{
    ssize_t n;
    do
    {
        n = ::read(impl_->child_stdout_read_, buffer, max_len);
    } while (n < 0 && errno == EINTR);
    if (n < 0)
        return -1;
    return (int)n;
}

bool NvimProcess::is_running() const
{
    if (!impl_->started_ || impl_->child_pid_ <= 0)
        return false;
    int status;
    pid_t result = waitpid(impl_->child_pid_, &status, WNOHANG);
    return result == 0;
}

#endif

} // namespace draxul
