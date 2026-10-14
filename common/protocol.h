#pragma once
#include <cstdint>

struct InputPacket {
    uint32_t seq;
    int8_t dx;
    int8_t dy;
};

constexpr float SPEED = 5.0f;
constexpr float DT = 1.0f / 30.0f;

struct StatePacket {
    uint32_t tick;
    uint32_t lastSeq;
    float x;
    float y;
};

inline void applyInput(float& x, float& y, int8_t dx, int8_t dy) {
    x += dx * SPEED * DT;
    y += dy * SPEED * DT;
}