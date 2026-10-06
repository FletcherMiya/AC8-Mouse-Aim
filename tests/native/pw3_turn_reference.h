// Frozen pw.3 baseline, from ac8-mouse-aim/0.2.30-pw.3; do not tune.
#pragma once
#include "pw3_bank_reference.h"
#include "response_settings.h"

namespace pw3_reference {
using namespace flight;
struct TurnDemand {
    float pitch_command{}, yaw_command{};
    float horizontal_rate{}, vertical_rate{}, pitch_rate{}, yaw_rate{};
    float roll_gate{}, rate_scale{}, roll_advance{};
};
inline float roll_command(const BankDemand& bank,float actual,const ResponseSettings& cfg) {
    if(bank.diving) return arrival_command(bank.error,actual,70+70*bank.blend,240,3.5f,1,170,1,.15f);
    const float scale=1+(cfg.roll_rate_scale-1)*bank.blend;
    const float wanted=arrival_rate(bank.error,(70+40*bank.blend)*scale,120*scale,3.5f*scale,1,.25f);
    return rate_command(wanted,actual,170,1,cfg.response_gain,cfg.countersteer_gain);
}
// Allocate a desired heading/elevation velocity to BOTH body axes. At bank R:
// elevation_dot = pitch_rate*cos(R) - yaw_rate*sin(R).
// Limiting yaw independently after allocation breaks that cancellation and
// makes a horizontal turn climb. Scale the pair together to retain direction.
inline TurnDemand coordinated_turn(const Basis& b,V aim,const BankDemand& bank,
                                   float actual_pitch,float actual_yaw,float actual_roll=0,
                                   const ResponseSettings& cfg=ResponseSettings{}) {
    const float pitch_limit=45*cfg.turn_rate_scale,yaw_limit=7*cfg.turn_rate_scale;
    const float pitch_input=std::min(1.0f,.85f*cfg.turn_rate_scale);
    const float yaw_input=std::min(1.0f,.7f*cfg.turn_rate_scale);
    V level_right=cross(V{0,0,1},b.f);
    if (dot(level_right,level_right)<0.03f) {
        // World heading is undefined at a pole. Keep the pre-existing body
        // guidance there until the horizon becomes well-defined again.
        const float forward=dot(aim,b.f);
        const float pe=std::atan2(dot(aim,b.u),std::max(.02f,forward))/rad;
        const float ye=std::atan2(dot(aim,b.r),std::max(.02f,forward))/rad;
        const float q=arrival_rate(pe,pitch_limit,90,1.8f*cfg.turn_rate_scale,.2f);
        const float r=arrival_rate(ye,yaw_limit,90,1.2f*cfg.turn_rate_scale,.2f);
        return {rate_command(q,actual_pitch,55,pitch_input,cfg.response_gain,cfg.countersteer_gain),
                rate_command(r,actual_yaw,10,yaw_input,cfg.response_gain,cfg.countersteer_gain),
                0,0,q,r,1,1};
    }
    level_right=unit(level_right);
    const V level_up=unit(cross(b.f,level_right));
    const float sine=dot(b.u,level_right), cosine=dot(b.u,level_up);
    const float heading_error=std::atan2(dot(aim,level_right),std::max(.02f,dot(aim,b.f)))/rad;
    const float elevation_error=pitch(aim)-pitch(b.f);
    // Anticipate only motion toward the desired bank, by at most 12 degrees.
    // Use CURRENT attitude for axis allocation; predicted attitude there would
    // bring back premature climb. During inverted recovery, do not anticipate.
    const float advance=std::abs(bank.current)>90 ? 0 : std::clamp(
        actual_roll*std::copysign(1.0f,bank.error)*cfg.roll_lookahead,
        0.0f,std::min(12.0f,std::abs(bank.error)));
    float gate=std::clamp(1-(std::abs(bank.error)-advance)/30.0f,0.0f,1.0f);
    gate=gate*gate*(3-2*gate);
    float horizontal=arrival_rate(heading_error,pitch_limit,90,1.8f*cfg.turn_rate_scale,.2f)*gate;
    float vertical=arrival_rate(elevation_error,pitch_limit,90,2.6f*cfg.turn_rate_scale,.2f);
    float q=cosine*vertical+sine*horizontal;
    float r=-sine*vertical+cosine*horizontal;
    const float scale=1/std::max({1.0f,std::abs(q)/pitch_limit,std::abs(r)/yaw_limit});
    q*=scale; r*=scale; horizontal*=scale; vertical*=scale;
    return {rate_command(q,actual_pitch,55,pitch_input,cfg.response_gain,cfg.countersteer_gain),
            rate_command(r,actual_yaw,10,yaw_input,cfg.response_gain,cfg.countersteer_gain),
            horizontal,vertical,q,r,gate,scale,advance};
}
}
