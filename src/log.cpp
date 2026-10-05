#include "log.h"

#include <cstdio>
#include <ctime>

namespace oy {

std::mutex Log::mu_;
std::vector<std::string> Log::ring_;

const char* levelName(Level l) {
    switch (l) {
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
    }
    return "?";
}

void Log::write(Level level, const std::string& msg) {
    time_t now = time(nullptr);
    tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

    std::string line = std::string("[") + buf + "] [" + levelName(level) + "] " + msg;

    {
        std::lock_guard<std::mutex> g(mu_);
        ring_.push_back(line);
        if (ring_.size() > 500) ring_.erase(ring_.begin());
    }

    printf("%s\n", line.c_str());
    fflush(stdout);
}

std::vector<std::string> Log::tail(size_t n) {
    std::lock_guard<std::mutex> g(mu_);
    if (ring_.size() <= n) return ring_;
    return std::vector<std::string>(ring_.end() - static_cast<long>(n), ring_.end());
}

}
