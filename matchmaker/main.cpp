#include "httplib.h"
#include <cstdio>
#include <map>
#include <string>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

int main(){
    httplib::Server svr;
    std::map<std::string, std::string> sessions;
    sessions["42"] = "waiting";


    svr.Get("/health", [](const httplib::Request&, httplib::Response& res){
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    std::printf("matchmaker listening on 8080\n");
    
    svr.Post("/login", [](const httplib::Request& req, httplib::Response& res){
        json body = json::parse(req.body, nullptr,false);
        if(body.is_discarded() || !body.contains("name") || !body["name"].is_string()){
            res.status = 400;
            res.set_content(json{{"error","expected JSON with a string field 'name'"}}.dump(),"application/json");
            return;
        }
        std::string name = body["name"].get<std::string>();
        res.set_content(json{{"player",name}}.dump(),"application/json");
    });

    svr.Post("/queue", [](const httplib::Request& req, httplib::Response& res){
        json body = json::parse(req.body, nullptr, false);
        if(body.is_discarded() || !body.contains("name") || !body["name"].is_string()){
            res.status = 400;
            res.set_content(json{{"error","expected JSON with a string field 'name'"}}.dump(), "application/json");
            return;
        }
        std::string name = body["name"].get<std::string>();
        res.status = 202;
        res.set_content(json{{"player",name}}.dump(),"application/json");
    });

    svr.Get("/session/:id", [&sessions](const httplib::Request& req, httplib::Response& res){
        std::string id = req.path_params.at("id");

        auto it = sessions.find(id);
        if (it == sessions.end()){
            res.status = 404;
            res.set_content(json{{"error", "no such session"}}.dump(),"application/json");
            return;
        }
        res.set_content(json{{"id",id},{"status",it->second}}.dump(),"application/json");
    });

    svr.listen("0.0.0.0", 8080);
}