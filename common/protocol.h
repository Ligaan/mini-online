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
constexpr float WORLD_LIMIT = 50.0f;
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
    float scale = (dx != 0 && dy != 0) ? 0.70710678f : 1.0f;
    x += dx * SPEED * DT * scale;
    y += dy * SPEED * DT * scale;
    if(x > WORLD_LIMIT) x = WORLD_LIMIT;
    if(x < -WORLD_LIMIT) x = -WORLD_LIMIT;
    if(y > WORLD_LIMIT) y = WORLD_LIMIT;
    if(y < -WORLD_LIMIT) y = -WORLD_LIMIT;
}