#pragma once
#include <functional>

// Threads that run on their own, with nobody to join them: the Shadowsocks
// relay of each connection, the Windows SSH-agent bridge, the Shadowsocks IP
// check. They are started through start() so that the program can stop them
// on exit: shutdown() raises stopping(), closes what they wait on (see
// watch()) and waits until they have returned — before the objects they talk
// to are destroyed and before libssh / OpenSSL are torn down.
namespace bgthreads {

// Runs fn on a new thread, tracked until fn returns. Refused (returns false,
// fn not run) once shutdown() has begun.
bool start(std::function<void()> fn);

// True once shutdown() has begun: loops check it and leave.
bool stopping();

// A socket a worker blocks on (recv, accept, poll). shutdown() calls
// ::shutdown() on it (listening sockets: closes it), so the call returns.
// unwatch() before closing it yourself.
void watch(long long socket, bool listening = false);
void unwatch(long long socket);

// Stops the workers and waits at most timeoutMs. Returns how many were still
// running (0 = all finished). Called once, from the main thread, on exit.
int shutdown(int timeoutMs);

// Number of workers running now (tests).
int running();

} // namespace bgthreads
