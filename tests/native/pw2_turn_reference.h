// Frozen 0.2.30-pw.2 turn controller, tag ac8-mouse-aim/0.2.30-pw.2.
// Independent baseline for response comparisons; do not tune with production.
#pragma once
#include "pw2_bank_reference.h"

namespace pw2_reference {
using namespace flight;
struct TurnDemand {
    float pitch_command{}, yaw_command{};
    float horizontal_rate{}, vertical_rate{}, pitch_rate{}, yaw_rate{};
    float roll_gate{}, rate_scale{};
};
inline float rate_command(float wanted,float actual,float full_rate,float limit) {
    float command=(2*wanted-actual)/full_rate;
    if (std::abs(wanted)<0.001f || actual*std::copysign(1.0f,wanted)>std::abs(wanted)+3)
        command=(wanted-actual)/(full_rate*0.65f);
    return std::clamp(command,-limit,limit);
}
// Allocate a desired heading/elevation velocity to BOTH body axes. At bank R:
// elevation_dot = pitch_rate*cos(R) - yaw_rate*sin(R).
// Limiting yaw independently after allocation breaks that cancellation and
// makes a horizontal turn climb. Scale the pair together to retain direction.
inline TurnDemand coordinated_turn(const Basis& b,V aim,const BankDemand& bank,
                                   float actual_pitch,float actual_yaw) {
    V level_right=cross(V{0,0,1},b.f);
    if (dot(level_right,level_right)<0.03f) {
        // World heading is undefined at a pole. Keep the pre-existing body
        // guidance there until the horizon becomes well-defined again.
        const float forward=dot(aim,b.f);
        const float pe=std::atan2(dot(aim,b.u),std::max(.02f,forward))/rad;
        const float ye=std::atan2(dot(aim,b.r),std::max(.02f,forward))/rad;
        const float q=arrival_rate(pe,45,90,1.8f,.2f);
        const float r=arrival_rate(ye,7,90,1.2f,.2f);
        return {rate_command(q,actual_pitch,55,.85f),rate_command(r,actual_yaw,10,.7f),
                0,0,q,r,1,1};
    }
    level_right=unit(level_right);
    const V level_up=unit(cross(b.f,level_right));
    const float sine=dot(b.u,level_right), cosine=dot(b.u,level_up);
    const float heading_error=std::atan2(dot(aim,level_right),std::max(.02f,dot(aim,b.f)))/rad;
    const float elevation_error=pitch(aim)-pitch(b.f);
    float gate=std::clamp(1-std::abs(bank.error)/30.0f,0.0f,1.0f);
    gate=gate*gate*(3-2*gate);
    float horizontal=arrival_rate(heading_error,45,90,1.8f,.2f)*gate;
    float vertical=arrival_rate(elevation_error,45,90,2.6f,.2f);
    float q=cosine*vertical+sine*horizontal;
    float r=-sine*vertical+cosine*horizontal;
    const float scale=1/std::max({1.0f,std::abs(q)/45.0f,std::abs(r)/7.0f});
    q*=scale; r*=scale; horizontal*=scale; vertical*=scale;
    return {rate_command(q,actual_pitch,55,.85f),rate_command(r,actual_yaw,10,.7f),
            horizontal,vertical,q,r,gate,scale};
}
}
