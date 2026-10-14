#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <chrono>
#include <cstdint>

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
    auto nextTick = Clock::now() + TICK;
    uint64_t tickCount = 0;

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
            if(n > 0){
                std::printf("reply: %.*s\n", (int)n, reply);
            }

        }
        if(Clock::now() >= nextTick){
            nextTick += TICK;
            ++tickCount;
            pkt.seq = tickCount;
            pkt.dx = 1;
            pkt.dy = 0;
            if(sendto(fd, &pkt, sizeof(pkt), 0, (sockaddr*)&addr, sizeof(addr)) < 0){
                perror("sendto");
            }

        }
    }

    close(fd);
    return 0;
}
