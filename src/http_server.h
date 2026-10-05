#pragma once

#include <string>

namespace httplib {
class Server;
}

namespace oy {

struct App;

class HttpServer {
public:
    explicit HttpServer(App& app) : app_(app) {}
    ~HttpServer();

    int start(int port, std::string& err);
    void stop();

private:
    void installRoutes();

    App& app_;
    httplib::Server* svr_ = nullptr;
    bool running_ = false;
};

}
