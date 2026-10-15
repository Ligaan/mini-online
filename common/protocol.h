#pragma once
#include <cstdint>

enum : uint8_t {
    PKT_JOIN = 1,
    PKT_INPUT = 2
};

struct JoinPacket{
    uint8_t type;
    char ticket[512];
};

struct InputPacket {
    uint8_t type;
    uint32_t seq;
    int8_t dx;
    int8_t dy;
};

constexpr float SPEED = 5.0f;
constexpr float DT = 1.0f / 30.0f;
constexpr uint32_t MAX_PLAYERS = 8;

struct EntityState{
    uint32_t id;
    float x;
    float y;
};

struct SnapshotPacket {
    uint32_t tick;
    uint32_t lastSeq;
    uint32_t yourId;
    uint32_t count;
    EntityState entities[MAX_PLAYERS];
};

inline void applyInput(float& x, float& y, int8_t dx, int8_t dy) {
    x += dx * SPEED * DT;
    y += dy * SPEED * DT;
}