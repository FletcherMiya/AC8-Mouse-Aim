#pragma once
#include "angle_guidance.h"

namespace flight {
// Supplement an existing reverse demand during a fast final arrival. The
// original pw.7 pitch/yaw allocation and HighGBraking state are left intact.
// Forward demands and targets farther than 25 degrees are never changed.
class TerminalBraking {
    float previous=0, slowing=0;
    bool seeded=false, arriving=false, spent=false;
    V anchor{};
    bool anchored=false;
public:
    float fade_speed=0; // Short extrapolation, not a requested angular rate.
    void reset() { *this=TerminalBraking{}; }
    bool active() const { return arriving; }
    float step(const Basis& b,V aim,const BankDemand& bank,float rate,
               const AngleTurnDemand& turn,const ResponseSettings& cfg,float dt) {
        const float original=turn.pitch.command;
        const V horizon=cross(V{0,0,1},b.f);
        if(!cfg.angle_control || !cfg.rollout_coordination || bank.diving ||
           std::abs(bank.current)>90 || dot(horizon,horizon)<=.03f || dt<=0 || dt>.1f) {
            reset(); return original;
        }
        const float angle=std::acos(std::clamp(dot(b.f,aim),-1.f,1.f))/rad;
        const bool moved=anchored && std::acos(std::clamp(dot(anchor,aim),-1.f,1.f))/rad>5;
        if(angle>25 || moved) {
            arriving=false; spent=false; anchor=aim; anchored=true;
        }
        const float change=seeded && rate*previous>0 ?
            std::max(0.f,(std::abs(previous)-std::abs(rate))/dt) : 0;
        slowing=change>0 ? slowing+(change-slowing)*(1-std::exp(-dt/.05f)) : 0;
        // As a fast stop approaches, withdraw the supplement before delayed
        // reverse commands arrive. This stores no aircraft model or authority.
        const float fade_time=.08f+.17f*smooth_range(change/std::max(10.f,std::abs(rate)),4,8);
        fade_speed=std::max(0.f,std::abs(rate)-fade_time*change);
        // Slow corrections already work well in pw.7. Arm once per arrival,
        // then retain braking through a small overshoot until the first stop.
        if(!spent && angle<=6 && std::abs(rate)>20 && original*rate<0) {
            arriving=true; spent=true; anchor=aim; anchored=true;
        }
        if(arriving && (fade_speed<5 || (seeded && rate*previous<0))) arriving=false;
        previous=rate; seeded=true;
        if(original*rate>=0 || angle>=25) return original;
        const float high=std::max(cfg.high_g_requested?1.f:0.f,cfg.high_g_brake_weight);
        const float proximity=1-smooth_range(angle,10,25);
        const float high_weight=proximity*smooth_range(std::max(0.f,std::abs(rate)-.10f*slowing),10,40);
        const float normal_weight=arriving ? proximity*smooth_range(fade_speed,5,20) : 0;
        if(normal_weight==0 && high_weight==0) return original;
        const float normal=std::clamp(original*(1+4*normal_weight),-1.f,1.f);
        // High G already has strong predictive feedback. Its supplement is
        // smaller and decays with pw.7's existing key-release/settling behavior.
        const float high_g=std::clamp(original*(1+high_weight),-1.f,1.f);
        return normal*(1-high)+high_g*high;
    }
};
}
