#pragma once
#include "flight_math.h"
#include "steering_plan.h"

namespace pw7_reference {
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
    int rear_turn_sign{};
    float roll_activity=1;
};

// The attitude setpoint is measured against the horizon, not against the
// aircraft's moving up vector. Only an explicit steep descent may invert it.
class BankGuidance {
    V reference_right{};
    bool reference_valid = false;
    bool diving = false, recovering = false;
    float dive_sign = 1, recovery_bank = 0;
    LevelBlend leveling;
    RearTurnHold rear_turn;
public:
    void reset() { *this = BankGuidance{}; }
    BankDemand step(const Basis& b, V aim, float angle, float dt, const BankSettings& cfg,
                    const ResponseSettings& response=ResponseSettings{}) {
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
        // Existing bank keeps a committed turn responsive through arrival.
        // Switching solely on remaining error would level early and close the
        // pitch gate halfway through a large turn. Dives/poles retain authority.
        const float roll_activity=response.angle_control && !diving && dot(horizon_right,horizon_right)>.03f ?
            std::max(smooth_range(angle,8,30),smooth_range(std::abs(bank),25,60)) : 1;
        const int rear_direction=rear_turn.step(b,aim,response.rear_turn_hold && !diving);
        if (diving) {
            // A committed flip keeps its direction even when a nearly straight
            // down target fluctuates across the +/-180-degree atan2 boundary.
            target = dive_sign * std::abs(std::atan2(right,up)/rad);
            recovering = false;
            if (bank*dive_sign < -90) bank += dive_sign*360;
        } else {
            // Retain the small-turn softening; large level turns may bank
            // closer to side-on under the new cap, without demanding inversion.
            const auto plan=response.angle_control ? angle_steering_plan(b,aim,rear_direction) :
                steering_plan(b,aim,response,rear_direction);
            float bank_direction=std::atan2(right,std::max(0.12f,up))/rad;
            if(plan.valid) {
                // On leaving a pole, the transported reference may still be
                // slewing toward the horizon. Express the plan in that same
                // reference as 'bank', rather than mixing two roll origins.
                const V level_right=unit(cross(world_up,b.f));
                const V direction=level_right*plan.horizontal+cross(b.f,level_right)*plan.vertical;
                const float planned_right=dot(direction,reference_right), planned_up=dot(direction,reference_up);
                const float magnitude=std::hypot(plan.horizontal,plan.vertical);
                // High-G large turns need a more side-on attitude to use pitch
                // authority without exceeding yaw authority. Honor max_bank.
                const float high_g_blend=response.high_g_requested ? std::clamp((angle-10)/20,0.0f,1.0f) : 0;
                const float soft=magnitude*(.12f-.08f*high_g_blend)/std::max(.01f,std::sin(std::min(angle,90.0f)*rad));
                bank_direction=std::atan2(planned_right,std::max(soft,planned_up))/rad;
            }
            const float small_bank=std::min(cfg.max_bank,response.roll_small_bank);
            const float limit=response.angle_control ? small_bank+(cfg.max_bank-small_bank)*roll_activity : cfg.max_bank;
            target = std::clamp(bank_direction,-limit,limit)*blend;
            // Return through upright instead of choosing a shorter route that
            // continues a roll across the inverted attitude. Retain that route
            // if inertia carries the measured angle across +/-180 degrees.
            if (recovering) bank = recovery_bank + wrap_angle(bank-recovery_bank);
            recovering = std::abs(bank) > 90;
            recovery_bank = bank;
        }
        return {bank,target,target-bank,blend,diving,rear_direction,roll_activity};
    }
};
}
