#include "httplib.h"
#include <cstdio>
#include <map>
#include <string>
#include "nlohmann/json.hpp"
#include "jwt-cpp/jwt.h"
#include <chrono>
#include <cstdlib>

using json = nlohmann::json;

bool verifyToken(const httplib::Request& req, const std::string& secret, std::string& playerOut){
    std::string header = req.get_header_value("Authorization");
    const std::string prefix = "Bearer ";
    if(header.rfind(prefix, 0) != 0) return false;
    std::string token = header.substr(prefix.size());
    try {
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify().allow_algorithm(jwt::algorithm::hs256{secret});
        verifier.verify(decoded);
        playerOut = decoded.get_subject();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

int main(){
    httplib::Server svr;
    std::map<std::string, std::string> sessions;
    sessions["42"] = "waiting";
    const char* envSecret = std::getenv("MM_SECRET");
    std::string secret = envSecret ? envSecret : "dev-secret-change-me";


    svr.Get("/health", [](const httplib::Request&, httplib::Response& res){
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    std::printf("matchmaker listening on 8080\n");
    
    svr.Post("/login", [&secret](const httplib::Request& req, httplib::Response& res){
        json body = json::parse(req.body, nullptr,false);
        if(body.is_discarded() || !body.contains("name") || !body["name"].is_string()){
            res.status = 400;
            res.set_content(json{{"error","expected JSON with a string field 'name'"}}.dump(),"application/json");
            return;
        }
        std::string name = body["name"].get<std::string>();

        auto now = std::chrono::system_clock::now();
        std::string token = jwt::create()
            .set_type("JWT")
            .set_subject(name)
            .set_issued_at(now)
            .set_expires_at(now + std::chrono::minutes(10))
            .sign(jwt::algorithm::hs256{secret});

        res.set_content(json{{"player",name},{"token",token}}.dump(),"application/json");
    });

    svr.Post("/queue", [&secret](const httplib::Request& req, httplib::Response& res){
        std::string player;
        if(!verifyToken(req, secret, player)){
            res.status = 401;
            res.set_content(json{{"error","missing or invalid token"}}.dump(), "application/json");
            return;
        }
        res.status = 202;
        res.set_content(json{{"status","queued"},{"player",player}}.dump(),"application/json");
    });

    svr.Get("/session/:id", [&sessions, &secret](const httplib::Request& req, httplib::Response& res){
        std::string id = req.path_params.at("id");
        std::string player;
        if(!verifyToken(req, secret, player)){
            res.status = 401;
            res.set_content(json{{"error","missing or invalid token"}}.dump(),"application/json");
            return;
        }
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