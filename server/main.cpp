#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdio>
#include <unordered_map>
#include <chrono>
#include <cstdint>
#include <poll.h>
#include <cstring>
#include <string>
#include <cstdlib>
#include <algorithm>
#include <thread>
#include <atomic>
#include <functional>
#include <memory>
#include <csignal>
#include <mutex>
#include <map>
#include <set>

#include "protocol.h"
#include "jwt-cpp/jwt.h"
#include "game.pb.h"
#include "control.grpc.pb.h"
#include "grpcpp/grpcpp.h"

struct ClientInfo {
    sockaddr_in addr;
    std::chrono::steady_clock::time_point lastSeen;
    float x = 0.0f, y = 0.0f;
    uint32_t lastSeq = 0;
    uint32_t id = 0;
    std::string player;
    float budget = 5.0f;
    uint32_t dropped = 0;
    uint32_t inputsSec = 0;
};

using Clock = std::chrono::steady_clock;
constexpr auto TICK = std::chrono::nanoseconds(1'000'000'000 / 30); // 30Hz
constexpr float INPUT_REFILL = 1.1f;
constexpr float INPUT_BURST = 5.0f;


bool verifyTicket(const std::string& token, const std::string& secret, std::string& playerOut, std::string& sidOut, std::string& jtiOut){
    try{
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{secret})
            .with_audience("game-server");
        verifier.verify(decoded);
        playerOut = decoded.get_subject();
        sidOut = decoded.get_payload_claim("sid").as_string();
        jtiOut = decoded.get_id();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

struct AllowedSessions{
    std::mutex mtx;
    std::map<std::string,std::set<std::string>> sessions;
};

struct UsedTicket{
    uint64_t key;
    std::chrono::steady_clock::time_point expires;
};

struct Metrics{
    uint64_t pktsIn = 0, bytesIn = 0, pktsOut = 0;
    uint64_t bad = 0, rejectedJoins = 0;
    uint64_t inputsApplied = 0, inputsLimited = 0;
    double maxTickLateMs = 0;
};

class GameControlImpl final : public control::GameControl::Service {
    public:
        explicit GameControlImpl(AllowedSessions& a) : allowed_(a) {}

        grpc::Status AllocateMatch(grpc::ServerContext*, const control::AllocateRequest* req, control::AllocateReply* reply) override {
            std::lock_guard<std::mutex> lock(allowed_.mtx);
            std::set<std::string> players(req->players().begin(), req->players().end());
            allowed_.sessions[req->session_id()] = players;
            reply->set_accepted(true);
            std::printf("allocated session %s (%zu players)\n", req->session_id().c_str(),players.size());
            return grpc::Status::OK;
        }

    private:
        AllowedSessions& allowed_;
};

void registryLoop(std::string target, std::string publicAddr, std::string controlAddr,
                  std::atomic<uint32_t>& players, std::atomic<bool>& running){
    auto stub = control::Registry::NewStub(grpc::CreateChannel(target, grpc::InsecureChannelCredentials()));
    std::string serverId;

    while(running){
        grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));

        if(serverId.empty()){
            control::RegisterRequest req;
            req.set_address(publicAddr);
            req.set_capacity(MAX_PLAYERS);
            req.set_control_address(controlAddr);
            control::RegisterReply reply;
            grpc::Status st = stub->Register(&ctx, req, &reply);
            if(st.ok()){
                serverId = reply.server_id();
                std::printf("registered as %s\n", serverId.c_str());
            }else{
                std::printf("register failed: %s\n", st.error_message().c_str());
            }
        }else{
            control::HeartbeatRequest req;
            req.set_server_id(serverId);
            req.set_players(players);
            control::HeartbeatReply reply;
            grpc::Status st = stub->Heartbeat(&ctx, req, &reply);
            if(!st.ok()){
                std::printf("heartbeat failed: %s\n", st.error_message().c_str());
            }else if(!reply.known()){
                std::printf("registry forgot us, re-registering\n");
                serverId.clear();
            }
        }
        for(int i=0; i<20 && running; ++i){
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

}

volatile std::sig_atomic_t stopRequested = 0;
void onSignal(int){
    stopRequested = 1;
}

int main(){
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    const char* envSecret = std::getenv("MM_SECRET");
    std::string secret = envSecret ? envSecret : "dev-secret-change-me";

    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0){
        perror("socket");
        return 1;
    }
    std::printf("Socket created, fd=%d\n",fd);
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(9000);

    if(bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0 ){
        perror("bind");
        return 1;
    }
    
    std::printf("bound to UDP port 9000\n");
    
    const char* envGrpc = std::getenv("MM_GRPC_ADDR");
    std::string grpcTarget = envGrpc ? envGrpc : "localhost:50051";
    const char* envPub = std::getenv("GAME_PUBLIC_ADDR");
    std::string publicAddr = envPub ? envPub : "127.0.0.1:9000";

    const char* envCtl = std::getenv("GAME_CONTROL_ADDR");
    std::string controlAddr = envCtl ? envCtl : "127.0.0.1:50052";

    AllowedSessions allowed;
    GameControlImpl gameControl(allowed);
    grpc::ServerBuilder builder;
    builder.AddListeningPort("0.0.0.0:50052", grpc::InsecureServerCredentials());
    builder.RegisterService(&gameControl);
    std::unique_ptr<grpc::Server> grpcServer = builder.BuildAndStart();
    if(!grpcServer){
        std::printf("could not start control server on 50052\n");
        return 1;
    }
    std::printf("control gRPC on 50052\n");

    std::atomic<uint32_t> playerCount{0};
    std::atomic<bool> running{true};
    std::thread registry(registryLoop, grpcTarget, publicAddr, controlAddr, std::ref(playerCount), std::ref(running));

    char buf[1500];

    std::unordered_map<uint64_t, ClientInfo> clients;
    std::map<std::string, UsedTicket> usedTickets;
    Metrics m;

    auto nextTick = Clock::now() + TICK;
    uint64_t tickCount = 0;
    uint32_t nextId = 1;

    while(!stopRequested){
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        auto untilTick = std::chrono::duration_cast<std::chrono::milliseconds>(nextTick - Clock::now()).count();
        int timeoutMs = untilTick > 0 ? (int) untilTick : 0;
        int ready = poll(&pfd, 1, timeoutMs);

        if(ready < 0){
            perror("poll");
            break;
        }

        if(ready > 0){

            sockaddr_in from{};
            socklen_t fromLen = sizeof(from);
            
            ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (sockaddr*)&from, &fromLen);
            if(n < 0){
                perror("recvfrom");
                break;
            }
            ++m.pktsIn;
            m.bytesIn += (uint64_t)n;

            game::ClientMessage msg;
            if(!msg.ParseFromArray(buf, (int)n)){
                ++m.bad;
                continue;
            }

            uint64_t key = ((uint64_t)from.sin_addr.s_addr <<16) | from.sin_port;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            auto now = Clock::now();

            if(msg.has_join()){
                std::string player, sid, jti;
                if(msg.join().ticket().size() > 512){
                    ++m.rejectedJoins;
                    continue;
                }

                if(!verifyTicket(msg.join().ticket(), secret, player, sid, jti)){
                    //std::printf("rejected join from %s:%d\n", ip, ntohs(from.sin_port));
                    ++m.rejectedJoins;
                    continue;
                }


                {
                    std::lock_guard<std::mutex> lock(allowed.mtx);
                    auto s = allowed.sessions.find(sid);
                    if(s == allowed.sessions.end() || !s->second.count(player)){
                        //std::printf("rejected join: %s is not allocated to session %s\n",player.c_str(), sid.c_str());
                        ++m.rejectedJoins;
                        continue;
                    }
                }
                bool duplicate = false;
                for(const auto& entry:clients){
                    if(entry.second.player == player && entry.first != key){
                        duplicate = true;
                        break;
                    }
                }
                if(duplicate){
                    //std::printf("rejected join: %s already connected from another address\n", player.c_str());
                    ++m.rejectedJoins;
                    continue;
                }
                auto ut = usedTickets.find(jti);
                if(ut != usedTickets.end() && ut->second.key != key){
                    //std::printf("rejected join: ticket already used from another address\n");
                    ++m.rejectedJoins;
                    continue;
                }
                if(ut == usedTickets.end()){
                    usedTickets[jti] = UsedTicket{key, now + std::chrono::seconds(60)};
                }
                if(clients.find(key) == clients.end()){
                    std::printf("player %s joined session %s from %s:%d\n", 
                    player.c_str(), sid.c_str(), ip, ntohs(from.sin_port));
                }
                auto& c = clients[key];
                if(c.id == 0) c.id = nextId++;
                c.addr = from;
                c.lastSeen = now;
                c.player = player;
            }
            else if(msg.has_input()){
                auto it = clients.find(key);
                if(it == clients.end()) continue;

                const game::Input& in = msg.input();
                ClientInfo& c = it->second;
                c.lastSeen = now;
                if(in.seq() <= c.lastSeq) continue;

                if(c.budget < 1.0f){
                    ++c.dropped;
                    ++m.inputsLimited;
                    continue;
                }
                c.budget -= 1.0f;
                

                int dx = std::clamp(in.dx(), -1, 1);
                int dy = std::clamp(in.dy(), -1, 1);
                applyInput(c.x,c.y,(int8_t)dx,(int8_t)dy);
                ++m.inputsApplied;
                ++c.inputsSec;
                c.lastSeq = in.seq();
            }
        }

        auto nowSweep = std::chrono::steady_clock::now();

        for(auto it = clients.begin();it != clients.end();){
            if(nowSweep - it->second.lastSeen > std::chrono::seconds(10)){
                std::printf("client timed out\n");
                it = clients.erase(it);
            }else{
                ++it;
            }
        }

        
        if(Clock::now() >= nextTick){
            double lateMs = std::chrono::duration<double, std::milli>(Clock::now() - nextTick).count();
            if(lateMs > m.maxTickLateMs) m.maxTickLateMs = lateMs;

            nextTick += TICK;
            ++tickCount;
            playerCount =static_cast<uint32_t>(clients.size());

            for(auto& entry:clients){
                ClientInfo& c = entry.second;
                c.budget = std::min(c.budget + INPUT_REFILL, INPUT_BURST);
            }

            if(tickCount % 30 == 0){
                auto nowT = Clock::now();
                for(auto it = usedTickets.begin();it != usedTickets.end();){
                    if(it->second.expires < nowT){
                        it = usedTickets.erase(it);
                    }else{
                        ++it;
                    }
                }
                //std::printf("tick %llu\n",(unsigned long long)tickCount);
                std::printf("stats: players=%zu in=%llu pkt/s (%llu B/s) out=%llu bad=%llu rejected_joins=%llu inputs_ok=%llu rate_limited=%llu tickets=%zu max_tick_late=%.1fms\n",
                        clients.size(),
                        (unsigned long long)m.pktsIn, (unsigned long long)m.bytesIn,
                        (unsigned long long)m.pktsOut, (unsigned long long)m.bad,
                        (unsigned long long)m.rejectedJoins, (unsigned long long)m.inputsApplied,
                        (unsigned long long)m.inputsLimited, usedTickets.size(), m.maxTickLateMs);
                for(auto& entry : clients){
                    std::printf("  %s inputs/s=%u\n", entry.second.player.c_str(), entry.second.inputsSec);
                    entry.second.inputsSec = 0;
                    entry.second.dropped = 0;
                }
                m = Metrics{};
            }
            

            game::Snapshot snap;
            snap.set_tick((uint32_t)tickCount);
            for(const auto& entry:clients){
                const ClientInfo& c = entry.second;
                if(snap.entities_size()>=(int)MAX_PLAYERS) break;
                game::Entity* e = snap.add_entities();
                e->set_id(c.id);
                e->set_x(c.x);
                e->set_y(c.y);
            }

            for(const auto& entry:clients){
                const ClientInfo& c = entry.second;
                snap.set_last_seq(c.lastSeq);
                snap.set_your_id(c.id);
                std::string bytes = snap.SerializeAsString();
                if(sendto(fd, bytes.data(), bytes.size(), 0, (sockaddr*)&c.addr, sizeof(c.addr)) < 0){
                    perror("sendto");
                }else{
                    ++m.pktsOut;
                }
            }
        }
    }
    running = false;
    registry.join();
    grpcServer->Shutdown();
    
    close(fd);
    return 0;
}