#include "httplib.h"
#include <cstdio>

int main(){
    httplib::Server svr;

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
    svr.listen("0.0.0.0", 8080);
}