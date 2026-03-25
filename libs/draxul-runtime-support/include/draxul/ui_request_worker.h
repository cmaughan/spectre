#pragma once

#include <condition_variable>
#include <draxul/nvim_rpc.h>
#include <mutex>
#include <string>
#include <thread>

#include <draxul/ui_request_worker_state.h>

namespace draxul
{

class UiRequestWorker
{
public:
    void start(IRpcChannel* rpc);
    void stop();
    void request_resize(int cols, int rows, std::string reason);

private:
    void thread_main();

    IRpcChannel* rpc_ = nullptr;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    UiRequestWorkerState state_;
};

} // namespace draxul
