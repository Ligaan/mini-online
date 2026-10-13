#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdio>
#include <unordered_map>
#include <chrono>
#include <cstdint>

struct ClientInfo {
    sockaddr_in addr;
    std::chrono::steady_clock::time_point lastSeen;
};

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
    while(true){
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
        std::printf("%zd bytes from %s:%d\n", n, ip, ntohs(from.sin_port));
        if(sendto(fd, buf,n, 0, (sockaddr*)&from, fromLen) < 0){
            perror("sendto");
        }
        auto now = std::chrono::steady_clock::now();

        if(clients.find(key) == clients.end()){
            std::printf("New client: %s:%d\n", ip, ntohs(from.sin_port));
        }

        clients[key] = ClientInfo{from, now};
    }

    close(fd);
    return 0;
}