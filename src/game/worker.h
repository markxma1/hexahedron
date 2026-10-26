// One background worker thread. The game code only uses this class;
// each platform supplies the implementation (std::thread by default,
// native PPU threads on PS3).
#pragma once
#include <cstdint>
#include <functional>

namespace hx {

class Worker {
public:
    Worker() = default;
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    ~Worker() { join(); }

    bool start(std::function<void()> fn); // false: could not start (fn not run)
    void join();                           // waits if running, safe to call twice

private:
    std::function<void()> fn_;
    void* handle_ = nullptr;   // platform data
    uint64_t id_ = 0;
    bool started_ = false;
};

} // namespace hx
