#pragma once
#include "bank_guidance.h"

namespace flight {
enum class RolloutPhase { Tracking, Waiting, Returning };
enum class RolloutReason { None, ResidualMotion, SlowRotation, TargetMoved, Departed, Bypass, Timeout, Returned };
struct RolloutDemand {
    BankDemand bank; // Roll-only copy. Never feed this to angle_turn().
    RolloutPhase phase{};
    RolloutReason reason{};
    float nose_speed{}, residual_speed{}, wait_time{};
};
inline float target_distance(V a,V b) {
    return std::acos(std::clamp(dot(a,b),-1.f,1.f))/rad;
}

// A separate roll setpoint prevents holding the current bank from falsely
// satisfying the pitch gate. All pitch/yaw decisions use untouched pw.7 data.
class RolloutCoordinator {
    RolloutPhase phase=RolloutPhase::Tracking;
    RolloutReason reason=RolloutReason::None;
    V anchor{};
    float target=0, elapsed=0, quiet=0, previous_speed=0;
    bool speed_seeded=false;
    bool spent=false, anchor_valid=false;
public:
    void reset() { *this=RolloutCoordinator{}; }
    RolloutPhase state() const { return phase; }
    RolloutDemand step(const Basis& b,V aim,const BankDemand& turn_bank,
                       float pitch_rate,float yaw_rate,float dt,bool enabled=true) {
        RolloutDemand out{turn_bank};
        out.nose_speed=std::hypot(pitch_rate,yaw_rate);
        const float angle=target_distance(b.f,aim);
        const V horizon=cross(V{0,0,1},b.f);
        if(!enabled || turn_bank.diving || std::abs(turn_bank.current)>90 || dot(horizon,horizon)<=.03f || dt<=0 || dt>.1f) {
            reset(); out.reason=RolloutReason::Bypass; return out;
        }
        // The measured rate is filtered. A rapidly stopping nose must not
        // wait for that filter to reach zero before roll is allowed to return.
        const float stopping=speed_seeded ? std::max(0.f,(previous_speed-out.nose_speed)/dt) : 0;
        out.residual_speed=std::max(0.f,out.nose_speed-.30f*stopping);
        previous_speed=out.nose_speed; speed_seeded=true;
        const bool moved=anchor_valid && target_distance(anchor,aim)>5;
        const bool departed=angle>12;
        if(moved || departed) {
            spent=false; anchor=aim; anchor_valid=true;
            if(phase==RolloutPhase::Waiting) {
                phase=RolloutPhase::Returning;
                reason=moved?RolloutReason::TargetMoved:RolloutReason::Departed;
            }
        }
        // Do not delay ordinary roll-in. Only intercept a leveling request
        // after the nose has already reached the 3-degree acceptance cone.
        const bool leveling=(turn_bank.target-turn_bank.current)*turn_bank.current<0;
        if(phase==RolloutPhase::Tracking && !spent && angle<=3 &&
           std::abs(turn_bank.current)>=20 && leveling && out.residual_speed>10) {
            phase=RolloutPhase::Waiting; reason=RolloutReason::ResidualMotion;
            target=turn_bank.current; anchor=aim; anchor_valid=true;
            elapsed=quiet=0; spent=true;
        }
        if(phase==RolloutPhase::Waiting) {
            elapsed+=dt;
            quiet=out.residual_speed<=10 ? quiet+dt : 0;
            // A slow but off-target nose must be allowed to resume pw.7
            // correction; requiring perfect alignment here can deadlock it.
            if(quiet>=.10f || elapsed>=2) {
                phase=RolloutPhase::Returning;
                reason=elapsed>=2?RolloutReason::Timeout:RolloutReason::SlowRotation;
            }
        }
        if(phase==RolloutPhase::Returning) {
            target+=(turn_bank.target-target)*(1-std::exp(-dt/.20f));
            if(std::abs(turn_bank.target-target)<.5f) {
                target=turn_bank.target; phase=RolloutPhase::Tracking;
                reason=RolloutReason::Returned;
            }
        }
        if(phase!=RolloutPhase::Tracking) {
            out.bank.target=target; out.bank.error=target-turn_bank.current;
        }
        out.phase=phase; out.reason=reason; out.wait_time=elapsed;
        return out;
    }
};
}
