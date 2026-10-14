#pragma once
#include <cstdint>

struct InputPacket {
    uint32_t seq;
    int8_t dx;
    int8_t dy;
};