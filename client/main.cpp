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
    StatePacket st{};
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
            if(n != sizeof(StatePacket)){
                std::printf("Bad Packet\n");
                continue;
            }
            memcpy(&st, reply, sizeof(st));
            if (st.lastSeq < lastAck) continue;
            lastAck = st.lastSeq;
            while(!pending.empty() && pending.front().seq <= st.lastSeq){
                pending.pop_front();
            }
            float oldX = px;
            px = st.x;
            py = st.y;
            for (const auto& p: pending){
                applyInput(px, py, p.dx, p.dy);
            }
            float err = oldX - px;
            if (fabsf(err)>0.001f){
                std::printf("correction dx=%.3f\n",err);
            }

            if(st.tick % 30 == 0){
                std::printf("server x=%.2f ack=%u | predicted x=%.2f seq=%u\n", st.x, st.lastSeq, px, (uint32_t)tickCount);
            }

        }
        if(Clock::now() >= nextTick){
            nextTick += TICK;
            ++tickCount;
            pkt.seq = tickCount;
            pkt.dx = 1;
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
