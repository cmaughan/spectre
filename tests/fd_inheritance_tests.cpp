#include <catch2/catch_all.hpp>

// FD inheritance tests are POSIX-only (the bug and fix are macOS/Linux specific).
#ifndef _WIN32

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace
{

/// Helper: close all FDs above stderr except those in `keep`, mimicking the
/// fix applied to nvim_process.cpp and unix_pty_process.cpp.
void close_inherited_fds(const std::vector<int>& keep = {})
{
    const int max_fd = static_cast<int>(sysconf(_SC_OPEN_MAX));
    const int limit = (max_fd > 0) ? max_fd : 1024;
    for (int fd = STDERR_FILENO + 1; fd < limit; ++fd)
    {
        bool skip = false;
        for (int k : keep)
        {
            if (fd == k)
            {
                skip = true;
                break;
            }
        }
        if (!skip)
            close(fd);
    }
}

/// Count open FDs in [STDERR_FILENO+1, limit) via fcntl(F_GETFD).
int count_open_fds_above_stderr(const std::vector<int>& exclude = {})
{
    const int max_fd = static_cast<int>(sysconf(_SC_OPEN_MAX));
    const int limit = (max_fd > 0) ? max_fd : 1024;
    int count = 0;
    for (int fd = STDERR_FILENO + 1; fd < limit; ++fd)
    {
        bool skip = false;
        for (int e : exclude)
        {
            if (fd == e)
            {
                skip = true;
                break;
            }
        }
        if (skip)
            continue;
        if (fcntl(fd, F_GETFD) != -1)
            ++count;
    }
    return count;
}

} // namespace

TEST_CASE("child process has no inherited FDs above stderr after close loop", "[process][posix]")
{
    // Open several dummy FDs in the parent to simulate leaked descriptors
    // (log file, GPU FDs, etc.)
    std::vector<int> leaked_fds;
    for (int i = 0; i < 5; ++i)
    {
        int fd = open("/dev/null", O_RDONLY);
        REQUIRE(fd >= 0);
        leaked_fds.push_back(fd);
    }

    // Create a reporting pipe: child writes its open-FD count, parent reads it.
    std::array<int, 2> report_pipe{};
    REQUIRE(pipe(report_pipe.data()) == 0);

    pid_t pid = fork();
    REQUIRE(pid >= 0);

    if (pid == 0)
    {
        // Child: close the read end of the report pipe, then close all
        // inherited FDs except the write end (mirroring the production fix).
        close(report_pipe[0]);
        close_inherited_fds({ report_pipe[1] });

        // Count remaining open FDs above stderr (excluding the report pipe).
        int open_count = count_open_fds_above_stderr({ report_pipe[1] });

        // Write the count back to the parent.
        (void)!::write(report_pipe[1], &open_count, sizeof(open_count));
        close(report_pipe[1]);
        _exit(0);
    }

    // Parent: close write end, read the child's report.
    close(report_pipe[1]);

    int child_open_count = -1;
    ssize_t n = ::read(report_pipe[0], &child_open_count, sizeof(child_open_count));
    close(report_pipe[0]);

    // Clean up leaked FDs in the parent.
    for (int fd : leaked_fds)
        close(fd);

    // Reap child.
    int status = 0;
    waitpid(pid, &status, 0);

    REQUIRE(n == sizeof(child_open_count));
    // The child should have zero open FDs above stderr (aside from the report pipe).
    CHECK(child_open_count == 0);
}

TEST_CASE("close loop preserves specified keep-FDs", "[process][posix]")
{
    // Open a FD that we want to keep.
    int keep_fd = open("/dev/null", O_RDONLY);
    REQUIRE(keep_fd >= 0);

    // Open extra FDs that should be closed.
    std::vector<int> extra_fds;
    for (int i = 0; i < 3; ++i)
    {
        int fd = open("/dev/null", O_RDONLY);
        REQUIRE(fd >= 0);
        extra_fds.push_back(fd);
    }

    std::array<int, 2> report_pipe{};
    REQUIRE(pipe(report_pipe.data()) == 0);

    pid_t pid = fork();
    REQUIRE(pid >= 0);

    if (pid == 0)
    {
        close(report_pipe[0]);
        close_inherited_fds({ report_pipe[1], keep_fd });

        // Check that keep_fd is still open.
        int keep_ok = (fcntl(keep_fd, F_GETFD) != -1) ? 1 : 0;

        // Count unexpected open FDs (excluding report_pipe[1] and keep_fd).
        int unexpected = count_open_fds_above_stderr({ report_pipe[1], keep_fd });

        // Report: [keep_ok, unexpected_count]
        std::array<int, 2> report = { keep_ok, unexpected };
        (void)!::write(report_pipe[1], report.data(), sizeof(report));
        close(report_pipe[1]);
        _exit(0);
    }

    close(report_pipe[1]);

    std::array<int, 2> report{};
    ssize_t n = ::read(report_pipe[0], report.data(), sizeof(report));
    close(report_pipe[0]);

    // Clean up parent FDs.
    close(keep_fd);
    for (int fd : extra_fds)
        close(fd);

    int status = 0;
    waitpid(pid, &status, 0);

    REQUIRE(n == sizeof(report));
    CHECK(report[0] == 1); // keep_fd was preserved
    CHECK(report[1] == 0); // no unexpected FDs
}

#endif // !_WIN32
