#include "lua_manager.h"

extern "C" {
#include <lauxlib.h>
#include <lualib.h>
}

#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "app.h"
#include "log.h"

namespace oy {

using json = nlohmann::json;

namespace {
LuaManager* g_self = nullptr;
}

void LuaManager::pushJson(const json& j) {
    switch (j.type()) {
        case json::value_t::null: lua_pushnil(L_); break;
        case json::value_t::boolean: lua_pushboolean(L_, j.get<bool>() ? 1 : 0); break;
        case json::value_t::number_integer:
        case json::value_t::number_unsigned: lua_pushinteger(L_, j.get<lua_Integer>()); break;
        case json::value_t::number_float: lua_pushnumber(L_, j.get<lua_Number>()); break;
        case json::value_t::string: lua_pushstring(L_, j.get<std::string>().c_str()); break;
        case json::value_t::array: {
            lua_newtable(L_);
            lua_Integer i = 1;
            for (const auto& v : j) { pushJson(v); lua_rawseti(L_, -2, i++); }
            break;
        }
        case json::value_t::object: {
            lua_newtable(L_);
            for (auto it = j.begin(); it != j.end(); ++it) {
                lua_pushstring(L_, it.key().c_str());
                pushJson(it.value());
                lua_rawset(L_, -3);
            }
            break;
        }
        default: lua_pushnil(L_); break;
    }
}

json LuaManager::toJson(int idx) {
    const int t = lua_type(L_, idx);
    switch (t) {
        case LUA_TNIL:
        case LUA_TNONE: return json(nullptr);
        case LUA_TBOOLEAN: return lua_toboolean(L_, idx) != 0;
        case LUA_TNUMBER: {
            if (lua_isinteger(L_, idx)) return static_cast<long long>(lua_tointeger(L_, idx));
            return lua_tonumber(L_, idx);
        }
        case LUA_TSTRING: return std::string(lua_tostring(L_, idx));
        case LUA_TTABLE: {
            lua_len(L_, idx);
            const lua_Integer n = lua_tointeger(L_, -1);
            lua_pop(L_, 1);

            bool isArray = n > 0;
            if (isArray) {
                for (lua_Integer i = 1; i <= n; ++i) {
                    lua_rawgeti(L_, idx, i);
                    if (lua_isnil(L_, -1)) { isArray = false; }
                    lua_pop(L_, 1);
                    if (!isArray) break;
                }
            }

            if (isArray) {
                json arr = json::array();
                for (lua_Integer i = 1; i <= n; ++i) { lua_rawgeti(L_, idx, i); arr.push_back(toJson(-1)); lua_pop(L_, 1); }
                return arr;
            }

            json obj = json::object();
            lua_pushnil(L_);
            while (lua_next(L_, idx < 0 ? idx - 1 : idx) != 0) {
                if (lua_type(L_, -2) == LUA_TSTRING) {
                    obj[lua_tostring(L_, -2)] = toJson(-1);
                }
                lua_pop(L_, 1);
            }
            return obj;
        }
        default: return json(nullptr);
    }
}

namespace {

int l_log(lua_State* L) {
    const char* s = luaL_optstring(L, 1, "");
    Log::info(std::string("[lua] ") + s);
    return 0;
}

int l_get_entities(lua_State* L) {
    if (!g_self) return 0;
    App& app = *reinterpret_cast<App*>(lua_touserdata(L, lua_upvalueindex(1)));
    (void)L;
    json v = app.mc.entities();
    g_self->pushJsonFor(v);
    return 1;
}

int l_get_camera(lua_State* L) {
    (void)L;
    App& app = *reinterpret_cast<App*>(lua_touserdata(L, lua_upvalueindex(1)));
    g_self->pushJsonFor(app.mc.camera());
    return 1;
}

int l_get_matrix(lua_State* L) {
    (void)L;
    App& app = *reinterpret_cast<App*>(lua_touserdata(L, lua_upvalueindex(1)));
    g_self->pushJsonFor(app.mc.matrix());
    return 1;
}

int l_read_field(lua_State* L) {
    App& app = *reinterpret_cast<App*>(lua_touserdata(L, lua_upvalueindex(1)));
    json req = g_self->argToJson(1);
    json out; std::string err;
    if (!app.mc.readField(req, out, err)) {
        lua_pushnil(L);
        lua_pushstring(L, err.c_str());
        return 2;
    }
    g_self->pushJsonFor(out);
    return 1;
}

int l_write_field(lua_State* L) {
    App& app = *reinterpret_cast<App*>(lua_touserdata(L, lua_upvalueindex(1)));
    json req = g_self->argToJson(1);
    std::string err;
    bool ok = app.mc.writeField(req, err);
    lua_pushboolean(L, ok ? 1 : 0);
    if (!ok) lua_pushstring(L, err.c_str());
    else lua_pushnil(L);
    return 2;
}

int l_register_mapping(lua_State* L) {
    App& app = *reinterpret_cast<App*>(lua_touserdata(L, lua_upvalueindex(1)));
    json m = g_self->argToJson(1);
    std::string err;
    bool ok = app.mc.addMapping(m, err);
    lua_pushboolean(L, ok ? 1 : 0);
    lua_pushstring(L, err.c_str());
    return 2;
}

int l_register_module(lua_State* L) {
    App& app = *reinterpret_cast<App*>(lua_touserdata(L, lua_upvalueindex(1)));
    json m = g_self->argToJson(1);
    std::string err;
    json dv = m.contains("values") && m["values"].is_object() ? m["values"] : json::object();
    bool ok = app.modules.registerModule(m.value("id", ""), m.value("name", ""), "lua", dv, err);
    if (ok) {
        app.config.slot(m.value("id", ""));
        if (!dv.empty()) app.config.slot(m.value("id", "")).values = dv;
    }
    lua_pushboolean(L, ok ? 1 : 0);
    lua_pushstring(L, err.c_str());
    return 2;
}

}

bool LuaManager::init(std::string& err) {
    g_self = this;
    L_ = luaL_newstate();
    if (!L_) { err = "Failed to create Lua state"; return false; }
    luaL_openlibs(L_);
    registerBindings();
    return true;
}

void LuaManager::shutdown() {
    if (L_) { lua_close(L_); L_ = nullptr; }
    g_self = nullptr;
}

void LuaManager::registerBindings() {
    lua_newtable(L_);

    struct B { const char* name; lua_CFunction fn; };
    const B bindings[] = {
        {"log", l_log},
        {"get_entities", l_get_entities},
        {"get_camera", l_get_camera},
        {"get_matrix", l_get_matrix},
        {"read_field", l_read_field},
        {"write_field", l_write_field},
        {"register_mapping", l_register_mapping},
        {"register_module", l_register_module},
    };

    for (const auto& b : bindings) {
        lua_pushlightuserdata(L_, &app_);
        lua_pushcclosure(L_, b.fn, 1);
        lua_setfield(L_, -2, b.name);
    }

    lua_setglobal(L_, "younkoo");
}

bool LuaManager::runFile(const std::string& path, std::string& err) {
    if (luaL_loadfile(L_, path.c_str()) != LUA_OK) {
        err = std::string("load failed: ") + (lua_isstring(L_, -1) ? lua_tostring(L_, -1) : "?");
        lua_pop(L_, 1);
        return false;
    }

    lua_newtable(L_);
    lua_newtable(L_);
    lua_pushglobaltable(L_);
    lua_setfield(L_, -2, "__index");
    lua_setmetatable(L_, -2);

    lua_pushvalue(L_, -1);
    if (lua_setupvalue(L_, -3, 1) == nullptr) {
        lua_pop(L_, 2);
        err = "cannot set _ENV";
        return false;
    }
    lua_insert(L_, -2);

    if (lua_pcall(L_, 0, 0, 0) != LUA_OK) {
        err = lua_isstring(L_, -1) ? lua_tostring(L_, -1) : "runtime error";
        lua_pop(L_, 2);
        return false;
    }

    const int envRef = luaL_ref(L_, LUA_REGISTRYINDEX);
    envRefs_.push_back(envRef);

    auto hookRef = [&](const char* name) {
        lua_rawgeti(L_, LUA_REGISTRYINDEX, envRef);
        lua_getfield(L_, -1, name);
        int r = LUA_NOREF;
        if (lua_isfunction(L_, -1)) r = luaL_ref(L_, LUA_REGISTRYINDEX);
        else lua_pop(L_, 1);
        lua_pop(L_, 1);
        return r;
    };

    hooks_.push_back({hookRef("on_enable"), hookRef("on_update"), hookRef("on_disable")});
    return true;
}

bool LuaManager::reload(std::string& err) {
    std::lock_guard<std::mutex> g(mu_);
    if (!L_) { err = "lua manager is unavailable"; return false; }

    for (auto r : envRefs_) luaL_unref(L_, LUA_REGISTRYINDEX, r);
    for (auto& h : hooks_) {
        luaL_unref(L_, LUA_REGISTRYINDEX, h[0]);
        luaL_unref(L_, LUA_REGISTRYINDEX, h[1]);
        luaL_unref(L_, LUA_REGISTRYINDEX, h[2]);
    }
    envRefs_.clear();
    hooks_.clear();
    scriptCount_ = 0;

    app_.modules.resetDynamic();
    app_.mc.clearMappings();

    std::error_code ec;
    if (!std::filesystem::exists(scriptDir_, ec)) {
        std::filesystem::create_directories(scriptDir_, ec);
    }

    std::vector<std::string> files;
    for (const auto& e : std::filesystem::directory_iterator(scriptDir_, ec)) {
        if (e.is_regular_file() && e.path().extension() == ".lua") {
            files.push_back(e.path().string());
        }
    }
    std::sort(files.begin(), files.end());

    for (const auto& f : files) {
        std::string e2;
        if (runFile(f, e2)) {
            ++scriptCount_;
            Log::info("loaded script: " + f);
        } else {
            Log::error(std::string("[lua] ") + f + " -> " + e2);
        }
    }

    std::string msg = "Loaded " + std::to_string(scriptCount_) + " Lua script(s)";
    Log::info(msg);
    callHook("on_enable");
    return true;
}

void LuaManager::callHook(const char* name) {
    if (!L_) return;
    const int which = (std::string(name) == "on_enable") ? 0
                    : (std::string(name) == "on_update") ? 1
                    : (std::string(name) == "on_disable") ? 2
                    : -1;
    if (which < 0) return;

    for (const auto& h : hooks_) {
        if (h[which] == LUA_NOREF) continue;
        lua_rawgeti(L_, LUA_REGISTRYINDEX, h[which]);
        if (lua_pcall(L_, 0, 0, 0) != LUA_OK) {
            Log::error(std::string("[lua] hook ") + name + ": " +
                       (lua_isstring(L_, -1) ? lua_tostring(L_, -1) : "?"));
            lua_pop(L_, 1);
        }
    }
}

void LuaManager::tick() { callHook("on_update"); }

}
