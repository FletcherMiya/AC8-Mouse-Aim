#pragma once
#include <algorithm>

namespace pw7_reference {
// These tune input demands, not the game's aircraft performance. A rate scale
// cannot make an aircraft attain a rate beyond its actual authority.
struct ResponseSettings {
    bool angle_control = true;
    float pitch_full_input_angle = 20.0f;
    float roll_full_input_angle = 45.0f;
    float turn_brake_lookahead = 0.10f;
    float roll_brake_lookahead = 0.25f;
    float angle_pitch_yaw_ratio = 5.5f;
    float angle_high_g_pitch_yaw_ratio = 11.0f;
    float roll_small_input_scale = 0.50f;
    float roll_large_input_scale = 1.35f;
    float roll_small_bank = 25.0f;
    float roll_brake_gain = 1.5f;
    float high_g_brake_lookahead = 0.30f;
    float high_g_brake_gain = 1.5f;
    float high_g_brake_hold = 0.45f;
    float high_g_pitch_priority = 1.0f;
    float high_g_brake_weight = 0; // Runtime release tail, NOT actual game high-G state.
    bool high_g_settling = false; // Final correction after fast rotation has stopped.
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
