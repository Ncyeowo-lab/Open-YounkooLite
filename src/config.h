#pragma once

#include <map>
#include <string>

#include <nlohmann/json.hpp>

namespace oy {

struct ModuleSetting {
    bool enabled = false;
    int keybind = 0;
    nlohmann::json values = nlohmann::json::object();
};

class Config {
public:
    int version = 1;
    std::map<std::string, ModuleSetting> modules;

    bool load(const std::string& path, std::string& err);
    bool save(const std::string& path, std::string& err) const;

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

    ModuleSetting& slot(const std::string& id);
    const ModuleSetting* find(const std::string& id) const;
};

}
