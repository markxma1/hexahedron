#include "log.h"
#include <cstdarg>
#include <cstdio>

namespace hx {

static LogSink sSink = nullptr;

void setLogSink(LogSink sink) { sSink = sink; }

void logMsg(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (sSink) sSink(buf);
    else std::fprintf(stderr, "%s\n", buf);
}

} // namespace hx
