#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>

int socket(int domain, int type, int protocol);

int main(){
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0){
        perror("socket");
        return 1;
    }
    std::printf("Socket created, fd=%d\n",fd);
    close(fd);
    return 0;
}