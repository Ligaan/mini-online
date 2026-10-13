#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>


int main(){
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0){
        perror("socket");
        return 1;
    }
    std::printf("Socket created, fd=%d\n",fd);
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9000);  
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    const char* buf= "Hello Server";
    if(sendto(fd, buf,std::strlen(buf), 0, (sockaddr*)&addr, sizeof(addr)) < 0){
        perror("sendto");
    }
    std::printf("sent\n");

    char reply[1500];
    ssize_t n = recvfrom(fd, reply, sizeof(reply), 0, nullptr, nullptr);
    if (n < 0){
        perror("recvfrom");
    }
    std::printf("reply: %.*s\n", (int)n, reply);
    close(fd);
    return 0;
}
