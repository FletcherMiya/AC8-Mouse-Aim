#pragma once
#include <algorithm>

namespace flight {
// These tune input demands, not the game's aircraft performance. A rate scale
// cannot make an aircraft attain a rate beyond its actual authority.
struct ResponseSettings {
    float turn_rate_scale = 1.20f;
    float roll_rate_scale = 1.25f;
    float response_gain = 1.20f;
    float countersteer_gain = 2.0f;
    float roll_lookahead = 0.10f;
    bool high_g_enabled = true;
    float high_g_pitch_rate = 110.0f;
    bool rear_turn_hold = true;
    bool dive_pitch_boost = true;
    bool high_g_yaw_boost = false; // Opt-in, isolated in-game comparison.
    bool high_g_requested = false; // Runtime key observation, not a game-state flag.
};
inline float pitch_rate_limit(const ResponseSettings& cfg) {
    return cfg.high_g_requested ? std::max(45*cfg.turn_rate_scale,cfg.high_g_pitch_rate) : 45*cfg.turn_rate_scale;
}
}
