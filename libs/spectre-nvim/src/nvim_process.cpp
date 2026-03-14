#include <spectre/nvim.h>
#include <cstdio>

#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#endif

namespace spectre {

#ifdef _WIN32

bool NvimProcess::spawn(const std::string& nvim_path) {
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE stdin_read, stdin_write, stdout_read, stdout_write;

    if (!CreatePipe(&stdin_read, &stdin_write, &sa, 0)) {
        fprintf(stderr, "Failed to create stdin pipe\n");
        return false;
    }
    SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0)) {
        fprintf(stderr, "Failed to create stdout pipe\n");
        CloseHandle(stdin_read);
        CloseHandle(stdin_write);
        return false;
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

    std::string cmd = nvim_path + " --embed";

    if (!CreateProcessA(
        nullptr,
        cmd.data(),
        nullptr, nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &proc_info_))
    {
        fprintf(stderr, "Failed to spawn nvim: error %lu\n", GetLastError());
        CloseHandle(stdin_read);
        CloseHandle(stdin_write);
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        return false;
    }

    CloseHandle(stdin_read);
    CloseHandle(stdout_write);
    if (nul_handle != INVALID_HANDLE_VALUE) CloseHandle(nul_handle);

    child_stdin_write_ = stdin_write;
    child_stdout_read_ = stdout_read;
    started_ = true;

    fprintf(stderr, "[spectre] nvim spawned (PID %lu)\n", proc_info_.dwProcessId);
    return true;
}

void NvimProcess::shutdown() {
    if (!started_) return;

    if (child_stdin_write_ != INVALID_HANDLE_VALUE) {
        CloseHandle(child_stdin_write_);
        child_stdin_write_ = INVALID_HANDLE_VALUE;
    }
    if (child_stdout_read_ != INVALID_HANDLE_VALUE) {
        CloseHandle(child_stdout_read_);
        child_stdout_read_ = INVALID_HANDLE_VALUE;
    }

    if (proc_info_.hProcess) {
        WaitForSingleObject(proc_info_.hProcess, 2000);
        TerminateProcess(proc_info_.hProcess, 0);
        CloseHandle(proc_info_.hProcess);
        CloseHandle(proc_info_.hThread);
    }

    started_ = false;
}

bool NvimProcess::write(const uint8_t* data, size_t len) {
    DWORD written;
    return WriteFile(child_stdin_write_, data, (DWORD)len, &written, nullptr) && written == len;
}

int NvimProcess::read(uint8_t* buffer, size_t max_len) {
    DWORD bytes_read;
    if (!ReadFile(child_stdout_read_, buffer, (DWORD)max_len, &bytes_read, nullptr)) {
        return -1;
    }
    return (int)bytes_read;
}

bool NvimProcess::is_running() const {
    if (!started_) return false;
    DWORD exit_code;
    GetExitCodeProcess(proc_info_.hProcess, &exit_code);
    return exit_code == STILL_ACTIVE;
}

#else // POSIX (macOS, Linux)

bool NvimProcess::spawn(const std::string& nvim_path) {
    int stdin_pipe[2];
    int stdout_pipe[2];

    if (pipe(stdin_pipe) != 0) {
        fprintf(stderr, "Failed to create stdin pipe: %s\n", strerror(errno));
        return false;
    }
    if (pipe(stdout_pipe) != 0) {
        fprintf(stderr, "Failed to create stdout pipe: %s\n", strerror(errno));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Failed to fork: %s\n", strerror(errno));
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return false;
    }

    if (pid == 0) {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);

        execlp(nvim_path.c_str(), nvim_path.c_str(), "--embed", nullptr);
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    child_stdin_write_ = stdin_pipe[1];
    child_stdout_read_ = stdout_pipe[0];
    child_pid_ = pid;
    started_ = true;

    fprintf(stderr, "[spectre] nvim spawned (PID %d)\n", (int)child_pid_);
    return true;
}

void NvimProcess::shutdown() {
    if (!started_) return;

    if (child_stdin_write_ >= 0) {
        close(child_stdin_write_);
        child_stdin_write_ = -1;
    }
    if (child_stdout_read_ >= 0) {
        close(child_stdout_read_);
        child_stdout_read_ = -1;
    }

    if (child_pid_ > 0) {
        int status;
        pid_t result = waitpid(child_pid_, &status, WNOHANG);
        if (result == 0) {
            kill(child_pid_, SIGTERM);
            usleep(500000);
            result = waitpid(child_pid_, &status, WNOHANG);
            if (result == 0) {
                kill(child_pid_, SIGKILL);
                waitpid(child_pid_, &status, 0);
            }
        }
        child_pid_ = -1;
    }

    started_ = false;
}

bool NvimProcess::write(const uint8_t* data, size_t len) {
    size_t total_written = 0;
    while (total_written < len) {
        ssize_t n = ::write(child_stdin_write_, data + total_written, len - total_written);
        if (n <= 0) return false;
        total_written += (size_t)n;
    }
    return true;
}

int NvimProcess::read(uint8_t* buffer, size_t max_len) {
    ssize_t n = ::read(child_stdout_read_, buffer, max_len);
    if (n < 0) return -1;
    return (int)n;
}

bool NvimProcess::is_running() const {
    if (!started_ || child_pid_ <= 0) return false;
    int status;
    pid_t result = waitpid(child_pid_, &status, WNOHANG);
    return result == 0;
}

#endif

} // namespace spectre
