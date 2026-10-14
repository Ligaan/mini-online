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

#include "protocol.h"

using Clock = std::chrono::steady_clock;
constexpr auto TICK = std::chrono::nanoseconds(1'000'000'000 / 30); // 30Hz

struct Sample {
    uint32_t tick;
    float x, y;
};
constexpr double INTERP_DELAY = 3.0;

int main(int argc, char** argv){
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

    InputPacket pkt{};
    float px = 0.0f, py = 0.0f;
    std::deque<InputPacket> pending;
    std::unordered_map<uint32_t, std::deque<Sample>> remotes;
    uint32_t lastSnapTick = 0;
    Clock::time_point lastRecvTime = Clock::now();

    auto nextTick = Clock::now() + TICK;
    uint64_t tickCount = 0;
    SnapshotPacket snap{};
    uint32_t lastAck = 0;

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

            const EntityState* me = nullptr;
            for (uint32_t i = 0; i < snap.count; ++i){
                if(snap.entities[i].id == snap.yourId){
                    me = &snap.entities[i];
                    break;
                }
            }
            if (snap.lastSeq < lastAck) continue;
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
                    if (!present){
                        it = remotes.erase(it);
                    }else{
                        ++it;
                    }
                }
            }

        }
        if(Clock::now() >= nextTick){
            nextTick += TICK;
            ++tickCount;
            pkt.seq = tickCount;
            pkt.dx = ((tickCount / 90) % 2 == 0) ? 1 : -1;
            pkt.dy = 0;

            double elapsed = std::chrono::duration<double>(Clock::now()-lastRecvTime).count();
            double serverNow = lastSnapTick + elapsed * 30.0;
            double renderTick = serverNow - INTERP_DELAY;

            applyInput(px, py, pkt.dx, pkt.dy);
            pending.push_back(pkt);
            if (pending.size() > 120) pending.pop_front();
            if(sendto(fd, &pkt, sizeof(pkt), 0, (sockaddr*)&addr, sizeof(addr)) < 0){
                perror("sendto");
            }

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
                if (tickCount % 30 == 0){
                    std::printf("remote id=%u raw x=%.2f | smooth x=%.2f\n", remote.first, remote.second.back().x,x);
                }
            }
        }
    }

    close(fd);
    return 0;
}
