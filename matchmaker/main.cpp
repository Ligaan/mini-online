#include "httplib.h"
#include <cstdio>
#include <map>
#include <string>

int main(){
    httplib::Server svr;
    std::map<std::string, std::string> sessions;
    sessions["42"] = "waiting";


    svr.Get("/health", [](const httplib::Request&, httplib::Response& res){
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    std::printf("matchmaker listening on 8080\n");
    
    svr.Post("/login", [](const httplib::Request& req, httplib::Response& res){
        if(req.body.empty()){
            res.status = 400;
            res.set_content("{\"error\":\"empty body\"}","application/json");
            return;
        }
        res.set_content(req.body,"application/json");
    });

    svr.Post("/queue", [](const httplib::Request& req, httplib::Response& res){
        if(req.body.empty()){
            res.status = 400;
            res.set_content("{\"error\":\"empty body\"}", "application/json");
            return;
        }
        res.status = 202;
        res.set_content("{\"status\":\"queued\"}","application/json");
    });

    svr.Get("/session/:id", [&sessions](const httplib::Request& req, httplib::Response& res){
        std::string id = req.path_params.at("id");

        auto it = sessions.find(id);
        if (it == sessions.end()){
            res.status = 404;
            res.set_content("{\"error\":\"no such session\"}","application/json");
            return;
        }
        res.set_content("{\"id\":\"" + id + "\",\"status\":\"" + it-> second + "\"}","application/json");
    });

    svr.listen("0.0.0.0", 8080);
}