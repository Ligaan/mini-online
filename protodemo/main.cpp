#include "game.pb.h"
#include <cstdio>
#include <string>

int main(){
    game::Input in;
    game::Join inJoin;
    in.set_seq(1500);
    in.set_dx(1);
    in.set_dy(0);
    inJoin.set_ticket("1");

    std::string bytes,joinBytes;
    in.SerializeToString(&bytes);
    inJoin.SerializeToString(&joinBytes);

    std::printf("size: %zu bytes\n", bytes.size());
    for(unsigned char c: bytes){
        std::printf("%02x ", c);
    }
    std::printf("\nsize: %zu bytes\n", joinBytes.size());
    for(unsigned char c:joinBytes){
        std::printf("%02x ", c);
    }

    std::printf("\n");

    game::Input out;
    game::Join outJoin;
    if(!out.ParseFromString(bytes)){
        std::printf("parse failed\n");
        return 1;
    }
    std::printf("seq=%u dx=%d dy=%d\n", out.seq(),out.dx(),out.dy());
    if(!outJoin.ParseFromString(joinBytes)){
        std::printf("parse failed\n");
        return 1;
    }
    std::printf("ticket=%s\n",outJoin.ticket().c_str());
}