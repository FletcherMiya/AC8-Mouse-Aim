#pragma once
#include "turn_guidance.h"

namespace flight {
// Retain braking briefly after releasing the chord, while rotation decays.
// Never delay a request or synthesize/extend the game's throttle key inputs.
class HighGBraking {
    float weight=0, previous_rate=0;
    bool arriving=false, terminal=false;
public:
    void reset() { *this=HighGBraking{}; }
    bool settling() const { return terminal; }
    float step(bool requested,float dt,float hold,float angle=180,float rate=0) {
        weight=requested ? 1 : hold>0 ? std::max(0.0f,weight-dt/hold) : 0;
        if(weight==0) reset();
        else {
            if(angle>30) { arriving=true; terminal=false; }
            if(std::abs(rate)>40) arriving=true;
            // End strong braking at the first stop, not on key release. A
            // latched gentle finish prevents delayed full reverse input from
            // repeatedly flinging a powerful aircraft across a nearby target.
            if(arriving && angle<25 && (std::abs(rate)<15 || rate*previous_rate<0)) terminal=true;
            previous_rate=rate;
        }
        return weight;
    }
};
struct AngleAxisDemand {
    float error{}, predicted{}, raw{}, command{};
};
inline AngleAxisDemand angle_axis(float error,float actual,float full_angle,float lookahead,float zone=.2f) {
    const float predicted=dead(error,zone)-actual*lookahead;
    const float raw=predicted/full_angle;
    return {error,predicted,raw,std::clamp(raw,-1.0f,1.0f)};
}
inline AngleAxisDemand angle_roll(const BankDemand& bank,float actual,const ResponseSettings& cfg,float drift=0) {
    const float scale=cfg.roll_small_input_scale+(cfg.roll_large_input_scale-cfg.roll_small_input_scale)*bank.roll_activity;
    // Soften small proportional demands without also weakening rate damping.
    return angle_axis(bank.error,actual+drift,cfg.roll_full_input_angle/scale,
        cfg.roll_brake_lookahead*cfg.roll_brake_gain/scale,1);
}
struct AngleTurnDemand {
    AngleAxisDemand pitch{}, yaw{};
    float heading_error{}, elevation_error{}, gate=1, advance=0, allocation=1, scale=1;
    float brake_weight{}, pitch_lookahead{}, pitch_priority{};
};
inline AngleTurnDemand angle_turn(const Basis& b,V aim,const BankDemand& bank,
                                  float actual_pitch,float actual_yaw,float bank_rate,
                                  const ResponseSettings& cfg) {
    AngleTurnDemand out;
    out.brake_weight=std::max(cfg.high_g_requested?1.f:0.f,cfg.high_g_brake_weight);
    const float arrival_weight=cfg.high_g_settling ? 0 : out.brake_weight;
    const float angle=std::acos(std::clamp(dot(b.f,aim),-1.0f,1.0f))/rad;
    const float brake_distance=std::abs(actual_pitch)*cfg.high_g_brake_lookahead;
    const float proximity=1-smooth_range(angle,brake_distance,brake_distance+cfg.pitch_full_input_angle);
    // Strong reverse feedback is for rapid arrival, not slow final tracking:
    // delayed high-authority aircraft otherwise bounce under heavy damping.
    const float fast_arrival=smooth_range(std::abs(actual_pitch),20,80);
    out.pitch_lookahead=cfg.turn_brake_lookahead+
        (std::max(cfg.turn_brake_lookahead,cfg.high_g_brake_lookahead)-cfg.turn_brake_lookahead)*arrival_weight*proximity*fast_arrival;
    const float ratio=cfg.angle_pitch_yaw_ratio+
        (cfg.angle_high_g_pitch_yaw_ratio-cfg.angle_pitch_yaw_ratio)*out.brake_weight;
    const float yaw_angle=cfg.pitch_full_input_angle/ratio;
    const auto pitch_axis=[&](float error,float zone) {
        auto axis=angle_axis(error,actual_pitch,cfg.pitch_full_input_angle,out.pitch_lookahead,zone);
        if(axis.raw*actual_pitch<0) axis.raw*=1+(cfg.high_g_brake_gain-1)*arrival_weight*fast_arrival;
        if(cfg.high_g_settling) axis.raw*=1-.5f*out.brake_weight;
        axis.command=std::clamp(axis.raw,-1.0f,1.0f);
        return axis;
    };
    const auto plan=angle_steering_plan(b,aim,bank.rear_turn_sign);
    if(bank.diving || !plan.valid) {
        // Preserve the established body-space route during dives and poles.
        const float forward=std::max(.02f,dot(aim,b.f));
        out.pitch=pitch_axis(std::atan2(dot(aim,b.u),forward)/rad,.2f);
        out.yaw=angle_axis(std::atan2(dot(aim,b.r),forward)/rad,actual_yaw,
            yaw_angle,cfg.turn_brake_lookahead);
        return out;
    }
    out.heading_error=plan.horizontal; out.elevation_error=plan.elevation_error;
    out.advance=std::abs(bank.current)>90 ? 0 : std::clamp(
        bank_rate*std::copysign(1.0f,bank.error)*cfg.roll_lookahead,
        0.0f,std::min(12.0f,std::abs(bank.error)));
    out.gate=smooth_range(1-(std::abs(bank.error)-out.advance)/30.0f,0,1);
    const V level_right=unit(cross(V{0,0,1},b.f)), level_up=cross(b.f,level_right);
    const float sine=dot(b.u,level_right), cosine=dot(b.u,level_up);
    const float horizontal=plan.horizontal*out.gate, vertical=plan.vertical;
    const float pe=cosine*vertical+sine*horizontal, ye=-sine*vertical+cosine*horizontal;
    // The plan has already applied its angular dead zones. Applying another
    // per-axis zone here would spoil coordinated level turns at small bank.
    out.pitch=pitch_axis(pe,0);
    out.yaw=angle_axis(ye,actual_yaw,yaw_angle,cfg.turn_brake_lookahead,0);
    out.scale=1/std::max({1.0f,std::abs(out.pitch.raw),std::abs(out.yaw.raw)});
    out.allocation=smooth_range(std::abs(plan.elevation_error),3,15);
    // A weak/saturated rudder must not repeatedly throttle a distant high-G
    // pitch demand once bank is aligned. Fade before arrival or inversion.
    out.pitch_priority=cfg.high_g_requested && std::abs(bank.current)<=90 ?
        cfg.high_g_pitch_priority*smooth_range(angle,30,60)*smooth_range(out.gate,.85f,1)*
        (1-smooth_range(std::abs(plan.elevation_error),1,3)) : 0;
    out.allocation=std::max(out.allocation,out.pitch_priority);
    float q=out.pitch.raw*out.scale+(out.pitch.command-out.pitch.raw*out.scale)*out.allocation;
    float r=out.yaw.raw*out.scale+(out.yaw.command-out.yaw.raw*out.scale)*out.allocation;
    // Protect the predicted vertical correction, including intentional braking.
    // Only diminish an opposing contribution; never invent extra authority.
    const float predicted_vertical=vertical-out.pitch_lookahead*actual_pitch*cosine+
        cfg.turn_brake_lookahead*actual_yaw*sine;
    const float sign=std::copysign(1.0f,predicted_vertical);
    const float pq=sign*q*cosine, pr=-sign*r*sine/ratio;
    if(std::abs(predicted_vertical)>.001f && pq+pr<0) {
        const float positive=std::max(0.0f,pq)+std::max(0.0f,pr);
        const float negative=-std::min(0.0f,pq)-std::min(0.0f,pr);
        const float reduce=positive/std::max(.001f,negative);
        if(pq<0) q*=reduce;
        if(pr<0) r*=reduce;
    }
    out.pitch.command=std::clamp(q,-1.0f,1.0f);
    // Preserve arrival braking even when yaw normalization/height correction
    // asks for less pitch. This is reverse input only, never a minimum pull.
    if(out.pitch.raw*actual_pitch<0 && out.brake_weight>0)
        out.pitch.command+=(std::clamp(out.pitch.raw,-1.0f,1.0f)-out.pitch.command)*out.brake_weight;
    out.yaw.command=std::clamp(r,-1.0f,1.0f);
    return out;
}
}
