#include "support/test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <draxul/log.h>

#include <string>
#include <vector>

using namespace draxul;
using namespace draxul::tests;

TEST_CASE("logger filters by level", "[log]")
{
    ScopedLogCapture capture;
    LogOptions options;
    options.min_level = LogLevel::Warn;
    options.enable_stderr = false;
    configure_logging(options);
    set_log_sink([&capture](const LogRecord& record) { capture.records.push_back(record); });

    log_message(LogLevel::Info, LogCategory::App, "hidden");
    log_message(LogLevel::Warn, LogCategory::App, "shown");

    REQUIRE(capture.records.size() == 1);
    REQUIRE(capture.records[0].message == std::string("shown"));
}

TEST_CASE("logger filters by category", "[log]")
{
    ScopedLogCapture capture;
    LogOptions options;
    options.enable_stderr = false;
    options.enabled_categories = { LogCategory::Rpc };
    configure_logging(options);
    set_log_sink([&capture](const LogRecord& record) { capture.records.push_back(record); });

    log_message(LogLevel::Error, LogCategory::App, "skip");
    log_message(LogLevel::Error, LogCategory::Rpc, "keep");

    REQUIRE(capture.records.size() == 1);
    REQUIRE(capture.records[0].category == LogCategory::Rpc);
}
