#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace oy {

struct ModuleDef {
    std::string id;
    std::string name;
    std::string source = "builtin";
    nlohmann::json defaultValues = nlohmann::json::object();
};

class ModuleRegistry {
public:
    void loadBuiltins();

    bool registerModule(const std::string& id, const std::string& name,
                        const std::string& source, nlohmann::json defaultValues,
                        std::string& err);

    std::vector<ModuleDef> list() const;
    const ModuleDef* find(const std::string& id) const;
    bool has(const std::string& id) const;

    void resetDynamic();

private:
    std::map<std::string, ModuleDef> defs_;
};

}
