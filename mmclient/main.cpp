#include "httplib.h"
#include "nlohmann/json.hpp"
#include <cstdio>
#include <string>

using json = nlohmann::json;

int main(int argc, char** argv){
    if(argc < 2){
        std::printf("usage: mmclient <name> [host]\n");
        return 1;
    }
    std::string name = argv[1];
    std::string host = argc > 2 ? argv[2] : "localhost";

    httplib::Client http(host, 8080);

    auto login = http.Post("/login", json{{"name",name}}.dump(), "application/json");
    if(!login || login->status != 200){
        std::printf("login failed\n");
        return 1;
    }
    std::string token = json::parse(login->body)["token"].get<std::string>();
    httplib::Headers auth = {{"Authorization", "Bearer "+token}};

    httplib::ws::WebSocketClient ws("ws://" + host + ":8080/ws", auth);
    if(!ws.connect()){
        std::printf("websocket connect failed\n");
        return 1;
    }
    std::printf("%s connected, waiting for a match...\n", name.c_str());

    auto q = http.Post("/queue", auth, "", "application/json");
    std::printf("queue: %d\n", q ? q->status : -1);

    std::string msg;
    while(ws.read(msg)){
        std::printf("message: %s\n", msg.c_str());
    }
    std::printf("connection closed\n");
    return 0;
}