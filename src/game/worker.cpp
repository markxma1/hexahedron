// Default Worker on top of std::thread. Platforms without it define
// HX_NO_THREADS and provide their own worker_*.cpp.
#ifndef HX_NO_THREADS
#include "worker.h"
#include <thread>

namespace hx {

bool Worker::start(std::function<void()> fn) {
    join();
    fn_ = std::move(fn);
    handle_ = new std::thread([this]() { fn_(); });
    started_ = true;
    return true;
}

void Worker::join() {
    if (!started_) return;
    auto* t = static_cast<std::thread*>(handle_);
    t->join();
    delete t;
    handle_ = nullptr;
    started_ = false;
}

} // namespace hx
#endif
