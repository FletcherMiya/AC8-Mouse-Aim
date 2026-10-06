#pragma once
#include "bank_guidance.h"
#include "response_settings.h"

namespace pw7_reference {
struct TurnDemand {
    float pitch_command{}, yaw_command{};
    float horizontal_rate{}, vertical_rate{}, pitch_rate{}, yaw_rate{};
    float roll_gate{}, rate_scale{}, roll_advance{}, allocation_blend{};
    float yaw_boost{};
};
inline float dive_pitch_command(float error,float actual,float final_gain,const ResponseSettings& cfg) {
    if(!cfg.high_g_requested) {
        // Release only the distant-target input cap. Keep the old arrival-rate
        // envelope and braking, including when already turning too fast.
        const float command=arrival_command(error,actual,45,90,1.8f+.8f*final_gain,.2f,55,1);
        const float extra=cfg.dive_pitch_boost && command*error>0 ? smooth_range(std::abs(error),20,45) : 0;
        const float limit=.85f+.15f*extra;
        return std::clamp(command,-limit,limit);
    }
    const float limit=pitch_rate_limit(cfg), boost=limit/45;
    const float wanted=arrival_rate(error,limit,90*boost,(1.8f+.8f*final_gain)*boost,.2f);
    return rate_command(wanted,actual,cfg.high_g_pitch_rate,1,cfg.response_gain,cfg.countersteer_gain);
}
inline float horizon_roll_drift(const Basis& b,float pitch_rate,float yaw_rate) {
    const V right=cross(V{0,0,1},b.f);
    const float cosine_pitch=std::sqrt(dot(right,right));
    if(cosine_pitch<std::sqrt(.03f)) return 0; // BankGuidance transports its reference at a pole.
    const V level_right=right*(1/cosine_pitch), level_up=cross(b.f,level_right);
    const float horizontal=pitch_rate*dot(b.u,level_right)+yaw_rate*dot(b.u,level_up);
    return horizontal*b.f.z/cosine_pitch;
}
inline float roll_command(const BankDemand& bank,float actual,const ResponseSettings& cfg,float drift=0) {
    if(bank.diving) return arrival_command(bank.error,actual,70+70*bank.blend,240,3.5f,1,170,1,.15f);
    const float scale=1+(cfg.roll_rate_scale-1)*bank.blend;
    const float wanted=arrival_rate(bank.error,(70+40*bank.blend)*scale,120*scale,3.5f*scale,1,.25f);
    const float bank_rate=actual+drift;
    const float gain=wanted*bank_rate<0 ? cfg.countersteer_gain : cfg.response_gain;
    float command=(wanted-drift+(wanted-bank_rate)*gain)/170;
    if(std::abs(bank.error)<=1 || bank_rate*std::copysign(1.0f,bank.error)>std::abs(wanted)+3)
        command=(-drift+(wanted-bank_rate)*cfg.countersteer_gain)/170;
    return std::clamp(command,-1.0f,1.0f);
}
// Allocate a desired heading/elevation velocity to BOTH body axes. At bank R:
// elevation_dot = pitch_rate*cos(R) - yaw_rate*sin(R).
// Near-level turns retain joint limits to avoid unintended climb. Diagonal
// turns may bend their path to use pitch authority when yaw is saturated.
inline TurnDemand coordinated_turn(const Basis& b,V aim,const BankDemand& bank,
                                   float actual_pitch,float actual_yaw,float actual_bank_rate=0,
                                   const ResponseSettings& cfg=ResponseSettings{}) {
    const float pitch_limit=pitch_rate_limit(cfg);
    float yaw_limit=7*cfg.turn_rate_scale;
    const float pitch_input=cfg.high_g_requested ? 1 : std::min(1.0f,.85f*cfg.turn_rate_scale);
    const float pitch_full=cfg.high_g_requested ? cfg.high_g_pitch_rate : 55;
    float yaw_input=std::min(1.0f,.7f*cfg.turn_rate_scale);
    V level_right=cross(V{0,0,1},b.f);
    if (dot(level_right,level_right)<0.03f) {
        // World heading is undefined at a pole. Keep the pre-existing body
        // guidance there until the horizon becomes well-defined again.
        const float forward=dot(aim,b.f);
        const float pe=std::atan2(dot(aim,b.u),std::max(.02f,forward))/rad;
        const float ye=std::atan2(dot(aim,b.r),std::max(.02f,forward))/rad;
        const float boost=pitch_limit/(45*cfg.turn_rate_scale);
        const float q=arrival_rate(pe,pitch_limit,90*boost,1.8f*cfg.turn_rate_scale*boost,.2f);
        const float r=arrival_rate(ye,yaw_limit,90,1.2f*cfg.turn_rate_scale,.2f);
        return {rate_command(q,actual_pitch,pitch_full,pitch_input,cfg.response_gain,cfg.countersteer_gain),
                rate_command(r,actual_yaw,10,yaw_input,cfg.response_gain,cfg.countersteer_gain),
                0,0,q,r,1,1};
    }
    level_right=unit(level_right);
    const V level_up=unit(cross(b.f,level_right));
    const float sine=dot(b.u,level_right), cosine=dot(b.u,level_up);
    const auto plan=steering_plan(b,aim,cfg,bank.rear_turn_sign);
    // Anticipate only motion toward the desired bank, by at most 12 degrees.
    // Use CURRENT attitude for axis allocation; predicted attitude there would
    // bring back premature climb. During inverted recovery, do not anticipate.
    const float advance=std::abs(bank.current)>90 ? 0 : std::clamp(
        actual_bank_rate*std::copysign(1.0f,bank.error)*cfg.roll_lookahead,
        0.0f,std::min(12.0f,std::abs(bank.error)));
    float gate=std::clamp(1-(std::abs(bank.error)-advance)/30.0f,0.0f,1.0f);
    gate=gate*gate*(3-2*gate);
    // Optional extra rudder only for large, aligned, near-level high-G turns.
    // Fade out before arrival, during bank changes and for diagonal motion.
    const float angle=std::acos(std::clamp(dot(b.f,aim),-1.0f,1.0f))/rad;
    const float yaw_boost=cfg.high_g_yaw_boost && cfg.high_g_requested && !bank.diving && std::abs(bank.current)<=90 ?
        smooth_range(angle,20,45)*smooth_range(gate,.9f,1)*
        (1-smooth_range(std::abs(plan.elevation_error),3,10)) : 0;
    yaw_limit+=(std::max(yaw_limit,10.0f)-yaw_limit)*yaw_boost;
    yaw_input+=(1-yaw_input)*yaw_boost;
    float horizontal=plan.horizontal*gate;
    float vertical=plan.vertical;
    float q=cosine*vertical+sine*horizontal;
    float r=-sine*vertical+cosine*horizontal;
    const float scale=1/std::max({1.0f,std::abs(q)/pitch_limit,std::abs(r)/yaw_limit});
    // Keep exact coordinated allocation for near-level tracking. A diagonal
    // maneuver may bend its path: let available pitch authority contribute
    // even when the much weaker yaw axis saturates. Transition continuously.
    float allocation=std::clamp((std::abs(plan.elevation_error)-3)/12,0.0f,1.0f);
    allocation=allocation*allocation*(3-2*allocation);
    q=q*scale+(std::clamp(q,-pitch_limit,pitch_limit)-q*scale)*allocation;
    r=r*scale+(std::clamp(r,-yaw_limit,yaw_limit)-r*scale)*allocation;
    // Independent limits must not reverse the requested elevation direction.
    // Only reduce an opposing contribution, never increase an axis past limits.
    const float sign=std::copysign(1.0f,vertical);
    const float pq=sign*q*cosine, pr=-sign*r*sine;
    if(std::abs(vertical)>.001f && pq+pr<0) {
        const float positive=std::max(0.0f,pq)+std::max(0.0f,pr);
        const float negative=-std::min(0.0f,pq)-std::min(0.0f,pr);
        const float reduce=positive/std::max(.001f,negative);
        if(pq<0) q*=reduce;
        if(pr<0) r*=reduce;
    }
    horizontal=q*sine+r*cosine; vertical=q*cosine-r*sine;
    return {rate_command(q,actual_pitch,pitch_full,pitch_input,cfg.response_gain,cfg.countersteer_gain),
            rate_command(r,actual_yaw,10,yaw_input,cfg.response_gain,cfg.countersteer_gain),
            horizontal,vertical,q,r,gate,scale,advance,allocation,yaw_boost};
}
}
