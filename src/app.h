#pragma once

#include <string>

#include "config.h"
#include "mc_runtime.h"
#include "modules.h"

namespace oy {

class LuaManager;

struct App {
    std::string baseDir;
    std::string configPath;
    int port = 8712;

    Config config;
    ModuleRegistry modules;
    McRuntime mc;
    LuaManager* lua = nullptr;

    bool saveConfig(std::string& err);
    void seedDefaults();
};

}
