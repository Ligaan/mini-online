#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdio>
#include <unordered_map>
#include <chrono>
#include <cstdint>
#include <poll.h>


struct ClientInfo {
    sockaddr_in addr;
    std::chrono::steady_clock::time_point lastSeen;
};

using Clock = std::chrono::steady_clock;
constexpr auto TICK = std::chrono::nanoseconds(1'000'000'000 / 30); // 30Hz
constexpr uint64_t SNAPSHOT_EVERY = 30;

int main(){
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
            if (n < 0){
                perror("recvfrom");
                break;
            }
            uint64_t key = ((uint64_t)from.sin_addr.s_addr << 16) | from.sin_port;
            
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from.sin_addr, ip,sizeof(ip));
            // std::printf("%zd bytes from %s:%d\n", n, ip, ntohs(from.sin_port));
            // if(sendto(fd, buf,n, 0, (sockaddr*)&from, fromLen) < 0){
            //     perror("sendto");
            // }
            auto now = std::chrono::steady_clock::now();

            
            if(clients.find(key) == clients.end()){
                std::printf("New client: %s:%d\n", ip, ntohs(from.sin_port));
            }
            
            clients[key] = ClientInfo{from, now};
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
            if(tickCount % SNAPSHOT_EVERY == 0){
                std::printf("tick %llu\n",(unsigned long long)tickCount);
                char msg[128];
                int len = std::snprintf(msg, sizeof(msg),"snapshot tick=%llu clients=%zu\n",(unsigned long long)tickCount, clients.size());
                for(auto it = clients.begin();it != clients.end(); ++it){
                    if(sendto(fd, msg,len, 0, (sockaddr*)&it->second.addr, sizeof(it->second.addr)) < 0){
                        perror("sendto");
                    }
                }
            }
        }
    }

    close(fd);
    return 0;
}