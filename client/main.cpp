#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <chrono>
#include <cstdint>
#include <deque>
#include <cmath>
#include <unordered_map>
#include <string>

#include "protocol.h"
#include "game.pb.h"

using Clock = std::chrono::steady_clock;
constexpr auto TICK = std::chrono::nanoseconds(1'000'000'000 / 30); // 30Hz

struct Sample {
    uint32_t tick;
    float x, y;
};
constexpr double INTERP_DELAY = 3.0;

int main(int argc, char** argv){

    if(argc < 3){
        std::printf("usage: client <server-ip> <ticket>\n");
        return 1;
    }
    game::ClientMessage joinMsg;
    joinMsg.mutable_join()->set_ticket(argv[2]);
    std::string joinBytes = joinMsg.SerializeAsString();
    bool joined = false;
    InputPacket pkt{};
    pkt.type = PKT_INPUT;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0){
        perror("socket");
        return 1;
    }
    std::printf("Socket created, fd=%d\n",fd);
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9000);  
    const char* serverIp = argc > 1 ? argv[1] : "127.0.0.1";
    inet_pton(AF_INET, serverIp, &addr.sin_addr);

    float px = 0.0f, py = 0.0f;
    std::deque<InputPacket> pending;
    std::unordered_map<uint32_t, std::deque<Sample>> remotes;
    uint32_t lastSnapTick = 0;
    Clock::time_point lastRecvTime = Clock::now();

    auto nextTick = Clock::now() + TICK;
    uint64_t tickCount = 0;
    SnapshotPacket snap{};
    uint32_t lastAck = 0;
    uint32_t snapsRecv = 0, corrections = 0, dropsOld = 0;
    float maxErr = 0.0f;
    std::unordered_map<uint32_t, float> prevSmooth;
    float maxStep = 0.0f;
    uint32_t stalls = 0, jumps = 0;

    while (tickCount < 1500){
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
            char reply[1500];
            ssize_t n = recvfrom(fd, reply, sizeof(reply), 0, nullptr, nullptr);
            if (n < 0){
                perror("recvfrom");
            }
            if(n != sizeof(SnapshotPacket)){
                std::printf("Bad Packet\n");
                continue;
            }
            
            memcpy(&snap, reply, sizeof(snap));

            if (snap.count > MAX_PLAYERS)
                continue;

            joined = true;
            snapsRecv++;

            const EntityState* me = nullptr;
            for (uint32_t i = 0; i < snap.count; ++i){
                if(snap.entities[i].id == snap.yourId){
                    me = &snap.entities[i];
                    break;
                }
            }
            if (snap.lastSeq < lastAck){
                dropsOld++;
                continue;
            }
            lastAck = snap.lastSeq;
            while(!pending.empty() && pending.front().seq <= snap.lastSeq){
                pending.pop_front();
            }
            float oldX = px;

            if (!me) continue;

            px = me->x;
            py = me->y;
            for (const auto& p: pending){
                applyInput(px, py, p.dx, p.dy);
            }
            float err = oldX - px;
            if (fabsf(err)>0.001f){
                corrections++;
                maxErr = fmaxf(maxErr,fabsf(err));
                std::printf("correction dx=%.3f\n",err);
            }

            lastSnapTick = snap.tick;
            lastRecvTime = Clock::now();

            for (uint32_t i = 0; i < snap.count; ++i){
                uint32_t& id = snap.entities[i].id;
                if(id != snap.yourId){
                    Sample sp{};
                    sp.tick = snap.tick;
                    sp.x = snap.entities[i].x;
                    sp.y = snap.entities[i].y;
                    if (!remotes[id].empty() && snap.tick <= remotes[id].back().tick)
                        continue;
                    remotes[id].push_back(sp);
                    if (remotes[id].size()>32)
                        remotes[id].pop_front();
                }
            }

            for (auto it = remotes.begin(); it != remotes.end();){
                bool present = false;
                for (uint32_t i = 0; i < snap.count; ++i){
                    if (snap.entities[i].id == it->first){
                        present = true;
                        break;
                    }
                }
                if (!present){
                    it = remotes.erase(it);
                }else{
                    ++it;
                }
            }

        }
        if(Clock::now() >= nextTick){
            nextTick += TICK;
            ++tickCount;

            if(!joined){
                if(sendto(fd,joinBytes.data(),joinBytes.size(),0,(sockaddr*)&addr, sizeof(addr))<0)
                    perror("sendto");
            }else{

                pkt.seq = tickCount;
                pkt.dx = ((tickCount / 90) % 2 == 0) ? 1 : -1;
                pkt.dy = 0;
                
                applyInput(px, py, pkt.dx, pkt.dy);
                pending.push_back(pkt);
                if (pending.size() > 120) pending.pop_front();

                game::ClientMessage msg;
                game::Input* in = msg.mutable_input();
                in->set_seq(pkt.seq);
                in->set_dx(pkt.dx);
                in->set_dy(pkt.dy);
                std::string bytes = msg.SerializeAsString();
                if(sendto(fd, bytes.data(),bytes.size(), 0, (sockaddr*)&addr, sizeof(addr)) < 0){
                    perror("senddto");
                }
            }
            double elapsed = std::chrono::duration<double>(Clock::now()-lastRecvTime).count();
            double serverNow = lastSnapTick + elapsed * 30.0;
            double renderTick = serverNow - INTERP_DELAY;

            for (auto& remote:remotes){
                if (remote.second.empty()) continue;

                while(remote.second.size()>=2 && remote.second[1].tick <= renderTick){
                    remote.second.pop_front();
                }
                float x = remote.second.front().x;
                if (remote.second.size()>=2){
                    const Sample& a = remote.second[0];
                    const Sample& b = remote.second[1];
                    double t = (renderTick - a.tick) / (double)(b.tick - a.tick);
                    t = std::fmax(0.0, std::fmin(1.0,t));
                    x = a.x + (b.x - a.x) * (float)t;
                }
                auto pit = prevSmooth.find(remote.first);
                if (pit != prevSmooth.end()){
                    float step = fabsf(x - pit->second);
                    maxStep = fmaxf(maxStep, step);
                    if (step < 0.02f) ++stalls;
                    if (step > 0.30f) ++jumps;
                }
                prevSmooth[remote.first] = x;
                if (tickCount % 30 == 0){
                    std::printf("remote id=%u raw x=%.2f | smooth x=%.2f\n", remote.first, remote.second.back().x,x);
                }
            }
            if (tickCount % 30 == 0){
                std::printf("stats: snaps/s=%u pending=%zu corrections=%u maxErr=%.3f stale=%u maxStep=%.3f stalls=%u jumps=%u\n",
                    snapsRecv, pending.size(), corrections, maxErr, dropsOld, maxStep, stalls, jumps);
                snapsRecv = corrections = dropsOld = 0;
                maxErr = 0.0f;
                maxStep = 0.0f;
                stalls = jumps = 0;
            }
        }
    }

    close(fd);
    return 0;
}
