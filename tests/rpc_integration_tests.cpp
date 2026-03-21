#include "support/test_support.h"

#include <chrono>
#include <cstdlib>
#include <draxul/log.h>
#include <draxul/nvim.h>
#include <filesystem>
#include <string>
#include <thread>

using namespace draxul;
using namespace draxul::tests;

namespace
{

class ScopedEnvVar
{
public:
    ScopedEnvVar(const char* name, const char* value)
        : name_(name)
    {
        const char* existing = std::getenv(name_);
        if (existing)
        {
            had_original_ = true;
            original_ = existing;
        }
        set(value);
    }

    ~ScopedEnvVar()
    {
        if (had_original_)
            set(original_.c_str());
        else
            clear();
    }

private:
    void set(const char* value)
    {
#ifdef _WIN32
        _putenv_s(name_, value);
#else
        setenv(name_, value, 1);
#endif
    }

    void clear()
    {
#ifdef _WIN32
        _putenv_s(name_, "");
#else
        unsetenv(name_);
#endif
    }

    const char* name_;
    bool had_original_ = false;
    std::string original_;
};

std::string helper_path()
{
    return DRAXUL_RPC_FAKE_PATH;
}

bool has_log_message(const std::vector<LogRecord>& records, LogLevel level, LogCategory category, std::string_view needle)
{
    for (const auto& record : records)
    {
        if (record.level == level && record.category == category && record.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

RpcResult run_request_with_mode(const char* mode, std::vector<RpcNotification>* notifications = nullptr)
{
    ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", mode);
    NvimProcess process;
    expect(process.spawn(helper_path()), "fake RPC server spawns");

    NvimRpc rpc;
    expect(rpc.initialize(process), "rpc initializes");

    RpcResult result = rpc.request("fake_method", { NvimRpc::make_int(7) });
    if (notifications)
        *notifications = rpc.drain_notifications();

    rpc.shutdown();
    process.shutdown();
    return result;
}

} // namespace

void run_rpc_integration_tests()
{
    run_test("nvim rpc returns successful responses from the transport", []() {
        RpcResult result = run_request_with_mode("success");

        expect(result.transport_ok, "request reports transport success");
        expect(result.ok(), "request reports overall success");
        expect_eq(result.result.as_str(), std::string("ok"), "request result payload survives");
        expect(result.error.is_nil(), "successful response keeps nil error");
    });

    run_test("nvim rpc surfaces remote errors without losing transport success", []() {
        RpcResult result = run_request_with_mode("error");

        expect(result.transport_ok, "remote error still counts as transport success");
        expect(!result.ok(), "remote error makes the request unsuccessful");
        expect(result.is_error(), "remote error is surfaced");
        expect_eq(result.error.as_str(), std::string("boom"), "remote error payload survives");
    });

    run_test("nvim rpc aborts cleanly when the child exits without responding", []() {
        auto start = std::chrono::steady_clock::now();
        RpcResult result = run_request_with_mode("abort_after_read");
        auto elapsed = std::chrono::steady_clock::now() - start;

        expect(!result.transport_ok, "aborted transport reports failure");
        expect(!result.ok(), "aborted transport is not successful");
        expect(elapsed < std::chrono::seconds(2), "aborted transport returns promptly without timing out");
    });

    run_test("nvim rpc queues notifications received before the response", []() {
        std::vector<RpcNotification> notifications;
        RpcResult result = run_request_with_mode("notify_then_success", &notifications);

        expect(result.ok(), "request still succeeds when a notification precedes the response");
        expect_eq(static_cast<int>(notifications.size()), 1, "notification is queued");
        expect_eq(notifications[0].method, std::string("redraw"), "notification method survives");
        expect_eq(static_cast<int>(notifications[0].params.size()), 1, "notification params survive");
        expect_eq(notifications[0].params[0].type(), MpackValue::Array, "notification param payload survives");
    });

    run_test("nvim rpc treats malformed responses as transport failure", []() {
        auto start = std::chrono::steady_clock::now();
        RpcResult result = run_request_with_mode("malformed_response");
        auto elapsed = std::chrono::steady_clock::now() - start;

        expect(!result.transport_ok, "malformed response reports transport failure");
        expect(!result.ok(), "malformed response is not successful");
        expect(elapsed < std::chrono::seconds(2), "malformed response returns promptly without timing out");
    });

    run_test("nvim rpc close() unblocks an in-flight request without waiting for timeout", []() {
        ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "hang");
        NvimProcess process;
        expect(process.spawn(helper_path()), "fake RPC server spawns");

        NvimRpc rpc;
        expect(rpc.initialize(process), "rpc initializes");

        // Send a request that will block forever (server never responds in hang mode).
        std::thread requester([&rpc]() {
            rpc.request("fake_method", { NvimRpc::make_int(7) });
        });

        // Give the request time to send and block on the response CV.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        auto start = std::chrono::steady_clock::now();
        rpc.close();
        requester.join();
        auto elapsed = std::chrono::steady_clock::now() - start;

        expect(elapsed < std::chrono::seconds(2), "close() unblocks in-flight request promptly, not after 5s timeout");

        process.shutdown();
        rpc.shutdown();
    });

    run_test("nvim rpc request from a worker thread completes successfully", []() {
        ScopedEnvVar env("DRAXUL_RPC_FAKE_MODE", "success");
        NvimProcess process;
        expect(process.spawn(helper_path()), "fake RPC server spawns");

        NvimRpc rpc;
        expect(rpc.initialize(process), "rpc initializes");

        RpcResult result;
        std::thread worker([&rpc, &result]() {
            result = rpc.request("fake_method", { NvimRpc::make_int(7) });
        });
        worker.join();

        expect(result.transport_ok, "worker-thread request reports transport success");
        expect(result.ok(), "worker-thread request reports overall success");
        expect_eq(result.result.as_str(), std::string("ok"), "worker-thread result payload survives");

        rpc.shutdown();
        process.shutdown();
    });

    run_test("nvim rpc logs a warning when the transport aborts before a response arrives", []() {
        ScopedLogCapture capture;

        RpcResult result = run_request_with_mode("abort_after_read");

        expect(!result.transport_ok, "aborted transport reports failure");
        expect(has_log_message(capture.records, LogLevel::Warn, LogCategory::Rpc, "Request timed out or aborted: fake_method"),
            "aborted request should emit an rpc warning");
    });
}
