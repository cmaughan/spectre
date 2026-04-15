#ifndef _WIN32

#include <catch2/catch_all.hpp>

#include "../libs/draxul-host/src/unix_pty_process.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

using namespace draxul;

TEST_CASE("UnixPtyProcess sets xterm-256color shell environment", "[unix_pty_process]")
{
    const auto dump_path
        = std::filesystem::temp_directory_path() / "draxul-unix-pty-env-dump.txt";
    std::filesystem::remove(dump_path);

    UnixPtyProcess process;
    const std::string script = "printf '%s\\n%s\\n%s\\n' \"$TERM\" \"$COLORTERM\" \"$TERM_PROGRAM\" > '"
        + dump_path.string() + "'";
    REQUIRE(process.spawn("/bin/sh", { "-c", script }, "", [] {}, 80, 24));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (process.is_running() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    process.shutdown();

    std::ifstream in(dump_path);
    REQUIRE(in.good());

    std::string term;
    std::string colorterm;
    std::string term_program;
    std::getline(in, term);
    std::getline(in, colorterm);
    std::getline(in, term_program);

    REQUIRE(term == "xterm-256color");
    REQUIRE(colorterm == "truecolor");
    REQUIRE(term_program == "draxul");

    std::filesystem::remove(dump_path);
}

#endif
