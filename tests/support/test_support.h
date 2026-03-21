#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <draxul/log.h>

namespace draxul::tests
{

struct ScopedLogCapture
{
    std::vector<LogRecord> records;

    ScopedLogCapture()
    {
        LogOptions options;
        options.enable_stderr = false;
        options.enable_file = false;
        configure_logging(options);
        set_log_sink([this](const LogRecord& record) { records.push_back(record); });
    }

    ~ScopedLogCapture()
    {
        clear_log_sink();
        configure_logging();
    }
};

class TestSkipped : public std::runtime_error
{
public:
    explicit TestSkipped(std::string message)
        : std::runtime_error(std::move(message))
    {
    }
};

inline void expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        throw std::runtime_error(std::string(message));
    }
}

[[noreturn]] inline void skip(std::string_view message)
{
    throw TestSkipped(std::string(message));
}

template <typename T, typename U>
inline void expect_eq(const T& actual, const U& expected, std::string_view message)
{
    if (!(actual == expected))
    {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Fn>
inline void run_test(std::string_view name, Fn&& fn)
{
    try
    {
        fn();
        DRAXUL_LOG_INFO(LogCategory::Test, "[ok] %.*s", static_cast<int>(name.size()), name.data());
    }
    catch (const TestSkipped& ex)
    {
        DRAXUL_LOG_INFO(LogCategory::Test, "[skip] %.*s: %s",
            static_cast<int>(name.size()), name.data(), ex.what());
    }
    catch (const std::exception& ex)
    {
        throw std::runtime_error(std::string(name) + ": " + ex.what());
    }
}

} // namespace draxul::tests
