#include "BackgroundThreads.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>

#ifdef _WIN32
#  include <winsock2.h>
#else
#  include <sys/socket.h>
#  include <unistd.h>
#endif

namespace bgthreads {
namespace {

struct State
{
    std::mutex mutex;
    std::condition_variable done;
    int running = 0;
    std::set<long long> sockets, listening;
};

// Never destroyed: detached threads may still touch it while statics are torn down.
State &state()
{
    static State *s = new State;
    return *s;
}

std::atomic<bool> g_stopping{false};

void cut(long long s, bool listening)
{
#ifdef _WIN32
    if (listening)
        ::closesocket(static_cast<SOCKET>(s));
    else
        ::shutdown(static_cast<SOCKET>(s), SD_BOTH);
#else
    ::shutdown(int(s), SHUT_RDWR);   // also wakes accept() on Linux
    if (listening)
        ::close(int(s));
#endif
}

} // namespace

bool start(std::function<void()> fn)
{
    State &st = state();
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        if (g_stopping.load())
            return false;
        ++st.running;
    }
    try {
        std::thread([fn = std::move(fn)]() mutable {
            try {
                fn();
            } catch (...) {
                // a worker must never take the program down
            }
            fn = nullptr;   // what it captured (sockets, sessions) goes now, not after "done"
            State &s = state();
            std::lock_guard<std::mutex> lock(s.mutex);
            --s.running;
            s.done.notify_all();
        }).detach();
    } catch (...) {   // no thread could be created
        std::lock_guard<std::mutex> lock(st.mutex);
        --st.running;
        return false;
    }
    return true;
}

bool stopping()
{
    return g_stopping.load(std::memory_order_relaxed);
}

void watch(long long socket, bool listening)
{
    State &st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    if (g_stopping.load()) {   // too late: stop it right away
        cut(socket, listening);
        return;
    }
    (listening ? st.listening : st.sockets).insert(socket);
}

void unwatch(long long socket)
{
    State &st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.sockets.erase(socket);
    st.listening.erase(socket);
}

int shutdown(int timeoutMs)
{
    State &st = state();
    std::unique_lock<std::mutex> lock(st.mutex);
    g_stopping.store(true);
    for (long long s : st.sockets)
        cut(s, false);
    for (long long s : st.listening)
        cut(s, true);
    st.sockets.clear();
    st.listening.clear();
    st.done.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return st.running == 0; });
    return st.running;
}

int running()
{
    State &st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    return st.running;
}

} // namespace bgthreads
