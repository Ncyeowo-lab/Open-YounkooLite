#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

#include "app.h"
#include "http_server.h"
#include "log.h"
#include "lua_manager.h"

namespace {

volatile sig_atomic_t g_running = 1;

void onSignal(int) { g_running = 0; }

void usage(const char* exe) {
    printf("openyounkoolite - open reimplementation of younkoo-lite\n\n"
           "usage: %s [options]\n"
           "  --dir  <path>   数据目录（config.json / scripts），默认 C:\\openyounkoo-lite\n"
           "  --port <n>      Web 面板端口，默认 8712（被占用会自动往后找 10 个）\n"
           "  --help          显示本帮助\n",
           exe);
}

}

int main(int argc, char** argv) {
    std::string dir = "C:\\openyounkoo-lite";
    int port = 8712;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "--dir") && i + 1 < argc) dir = argv[++i];
        else if ((a == "--port") && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--help" || a == "-h") { usage(argv[0]); return 0; }
    }

    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    oy::Log::info("openyounkoolite starting");

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::filesystem::create_directories(dir + "\\scripts", ec);

    oy::App app;
    app.baseDir = dir;
    app.configPath = dir + "\\config.json";
    app.port = port;

    app.modules.loadBuiltins();

    std::string err;
    if (!app.config.load(app.configPath, err)) {
        oy::Log::warn(err);
    }
    app.seedDefaults();
    if (!std::filesystem::exists(app.configPath, ec)) {
        if (app.saveConfig(err)) {
            oy::Log::info("wrote default config -> " + app.configPath);
        } else {
            oy::Log::error("cannot write config: " + err);
        }
    }

    oy::LuaManager lua(app);
    if (!lua.init(err)) {
        oy::Log::error("lua init failed: " + err);
    } else {
        lua.setScriptDir(dir + "\\scripts");
        app.lua = &lua;
        lua.reload(err);
    }

    oy::HttpServer http(app);
    int actual = http.start(port, err);
    if (actual == 0) {
        oy::Log::error(err);
        return 1;
    }
    app.port = actual;

    oy::Log::info("data dir: " + dir);
    oy::Log::info("ready -> http://127.0.0.1:" + std::to_string(actual));

    while (g_running) {
        if (lua.isReady()) lua.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    oy::Log::info("shutting down");
    http.stop();
    lua.shutdown();
    return 0;
}
