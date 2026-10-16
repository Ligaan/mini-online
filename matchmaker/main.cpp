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
#include "hiredis/hiredis.h"

using json = nlohmann::json;

struct Session{
    std::string id;
    std::vector<std::string> players;
    std::string serverAddr;
};

using Reply = std::unique_ptr<redisReply, decltype(&freeReplyObject)>;

class Store {
    public:
        ~Store(){
            if(ctx_)
            redisFree(ctx_);
        }

        bool connect(const std::string& host, int port){
            ctx_ = redisConnect(host.c_str(), port);
            if(!ctx_ || ctx_->err){
                std::printf("redis connect failed: %s\n",ctx_ ? ctx_->errstr : "out of memory");
                return false;
            }
            return true;
        }

        long nextSessionId(){
            std::lock_guard<std::mutex> lock(m_);
            if(!ensure()) return -1;
            Reply r(static_cast<redisReply*>(redisCommand(ctx_, "INCR mm:next_session")), freeReplyObject);
            return (r && r->type == REDIS_REPLY_INTEGER) ? (long)r->integer : -1;
        }

        bool saveSession(const Session& s){
            std::lock_guard<std::mutex> lock(m_);
            if(!ensure()) return false;
            std::string body = json{{"id",s.id}, {"players", s.players}, {"server", s.serverAddr}}.dump();
            Reply r(static_cast<redisReply*>(redisCommand(ctx_, "SET mm:session:%s %s EX %d", s.id.c_str(), body.c_str(), kTtl)), freeReplyObject);
            if(!ok(r)) return false;
            for(const auto& p : s.players){
                Reply r2(static_cast<redisReply*>(redisCommand(ctx_, "SET mm:player:%s %s EX %d", p.c_str(), s.id.c_str(), kTtl)), freeReplyObject);
                if(!ok(r2)) return false;
            }
            return true;
        }

        bool loadSession(const std::string& id, Session& out){
            std::string body;
            if(!getString("mm:session:" + id, body)) return false;
            json j = json::parse(body, nullptr, false);
            if(j.is_discarded()) return false;
            out.id = j.value("id", "");
            out.players = j.value("players", std::vector<std::string>{});
            out.serverAddr = j.value("server", "");
            return true;
        }

        bool sessionOf(const std::string& player, std::string& idOut){
            return getString("mm:player:" + player, idOut);
        }
    private:
        static constexpr int kTtl = 3600;

        bool ensure(){
            if(!ctx_) return false;
            if(ctx_->err) return redisReconnect(ctx_) == REDIS_OK;
            return true;
        }

        static bool ok(const Reply& r){
             return r && r->type != REDIS_REPLY_ERROR;
        }

        bool getString(const std::string& key, std::string& out){
            std::lock_guard<std::mutex> lock(m_);
            if(!ensure()) return false;
            Reply r(static_cast<redisReply*>(redisCommand(ctx_, "GET %s", key.c_str())), freeReplyObject);
            if(!r || r->type != REDIS_REPLY_STRING) return false;
            out.assign(r->str, r->len);
            return true;
        }

        redisContext* ctx_ = nullptr;
        std::mutex m_;
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
    Store store;
    const char* envRedis = std::getenv("REDIS_HOST");
    if(!store.connect(envRedis ? envRedis : "127.0.0.1", 6379)) return 1;

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
        std::string existing;
        if(store.sessionOf(player,existing)){
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

        long n = store.nextSessionId();
        if(n<0){
            waiting.pop_back();
            res.status = 503;
            res.set_content(json{{"error","state store unavailable"}}.dump(),"application/json");
            return;
        }

        std::string sid = std::to_string(n);
        if(!allocateOnServer(ctlAddr, sid, {waiting[0], waiting[1]})){
            waiting.pop_back();
            res.status = 503;
            res.set_content(json{{"error", "game server did not accept the session"}}.dump(), "application/json");
            return;
        }

        Session s;
        s.id = sid;
        s.serverAddr = serverAddr;
        s.players = {waiting[0], waiting[1]};
        if(!store.saveSession(s)){
            waiting.pop_back();
            res.status = 503;
            res.set_content(json{{"error", "state store unavailable"}}.dump(), "application/json");
            return;
        }
        waiting.erase(waiting.begin(), waiting.begin()+2);
        cv.notify_all();
        res.set_content(matchJson(s, player).dump(), "application/json");
    });

    svr.Get("/match", [&](const httplib::Request& req, httplib::Response& res){
        std::string player;
        if(!verifyToken(req,secret,player)){
            res.status = 401;
            res.set_content(json{{"error","missing or invalid token"}}.dump(),"application/json");
            return;
        }

        std::lock_guard<std::mutex> lock(mtx);

        std::string sid;
        if(store.sessionOf(player, sid)){
            Session s;
            if(store.loadSession(sid,s)){
                res.set_content(matchJson(s, player).dump(), "application/json");
                return;
            }
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

        Session s;
        if (!store.loadSession(id,s)){
            res.status = 404;
            res.set_content(json{{"error", "no such session"}}.dump(),"application/json");
            return;
        }
        
        if(   std::find(s.players.begin(), s.players.end(), player) == s.players.end()){
            res.status = 403;
            res.set_content(json{{"error","not your session"}}.dump(), "application/json");
            return;
        }

        res.set_content(json{{"id",s.id},{"players",s.players},{"server", s.serverAddr}}.dump(),"application/json");
    });

    svr.WebSocket("/ws", [&](const httplib::Request& req, httplib::ws::WebSocket& ws){
        std::string player;
        if(!verifyToken(req, secret, player)){
            ws.close(httplib::ws::CloseStatus::PolicyViolation, "unauthorized");
            return;
        }

        std::string tmp;
        bool alreadyMatched;
        {
            std::lock_guard<std::mutex> lock(mtx);
            alreadyMatched = store.sessionOf(player,tmp);
        }

        if(!alreadyMatched){
            ws.send(json{{"status","waiting"}}.dump());
        }

        std::string msg;
        {
            std::unique_lock<std::mutex> lock(mtx);
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            while(ws.is_open() && std::chrono::steady_clock::now() < deadline){
                std::string sid;
                if(store.sessionOf(player, sid)){
                    Session s;
                    if(store.loadSession(sid,s)){
                        msg = matchJson(s, player).dump();
                        break;
                    }
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