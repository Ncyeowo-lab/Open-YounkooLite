#include "modules.h"

namespace oy {

using json = nlohmann::json;

void ModuleRegistry::loadBuiltins() {
    defs_.clear();

    auto add = [this](const char* id, const char* name, json dv) {
        ModuleDef d;
        d.id = id;
        d.name = name;
        d.source = "builtin";
        d.defaultValues = std::move(dv);
        defs_[d.id] = d;
    };

    add("aim_assist", "Aim Assist", json{
        {"Adaptive Offset", 3.0}, {"Adaptive Strafe", true}, {"Aim Distance", 4.0},
        {"Aim Key (LMB)", true}, {"Aim Mode", 0}, {"Disable While Mining", false},
        {"Fov", 35.0}, {"Horizontal Only", false}, {"MC Sensitivity", 0.5},
        {"Random Pitch", 0.025}, {"Random Yaw", 0.35}, {"Smooth", 15.0},
        {"Target Priority", 1}, {"Visibility Check", true}});

    add("anti_bot", "Anti Bot", json{{"Check LatestBot", true}, {"Check Shop", true}});

    add("auto_tool", "Auto Tool", json{
        {"Need Shift Pressed", false}, {"No Sword", false}, {"Switch Back", false}});

    add("cave_finder", "Cave Finder", json{{"Chunk Radius", 64}});

    add("clicker", "Clicker", json{
        {"Allow Bows", false}, {"Allow Ender Pearls", false}, {"Block Hit", false},
        {"BlockHit Interval", 3.0}, {"Fast Place Blocks", true},
        {"High Version Cooldown", false}, {"Inventory", false}, {"Jitter", false},
        {"Jitter Strength", 2}, {"Left Max CPS", 14.0}, {"Left Min CPS", 8.0},
        {"Mode", 0}, {"Right Max CPS", 14.0}, {"Right Min CPS", 8.0}});

    add("jump_reset", "Jump Reset", json{
        {"Cooldown Ms", 250}, {"Hold Ms", 24}, {"Ignore Fall Damage", true},
        {"Ignore Projectile", true}, {"Only Ground", true}});

    add("teams", "Teams", json{{"Mode", 0}});
}

bool ModuleRegistry::registerModule(const std::string& id, const std::string& name,
                                    const std::string& source, json defaultValues,
                                    std::string& err) {
    if (id.empty()) {
        err = "module id is required";
        return false;
    }
    ModuleDef d;
    d.id = id;
    d.name = name.empty() ? id : name;
    d.source = source.empty() ? "lua" : source;
    d.defaultValues = defaultValues.is_object() ? std::move(defaultValues) : json::object();
    defs_[d.id] = d;
    return true;
}

std::vector<ModuleDef> ModuleRegistry::list() const {
    std::vector<ModuleDef> out;
    out.reserve(defs_.size());
    for (const auto& kv : defs_) out.push_back(kv.second);
    return out;
}

const ModuleDef* ModuleRegistry::find(const std::string& id) const {
    auto it = defs_.find(id);
    return it == defs_.end() ? nullptr : &it->second;
}

bool ModuleRegistry::has(const std::string& id) const { return defs_.count(id) != 0; }

void ModuleRegistry::resetDynamic() {
    for (auto it = defs_.begin(); it != defs_.end();) {
        it = (it->second.source == "lua") ? defs_.erase(it) : std::next(it);
    }
}

}
