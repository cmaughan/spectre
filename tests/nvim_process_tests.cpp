#include "support/test_support.h"

#include <draxul/log.h>
#include <draxul/nvim_rpc.h>

#include <string>
#include <vector>

using namespace draxul;
using namespace draxul::tests;

namespace
{

std::string missing_nvim_path()
{
#ifdef _WIN32
    return "C:\\definitely-missing\\draxul-test-nvim.exe";
#else
    return "/definitely-missing/draxul-test-nvim";
#endif
}

bool contains_spawn_error(const std::vector<LogRecord>& records)
{
    for (const auto& record : records)
    {
        if (record.category == LogCategory::Nvim
            && record.level == LogLevel::Error
            && record.message.find("Failed to spawn nvim") != std::string::npos)
            return true;
    }
    return false;
}

} // namespace

void run_nvim_process_tests()
{
    run_test("nvim process spawn returns false and logs an error for a bad path", []() {
        ScopedLogCapture capture;
        NvimProcess process;

        expect(!process.spawn(missing_nvim_path()), "spawn should fail for a missing nvim binary");
        expect(!process.is_running(), "failed spawn should not leave a running process");
        expect(contains_spawn_error(capture.records), "failed spawn should emit an error log");
    });

    run_test("nvim process shutdown is a no-op after spawn failure", []() {
        NvimProcess process;

        expect(!process.spawn(missing_nvim_path()), "spawn should fail for a missing nvim binary");
        process.shutdown();
        expect(!process.is_running(), "shutdown after failed spawn should remain safe");
    });
}
