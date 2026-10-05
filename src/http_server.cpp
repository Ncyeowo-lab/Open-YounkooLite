#include "http_server.h"

#include <httplib.h>

#include <nlohmann/json.hpp>

#include "app.h"
#include "log.h"
#include "lua_manager.h"
#include "webui.h"

namespace oy {

using json = nlohmann::json;

namespace {
void sendJson(httplib::Response& res, int code, const json& j) {
    res.status = code;
    res.set_content(j.dump(), "application/json");
}

json parseBody(const httplib::Request& req) {
    json j = json::parse(req.body, nullptr, false);
    return j.is_discarded() ? json::object() : j;
}
}

HttpServer::~HttpServer() { stop(); }

void HttpServer::installRoutes() {
    auto& svr = *svr_;
    App& app = app_;

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(webIndex(), "text/html; charset=utf-8");
    });
    svr.Get("/radar", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(webRadar(), "text/html; charset=utf-8");
    });

    svr.Get("/api/state", [&app](const httplib::Request&, httplib::Response& res) {
        json modules = json::array();
        for (const auto& d : app.modules.list()) {
            const auto* s = app.config.find(d.id);
            modules.push_back(json{{"id", d.id}, {"name", d.name},
                                   {"source", d.source},
                                   {"enabled", s ? s->enabled : false},
                                   {"keybind", s ? s->keybind : 0},
                                   {"values", s ? s->values : d.defaultValues}});
        }
        sendJson(res, 200, json{
            {"auth", json{{"valid", true}, {"username", "local"},
                          {"latestVersion", "1.0.0"}, {"remainingSeconds", 0},
                          {"profile", json{{"username", "local"},
                                           {"expired_time", "2099-01-01"},
                                           {"is_beta", false}}}}},
            {"runtime", app.mc.status()},
            {"camera", app.mc.camera()},
            {"matrix", app.mc.matrix()},
            {"localPlayer", app.mc.localPlayer()},
            {"modules", modules},
            {"scripts", json{{"scriptCount", app.lua ? app.lua->scriptCount() : 0},
                             {"scriptDir", app.lua ? app.lua->scriptDir() : ""},
                             {"message", "ok"}}},
            {"config", json{{"path", app.configPath}, {"message", "Loaded config"}}},
            {"logs", Log::tail(200)},
            {"port", app.port},
        });
    });

    svr.Get("/api/processes", [](const httplib::Request&, httplib::Response& res) {
        json arr = json::array();
        for (const auto& p : McRuntime::listProcesses()) {
            arr.push_back(json{{"pid", p.pid}, {"name", p.name}, {"title", p.title}});
        }
        sendJson(res, 200, json{{"processes", arr}});
    });

    svr.Post("/api/attach", [&app](const httplib::Request& req, httplib::Response& res) {
        json body = parseBody(req);
        if (!body.contains("pid")) { sendJson(res, 200, json{{"ok", false}, {"reason", "pid is required"}}); return; }
        long long pid = body["pid"].get<long long>();
        if (pid == 0) { app.mc.detach(); sendJson(res, 200, json{{"ok", true}}); return; }
        std::string err;
        bool ok = app.mc.attach(static_cast<DWORD>(pid), err);
        sendJson(res, 200, json{{"ok", ok}, {"reason", err}});
    });

    svr.Get("/api/entities", [&app](const httplib::Request&, httplib::Response& res) {
        sendJson(res, 200, app.mc.entities());
    });
    svr.Get("/api/radar", [&app](const httplib::Request&, httplib::Response& res) {
        sendJson(res, 200, json{{"camera", app.mc.camera()},
                                {"players", app.mc.entities().value("players", json::array())}});
    });
    svr.Get("/api/camera", [&app](const httplib::Request&, httplib::Response& res) {
        sendJson(res, 200, app.mc.camera());
    });
    svr.Get("/api/matrix", [&app](const httplib::Request&, httplib::Response& res) {
        sendJson(res, 200, app.mc.matrix());
    });

    svr.Post("/api/read-field", [&app](const httplib::Request& req, httplib::Response& res) {
        json out; std::string err;
        bool ok = app.mc.readField(parseBody(req), out, err);
        if (ok) sendJson(res, 200, json{{"success", true}, {"result", out}});
        else sendJson(res, 200, json{{"success", false}, {"reason", err}});
    });
    svr.Post("/api/write-field", [&app](const httplib::Request& req, httplib::Response& res) {
        std::string err;
        bool ok = app.mc.writeField(parseBody(req), err);
        if (ok) sendJson(res, 200, json{{"success", true}});
        else sendJson(res, 200, json{{"success", false}, {"reason", err}});
    });

    svr.Get("/api/mappings", [&app](const httplib::Request&, httplib::Response& res) {
        sendJson(res, 200, json{{"mappings", app.mc.mappings()}, {"vmOffsets", app.mc.vmOffsets()}});
    });
    svr.Post("/api/mappings", [&app](const httplib::Request& req, httplib::Response& res) {
        std::string err;
        bool ok = app.mc.addMapping(parseBody(req), err);
        if (ok) sendJson(res, 200, json{{"success", true}});
        else sendJson(res, 200, json{{"success", false}, {"reason", err}});
    });

    svr.Get("/api/modules", [&app](const httplib::Request&, httplib::Response& res) {
        json arr = json::array();
        for (const auto& d : app.modules.list()) {
            const auto* s = app.config.find(d.id);
            arr.push_back(json{{"id", d.id}, {"name", d.name}, {"source", d.source},
                               {"enabled", s ? s->enabled : false},
                               {"keybind", s ? s->keybind : 0},
                               {"values", s ? s->values : d.defaultValues}});
        }
        sendJson(res, 200, json{{"modules", arr}});
    });

    svr.Post("/api/modules/([^/]+)/enabled", [&app](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        json body = parseBody(req);
        if (!app.modules.has(id)) { sendJson(res, 200, json{{"success", false}, {"reason", "module not found"}}); return; }
        app.config.slot(id).enabled = body.value("enabled", false);
        std::string err; app.saveConfig(err);
        sendJson(res, 200, json{{"success", err.empty()}, {"reason", err}});
    });

    svr.Post("/api/modules/([^/]+)/keybind", [&app](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        json body = parseBody(req);
        if (!app.modules.has(id)) { sendJson(res, 200, json{{"success", false}, {"reason", "module not found"}}); return; }
        if (!body.contains("keybind")) { sendJson(res, 200, json{{"success", false}, {"reason", "keybind is required"}}); return; }
        app.config.slot(id).keybind = body["keybind"].get<int>();
        std::string err; app.saveConfig(err);
        sendJson(res, 200, json{{"success", err.empty()}, {"reason", err}});
    });

    svr.Post("/api/modules/([^/]+)/values", [&app](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        json body = parseBody(req);
        if (!app.modules.has(id)) { sendJson(res, 200, json{{"success", false}, {"reason", "module not found"}}); return; }
        auto& slot = app.config.slot(id);
        if (body.contains("name") && body.contains("value")) {
            slot.values[body["name"].get<std::string>()] = body["value"];
        } else if (body.contains("values") && body["values"].is_object()) {
            slot.values = body["values"];
        } else {
            sendJson(res, 200, json{{"success", false}, {"reason", "name and value are required"}});
            return;
        }
        std::string err; app.saveConfig(err);
        sendJson(res, 200, json{{"success", err.empty()}, {"reason", err}});
    });

    svr.Get("/api/scripts", [&app](const httplib::Request&, httplib::Response& res) {
        json mods = json::array();
        for (const auto& d : app.modules.list()) {
            if (d.source == "lua") mods.push_back(json{{"id", d.id}, {"name", d.name}});
        }
        json st = json{{"message", std::to_string(app.lua ? app.lua->scriptCount() : 0) + " Lua script(s)"},
                       {"scriptCount", app.lua ? app.lua->scriptCount() : 0},
                       {"moduleCount", mods.size()},
                       {"scriptDir", app.lua ? app.lua->scriptDir() : ""}};
        sendJson(res, 200, json{{"modules", mods}, {"status", st}});
    });

    svr.Post("/api/scripts/reload", [&app](const httplib::Request&, httplib::Response& res) {
        std::string err;
        bool ok = app.lua && app.lua->reload(err);
        json st = json{{"message", std::to_string(app.lua ? app.lua->scriptCount() : 0) + " Lua script(s)"},
                       {"scriptCount", app.lua ? app.lua->scriptCount() : 0},
                       {"scriptDir", app.lua ? app.lua->scriptDir() : ""}};
        if (ok) sendJson(res, 200, json{{"ok", true}, {"status", st}});
        else sendJson(res, 200, json{{"ok", false}, {"reason", err}});
    });

    svr.Get("/api/auth/status", [](const httplib::Request&, httplib::Response& res) {
        sendJson(res, 200, json{
            {"success", true},
            {"result", json{{"verified", true}, {"ready", true}, {"verifying", false},
                            {"has_saved_card", false},
                            {"message", "欢迎，local。"},
                            {"profile", json{{"username", "local"},
                                             {"expired_time", "2099-01-01"},
                                             {"is_beta", false}}}}}});
    });
    svr.Post("/api/auth/login", [](const httplib::Request&, httplib::Response& res) {
        sendJson(res, 200, json{
            {"success", true},
            {"result", json{{"username", "local"}, {"expired_time", "2099-01-01"}, {"is_beta", false}}}});
    });
    svr.Post("/api/login", [](const httplib::Request&, httplib::Response& res) {
        sendJson(res, 200, json{{"success", true}});
    });

    svr.Post("/api/shutdown", [this](const httplib::Request&, httplib::Response& res) {
        sendJson(res, 200, json{{"ok", true}});
        Log::info("shutdown requested");
        running_ = false;
    });

    svr.Get("/api/logs", [](const httplib::Request&, httplib::Response& res) {
        json arr = json::array();
        for (const auto& l : Log::tail(300)) arr.push_back(l);
        sendJson(res, 200, json{{"lines", arr}});
    });

    svr.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr) {
        res.status = 500;
        res.set_content(R"({"success":false,"reason":"internal error"})", "application/json");
    });
}

int HttpServer::start(int port, std::string& err) {
    svr_ = new httplib::Server();
    installRoutes();
    running_ = true;

    for (int i = 0; i <= 10; ++i) {
        int p = port + i;
        if (svr_->listen("127.0.0.1", p)) {
            Log::info("web control panel: http://127.0.0.1:" + std::to_string(p));
            return p;
        }
    }
    err = "failed to bind port " + std::to_string(port) + " .. " + std::to_string(port + 10);
    running_ = false;
    return 0;
}

void HttpServer::stop() {
    if (svr_) {
        svr_->stop();
        delete svr_;
        svr_ = nullptr;
    }
    running_ = false;
}

}
