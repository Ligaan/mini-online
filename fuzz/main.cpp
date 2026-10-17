#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include "game.pb.h"

static std::mt19937_64 rng;

static std::string randomBytes(size_t n){
    std::string s(n, '\0');
    for(auto& c:s){
        c = (char)(rng() & 0xFF);
    }
    return s;
}

static std::string garbage(){
    return randomBytes(rng() % 2001);
}

static std::string truncatedInput(){
    game::ClientMessage m;
    game::Input* in = m.mutable_input();
    in->set_seq((uint32_t)rng());
    in->set_dx(1);
    in->set_dy(0);
    std::string b = m.SerializeAsString();
    b.resize(rng() % (b.size() + 1));
    if(rng() % 2){
        b += randomBytes(rng() % 64);
    }
    return b;
}

static std::string jwtShaped(){
    return garbage();
}

int main(int argc, char** argv){
    if(argc < 2){
        std::printf("usage: fuzz <host> [port=9000] [seed=random] [count=10000] [delay_us=100]\n");
        return 1;
    }
    const char* host = argv[1];
    int port = argc > 2 ? std::atoi(argv[2]) : 9000;
    uint64_t seed = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : std::random_device{}();
    int count = argc > 4 ? std::atoi(argv[4]) : 100000;
    int delayUs = argc > 5 ? std::atoi(argv[5]) : 100;
    rng.seed(seed);
    std::printf("seed=%llu target=%s:%d packets=%d delay=%dus\n", (unsigned long long)seed, host, port, count, delayUs);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(port);
    inet_pton(AF_INET, host, &dst.sin_addr);

    for(int i=0; i<count; ++i){
        std::string pkt;
        switch(rng() % 3){
            case 0:{ 
                pkt = garbage();
                break;
            }
            case 1:{
                pkt = truncatedInput();
                break;
            }
            default:{
                pkt = jwtShaped();
                break;
            }
        }
        sendto(fd, pkt.data(), pkt.size(), 0, (sockaddr*)&dst, sizeof(dst));
        if(delayUs > 0) usleep(delayUs);
        if(i % 20000 == 0) std::printf("sent %d\n", i);
    }
    std::printf("done, sent %d packets\n", count);
    close(fd);
    return 0;
}
