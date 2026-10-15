#include "httplib.h"
#include <cstdio>
#include <map>
#include <string>
#include "nlohmann/json.hpp"
#include "jwt-cpp/jwt.h"
#include <chrono>
#include <cstdlib>
#include <vector>
#include <mutex>
#include <algorithm>
#include <condition_variable>

using json = nlohmann::json;

struct Session{
    std::string id;
    std::vector<std::string> players;
};

bool verifyToken(const httplib::Request& req, const std::string& secret, std::string& playerOut){
    std::string header = req.get_header_value("Authorization");
    const std::string prefix = "Bearer ";
    if(header.rfind(prefix, 0) != 0) return false;
    std::string token = header.substr(prefix.size());
    try {
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
                                .allow_algorithm(jwt::algorithm::hs256{secret})
                                .with_audience("matchmaker");
        verifier.verify(decoded);
        playerOut = decoded.get_subject();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string makeTicket(const std::string& secret, const std::string& player, const std::string& sid){
    auto now = std::chrono::system_clock::now();
    return jwt::create()
        .set_type("JWT")
        .set_subject(player)
        .set_audience("game-server")
        .set_payload_claim("sid", jwt::claim(sid))
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::seconds(30))
        .sign(jwt::algorithm::hs256{secret});    
}

int main(){
    httplib::Server svr;
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<std::string> waiting;
    std::map<std::string, Session> sessions;
    std::map<std::string, std::string> playerSession;
    int nextSession = 1;

    const char* envSecret = std::getenv("MM_SECRET");
    std::string secret = envSecret ? envSecret : "dev-secret-change-me";
    const char* envAddr = std::getenv("MM_GAME_ADDR");
    std::string gameAddr = envAddr ? envAddr : "127.0.0.1:9000";

    auto matchJson = [&](const Session& s, const std::string& player){
        return json{{"status","matched"},{"session",s.id},{"server",gameAddr},
                    {"players",s.players},{"ticket",makeTicket(secret, player, s.id)}};
    };

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
            .set_audience("matchmaker")
            .set_issued_at(now)
            .set_expires_at(now + std::chrono::minutes(10))
            .sign(jwt::algorithm::hs256{secret});

        res.set_content(json{{"player",name},{"token",token}}.dump(),"application/json");
    });

    svr.Post("/queue", [&](const httplib::Request& req, httplib::Response& res){
        std::string player;
        if(!verifyToken(req, secret, player)){
            res.status = 401;
            res.set_content(json{{"error","missing or invalid token"}}.dump(), "application/json");
            return;
        }

        std::lock_guard<std::mutex> lock(mtx);

        if(playerSession.count(player)){
            res.status = 409;
            res.set_content(json{{"error","already in a session"}}.dump(),"application/json");
            return;
        }
        if(std::find(waiting.begin(), waiting.end(), player) != waiting.end()){
            res.status = 202;
            res.set_content(json{{"status","waiting"},{"player",player}}.dump(), "application/json");
            return;
        }
        waiting.push_back(player);
        if(waiting.size() <2){
            res.status = 202;
            res.set_content(json{{"status","waiting"},{"player",player}}.dump(), "application/json");
            return;
        }

        Session s;
        s.id = std::to_string(nextSession++);
        s.players = {waiting[0], waiting[1]};
        waiting.erase(waiting.begin(), waiting.begin() + 2);
        sessions[s.id] = s;
        for (const auto& p : s.players){
            playerSession[p] = s.id;
        }
        cv.notify_all();
        res.set_content(matchJson(s,player).dump(),"application/json");
    });

    svr.Get("/match", [&](const httplib::Request& req, httplib::Response& res){
        std::string player;
        if(!verifyToken(req,secret,player)){
            res.status = 401;
            res.set_content(json{{"error","missing or invalid token"}}.dump(),"application/json");
            return;
        }

        std::lock_guard<std::mutex> lock(mtx);

        auto ps = playerSession.find(player);
        if(ps != playerSession.end()){
            res.set_content(matchJson(sessions.at(ps->second), player).dump(), "application/json");
            return;
        }
        if(std::find(waiting.begin(),waiting.end(), player) != waiting.end()){
            res.set_content(json{{"status","waiting"}}.dump(), "application/json");
            return;
        }
        res.status = 404;
        res.set_content(json{{"error","not queued"}}.dump(), "application/json");
    });

    svr.Get("/session/:id", [&](const httplib::Request& req, httplib::Response& res){
        std::string id = req.path_params.at("id");
        std::string player;
        if(!verifyToken(req, secret, player)){
            res.status = 401;
            res.set_content(json{{"error","missing or invalid token"}}.dump(),"application/json");
            return;
        }

        std::lock_guard<std::mutex> lock(mtx);

        auto it = sessions.find(id);
        if (it == sessions.end()){
            res.status = 404;
            res.set_content(json{{"error", "no such session"}}.dump(),"application/json");
            return;
        }
        
        if(   std::find(it->second.players.begin(), it->second.players.end(), player) == it->second.players.end()){
            res.status = 403;
            res.set_content(json{{"error","not your session"}}.dump(), "application/json");
            return;
        }

        res.set_content(json{{"id",it->second.id},{"players",it->second.players}}.dump(),"application/json");
    });

    svr.WebSocket("/ws", [&](const httplib::Request& req, httplib::ws::WebSocket& ws){
        std::string player;
        if(!verifyToken(req, secret, player)){
            ws.close(httplib::ws::CloseStatus::PolicyViolation, "unauthorized");
            return;
        }

        bool alreadyMatched;
        {
            std::lock_guard<std::mutex> lock(mtx);
            alreadyMatched = playerSession.count(player) > 0;
        }

        if(!alreadyMatched){
            ws.send(json{{"status","waiting"}}.dump());
        }

        std::string msg;
        {
            std::unique_lock<std::mutex> lock(mtx);
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            while(ws.is_open() && std::chrono::steady_clock::now() < deadline){
                auto ps = playerSession.find(player);
                if(ps != playerSession.end()){
                    msg = matchJson(sessions.at(ps->second), player).dump();
                    break;
                }
                cv.wait_for(lock, std::chrono::seconds(1));
            }
        }

        if(!msg.empty()){
            ws.send(msg);
            ws.close();
        }else{
            ws.close(httplib::ws::CloseStatus::GoingAway, "no match");
        }
    });

    svr.listen("0.0.0.0", 8080);
}