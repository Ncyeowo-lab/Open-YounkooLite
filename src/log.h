#pragma once

#include <string>
#include <vector>
#include <mutex>

namespace oy {

enum class Level { Debug, Info, Warn, Error };

class Log {
public:
    static void write(Level level, const std::string& msg);
    static void debug(const std::string& msg) { write(Level::Debug, msg); }
    static void info(const std::string& msg) { write(Level::Info, msg); }
    static void warn(const std::string& msg) { write(Level::Warn, msg); }
    static void error(const std::string& msg) { write(Level::Error, msg); }

    static std::vector<std::string> tail(size_t n = 200);

private:
    static std::mutex mu_;
    static std::vector<std::string> ring_;
};

const char* levelName(Level l);

}
