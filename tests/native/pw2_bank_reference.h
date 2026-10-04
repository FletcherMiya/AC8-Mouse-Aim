// Frozen pw.2 baseline, from ac8-mouse-aim/0.2.30-pw.2; do not tune.
#pragma once
#include "flight_math.h"

namespace pw2_reference {
using namespace flight;
inline float wrap_angle(float value) {
    return std::remainder(value, 360.0f);
}
struct BankSettings {
    float max_bank = 85;
    bool allow_dive_inversion = true;
    float dive_enter_pitch = -45;
    float dive_enter_delta = 25;
    float dive_exit_pitch = -30;
};
struct BankDemand {
    float current{}, target{}, error{}, blend{};
    bool diving{};
};

// The attitude setpoint is measured against the horizon, not against the
// aircraft's moving up vector. Only an explicit steep descent may invert it.
class BankGuidance {
    V reference_right{};
    bool reference_valid = false;
    bool diving = false, recovering = false;
    float dive_sign = 1, recovery_bank = 0;
    LevelBlend leveling;
public:
    void reset() { *this = BankGuidance{}; }
    BankDemand step(const Basis& b, V aim, float angle, float dt, const BankSettings& cfg) {
        const V world_up{0,0,1};
        V horizon_right = cross(world_up, b.f);
        V transported = reference_right - b.f * dot(reference_right, b.f);
        if (!reference_valid || dot(transported, transported) < 1e-6f) {
            transported = dot(horizon_right,horizon_right) > 0.03f ? horizon_right : b.r;
        }
        reference_right = unit(transported);
        if (dot(horizon_right,horizon_right) > 0.03f) {
            horizon_right = unit(horizon_right);
            // Slew the reference when leaving a vertical maneuver; do not
            // introduce a 180-degree attitude jump at the Euler pole.
            const float change = std::atan2(dot(cross(reference_right,horizon_right),b.f),
                                             dot(reference_right,horizon_right));
            reference_right = rotate(reference_right, b.f,
                std::clamp(change, -120*rad*dt, 120*rad*dt));
        }
        reference_valid = true;
        const V reference_up = unit(cross(b.f,reference_right));
        float bank = std::atan2(dot(b.u,reference_right),dot(b.u,reference_up))/rad;
        const float aim_pitch = pitch(aim);
        if (diving && (!cfg.allow_dive_inversion || aim_pitch >= cfg.dive_exit_pitch || angle <= 3)) {
            diving = false;
        } else if (!diving && cfg.allow_dive_inversion && angle > 3 &&
                   aim_pitch <= cfg.dive_enter_pitch && pitch(b.f)-aim_pitch >= cfg.dive_enter_delta) {
            diving = true;
            const float side = dot(aim,reference_right);
            dive_sign = std::abs(side) > 0.02f ? std::copysign(1.0f,side) :
                        (std::abs(bank) > 1 ? std::copysign(1.0f,bank) : 1.0f);
        }
        const float blend = leveling.step(angle, dt);
        const float right = dot(aim,reference_right), up = dot(aim,reference_up);
        float target;
        if (diving) {
            // A committed flip keeps its direction even when a nearly straight
            // down target fluctuates across the +/-180-degree atan2 boundary.
            target = dive_sign * std::abs(std::atan2(right,up)/rad);
            recovering = false;
            if (bank*dive_sign < -90) bank += dive_sign*360;
        } else {
            // Retain the small-turn softening; large level turns may bank
            // closer to side-on under the new cap, without demanding inversion.
            target = std::clamp(std::atan2(right,std::max(0.12f,up))/rad,
                                -cfg.max_bank,cfg.max_bank)*blend;
            // Return through upright instead of choosing a shorter route that
            // continues a roll across the inverted attitude. Retain that route
            // if inertia carries the measured angle across +/-180 degrees.
            if (recovering) bank = recovery_bank + wrap_angle(bank-recovery_bank);
            recovering = std::abs(bank) > 90;
            recovery_bank = bank;
        }
        return {bank,target,target-bank,blend,diving};
    }
};
}
