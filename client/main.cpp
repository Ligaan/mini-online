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

#include "protocol.h"

using Clock = std::chrono::steady_clock;
constexpr auto TICK = std::chrono::nanoseconds(1'000'000'000 / 30); // 30Hz


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
            px = me->x;
            py = me->y;
            for (const auto& p: pending){
                applyInput(px, py, p.dx, p.dy);
            }
            float err = oldX - px;
            if (fabsf(err)>0.001f){
                std::printf("correction dx=%.3f\n",err);
            }

            if(snap.tick % 30 == 0){
                for(uint32_t i = 0; i < snap.count; ++i){
                    if(snap.entities[i].id != snap.yourId){
                        std::printf("remote id=%u x=%.2f\n", snap.entities[i].id,snap.entities[i].x);
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
            applyInput(px, py, pkt.dx, pkt.dy);
            pending.push_back(pkt);
            if(sendto(fd, &pkt, sizeof(pkt), 0, (sockaddr*)&addr, sizeof(addr)) < 0){
                perror("sendto");
            }
        }
    }

    close(fd);
    return 0;
}
