// Tiny portable log: printf-style, goes to stderr unless a backend installs a sink
// (Android -> logcat, PS3 -> TTY).
#pragma once

namespace hx {
using LogSink = void (*)(const char* line);
void setLogSink(LogSink sink);
void logMsg(const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
} // namespace hx
