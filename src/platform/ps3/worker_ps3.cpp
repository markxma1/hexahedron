// hx::Worker on a native PPU thread (the PPU runs two hardware threads).
#include "../../game/worker.h"
#include <sys/thread.h>

namespace hx {

static void entry(void* arg) {
    (*static_cast<std::function<void()>*>(arg))();
    sysThreadExit(0);
}

bool Worker::start(std::function<void()> fn) {
    join();
    fn_ = std::move(fn);
    sys_ppu_thread_t id;
    // Priority: higher number = lower priority than the main loop (1000).
    if (sysThreadCreate(&id, entry, &fn_, 1500, 256 * 1024, THREAD_JOINABLE, (char*)"hx_worker") != 0)
        return false;
    id_ = id;
    started_ = true;
    return true;
}

void Worker::join() {
    if (!started_) return;
    u64 ret;
    sysThreadJoin((sys_ppu_thread_t)id_, &ret);
    started_ = false;
}

} // namespace hx
