#pragma once

#include <array>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include <lua.h>
}

#include <nlohmann/json.hpp>

namespace oy {

class App;

class LuaManager {
public:
    explicit LuaManager(App& app) : app_(app) {}

    bool init(std::string& err);
    void shutdown();

    bool reload(std::string& err);

    bool isReady() const { return L_ != nullptr; }
    int scriptCount() const { return scriptCount_; }
    const std::string& scriptDir() const { return scriptDir_; }

    void setScriptDir(const std::string& dir) { scriptDir_ = dir; }

    void tick();

    void callHook(const char* name);

    void pushJsonFor(const nlohmann::json& j) { pushJson(j); }
    nlohmann::json argToJson(int idx) { return toJson(idx); }

private:
    void registerBindings();
    bool runFile(const std::string& path, std::string& err);
    void pushJson(const nlohmann::json& j);
    nlohmann::json toJson(int idx);

    App& app_;
    lua_State* L_ = nullptr;
    std::string scriptDir_;
    int scriptCount_ = 0;
    std::mutex mu_;

    std::vector<int> envRefs_;
    std::vector<std::array<int, 3>> hooks_;
};

}
