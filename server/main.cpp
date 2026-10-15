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

#include "protocol.h"
#include "jwt-cpp/jwt.h"

struct ClientInfo {
    sockaddr_in addr;
    std::chrono::steady_clock::time_point lastSeen;
    float x = 0.0f, y = 0.0f;
    uint32_t lastSeq = 0;
    uint32_t id = 0;
    std::string player;
};

using Clock = std::chrono::steady_clock;
constexpr auto TICK = std::chrono::nanoseconds(1'000'000'000 / 30); // 30Hz

bool verifyTicket(const std::string& token, const std::string& secret, std::string& playerOut, std::string& sidOut){
    try{
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{secret})
            .with_audience("game-server");
        verifier.verify(decoded);
        playerOut = decoded.get_subject();
        sidOut = decoded.get_payload_claim("sid").as_string();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

int main(){
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
    
    char buf[1500];

    std::unordered_map<uint64_t, ClientInfo> clients;

    auto nextTick = Clock::now() + TICK;
    uint64_t tickCount = 0;
    uint32_t nextId = 1;

    while(true){
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

            if(n < 1) continue;

            uint8_t type = (uint8_t)buf[0];

            uint64_t key = ((uint64_t)from.sin_addr.s_addr <<16) | from.sin_port;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            auto now = Clock::now();

            if(type == PKT_JOIN) {
                if (n != sizeof(JoinPacket)) {
                    std::printf("bad join size\n");
                    continue;
                }
                JoinPacket jp;
                std::memcpy(&jp, buf, sizeof(jp));
                jp.ticket[sizeof(jp.ticket) - 1] = '\0';

                std::string player, sid;
                if (!verifyTicket(jp.ticket, secret, player, sid)) {
                    std::printf("rejected join from %s:%d\n", ip, ntohs(from.sin_port));
                    continue;
                }
                if(clients.find(key) == clients.end()){
                    std::printf("player %s joined session %s from %s:%d\n",
                                player.c_str(), sid.c_str(), ip, ntohs(from.sin_port));
                }
                auto& c = clients[key];
                if (c.id == 0) c.id = nextId++;
                c.addr = from;
                c.lastSeen = now;
                c.player = player;
            } else if(type == PKT_INPUT){
                if(n != sizeof(InputPacket)){
                    std::printf("bad input size\n");
                    continue;
                }
                auto it = clients.find(key);
                if(it == clients.end()) continue;

                InputPacket in;
                std::memcpy(&in, buf, sizeof(in));
                ClientInfo& c = it->second;
                c.lastSeen = now;
                if(in.seq <= c.lastSeq) continue;
                applyInput(c.x,c.y,in.dx,in.dy);
                c.lastSeq = in.seq;
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
            nextTick += TICK;
            ++tickCount;
            if(tickCount % 30 == 0){
                std::printf("tick %llu\n",(unsigned long long)tickCount);
            }
            
            SnapshotPacket snap{};
            snap.tick = (uint32_t)tickCount;
            for(const auto& entry: clients){
                const ClientInfo& c = entry.second;
                EntityState& e = snap.entities[snap.count++];
                e.id = c.id;
                e.x = c.x;
                e.y = c.y;
    
                if(snap.count == MAX_PLAYERS)
                    break;
            }
    
            

            for(const auto& entry: clients){
                const ClientInfo& c = entry.second;
                SnapshotPacket out = snap;
                out.lastSeq = c.lastSeq;
                out.yourId = c.id;
    
                if(sendto(fd, &out,sizeof(out), 0, (sockaddr*)&c.addr, sizeof(c.addr)) < 0){
                        perror("sendto");
                }
            }
        }
    }

    close(fd);
    return 0;
}