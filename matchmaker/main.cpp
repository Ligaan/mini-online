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
#include "control.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <memory>

using json = nlohmann::json;

struct Session{
    std::string id;
    std::vector<std::string> players;
    std::string serverAddr;
};

struct GameServer {
    std::string address;
    uint32_t capacity = 0;
    uint32_t players = 0;
    std::chrono::steady_clock::time_point lastBeat;
    std::string controlAddress;
};

class RegistryImpl final : public control::Registry::Service {
    public:
        RegistryImpl(std::mutex& m, std::map<std::string, GameServer>& s) : mtx_(m), servers_(s) {}

        grpc::Status Register(grpc::ServerContext*, const control::RegisterRequest* req, control::RegisterReply* reply) override {
            std::lock_guard<std::mutex> lock(mtx_);
            std::string id = "gs" + std::to_string(nextId_++);
            GameServer gs;
            gs.address = req->address();
            gs.capacity = req->capacity();
            gs.controlAddress = req->control_address();
            gs.lastBeat = std::chrono::steady_clock::now();
            servers_[id]= gs;
            reply->set_server_id(id);
            std::printf("game server %s registered at %s\n", id.c_str(), gs.address.c_str());
            return grpc::Status::OK;
        }

        grpc::Status Heartbeat(grpc::ServerContext*, const control::HeartbeatRequest* req, control::HeartbeatReply* reply) override {
            std::lock_guard<std::mutex> lock(mtx_);
            auto it = servers_.find(req->server_id());
            if(it == servers_.end()){
                reply->set_known(false);
                return grpc::Status::OK;
            }
            it->second.players = req->players();
            it->second.lastBeat = std::chrono::steady_clock::now();
            reply->set_known(true);
            return grpc::Status::OK;
        }

    private:
        std::mutex& mtx_;
        std::map<std::string, GameServer>& servers_;
        int nextId_ = 1;
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

bool allocateOnServer(const std::string& ctlAddr, const std::string& sid, const std::vector<std::string>& players){
    auto stub = control::GameControl::NewStub(grpc::CreateChannel(ctlAddr, grpc::InsecureChannelCredentials()));
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));

    control::AllocateRequest req;
    req.set_session_id(sid);
    for(const auto& p : players){
     req.add_players(p);
    }

    control::AllocateReply reply;
    grpc::Status st = stub->AllocateMatch(&ctx, req, &reply);
    return st.ok() && reply.accepted();
}

int main(){
    std::setvbuf(stdout,nullptr, _IOLBF, 0);
    httplib::Server svr;
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<std::string> waiting;
    std::map<std::string, Session> sessions;
    std::map<std::string, std::string> playerSession;
    int nextSession = 1;

    std::mutex serversMtx;
    std::map<std::string, GameServer> servers;
    RegistryImpl registry(serversMtx, servers);

    auto pickServer = [&](std::string& addrOut, std::string& ctlOut) -> bool {
        std::lock_guard<std::mutex> lock(serversMtx);
        auto now = std::chrono::steady_clock::now();
        const GameServer* best = nullptr;
        for(const auto& kv:servers){
            const GameServer& g = kv.second;
            if(now - g.lastBeat > std::chrono::seconds(6)) continue;
            if(g.players + 2 > g.capacity) continue;
            if(!best || g.players < best->players) best = &g;
        }
        if(!best) return false;
        addrOut = best->address;
        ctlOut = best->controlAddress;
        return true;
    };

    grpc::ServerBuilder builder;
    builder.AddListeningPort("0.0.0.0:50051", grpc::InsecureServerCredentials());
    builder.RegisterService(&registry);
    std::unique_ptr<grpc::Server> grpcServer = builder.BuildAndStart();
    std::printf("registry gRPC on 50051\n");

    const char* envSecret = std::getenv("MM_SECRET");
    std::string secret = envSecret ? envSecret : "dev-secret-change-me";

    auto matchJson = [&](const Session& s, const std::string& player){
        return json{{"status","matched"},{"session",s.id},{"server",s.serverAddr},
                    {"players",s.players},{"ticket",makeTicket(secret, player, s.id)}};
    };

    svr.Get("/health", [](const httplib::Request&, httplib::Response& res){
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    std::printf("matchmaker listening on 8080\n");

    svr.Get("/servers", [&](const httplib::Request&, httplib::Response& res){
        std::lock_guard<std::mutex> lock(serversMtx);
        auto now = std::chrono::steady_clock::now();
        json arr = json::array();
        for(const auto& kv:servers){
            long age = std::chrono::duration_cast<std::chrono::seconds>(now - kv.second.lastBeat).count();
            arr.push_back({
                    {"id", kv.first},{"address",kv.second.address},
                    {"capacity", kv.second.capacity}, {"players",kv.second.players},
                    {"last_beat_s_ago", age}, {"alive", age <= 6}, {"control_address", kv.second.controlAddress}
                });
        }
        res.set_content(arr.dump(), "application/json");
    });
    
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

        std::string serverAddr,ctlAddr;
        if(!pickServer(serverAddr, ctlAddr)){
            waiting.pop_back();
            res.status = 503;
            res.set_content(json{{"error","no game server available"}}.dump(),"application/json");
            return;
        }

        std::string sid = std::to_string(nextSession);
        if(!allocateOnServer(ctlAddr, sid, {waiting[0], waiting[1]})){
            waiting.pop_back();
            res.status = 503;
            res.set_content(json{{"error","game server did not accept the session"}}.dump(), "application/json");
            return;
        }

        Session s;
        s.id = std::to_string(nextSession++);
        s.serverAddr= serverAddr;
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

        res.set_content(json{{"id",it->second.id},{"players",it->second.players},{"server", it->second.serverAddr}}.dump(),"application/json");
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