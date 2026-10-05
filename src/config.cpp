#include "config.h"

#include <fstream>

#include "log.h"

namespace oy {

using json = nlohmann::json;

json Config::toJson() const {
    json j = json::object();
    json mods = json::object();
    for (const auto& kv : modules) {
        mods[kv.first] = json{{"enabled", kv.second.enabled},
                              {"keybind", kv.second.keybind},
                              {"values", kv.second.values}};
    }
    j["modules"] = mods;
    j["version"] = version;
    return j;
}

void Config::fromJson(const json& j) {
    modules.clear();
    version = j.value("version", 1);
    if (!j.contains("modules") || !j["modules"].is_object()) return;

    for (auto it = j["modules"].begin(); it != j["modules"].end(); ++it) {
        const json& m = it.value();
        ModuleSetting s;
        s.enabled = m.value("enabled", false);
        s.keybind = m.value("keybind", 0);
        s.values = m.contains("values") && m["values"].is_object() ? m["values"] : json::object();
        modules[it.key()] = s;
    }
}

bool Config::load(const std::string& path, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err.clear();
        return true;
    }
    try {
        json j;
        in >> j;
        fromJson(j);
        return true;
    } catch (const std::exception& e) {
        err = std::string("Invalid config, reset to defaults: ") + e.what();
        Log::warn(err);
        return false;
    }
}

bool Config::save(const std::string& path, std::string& err) const {
    try {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            err = "cannot write " + path;
            return false;
        }
        out << toJson().dump(2);
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

ModuleSetting& Config::slot(const std::string& id) {
    auto it = modules.find(id);
    if (it == modules.end()) it = modules.emplace(id, ModuleSetting{}).first;
    return it->second;
}

const ModuleSetting* Config::find(const std::string& id) const {
    auto it = modules.find(id);
    return it == modules.end() ? nullptr : &it->second;
}

}
