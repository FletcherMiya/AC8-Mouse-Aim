#pragma once
#include "flight_math.h"
#include "response_settings.h"

namespace flight {
struct SteeringPlan {
    float horizontal{}, vertical{}, elevation_error{};
    bool valid{};
};
// Bank selection and axis allocation must refer to the SAME turn direction.
// A great-circle bank paired with independent world-elevation tracking can
// point the lift axis away from the requested motion, especially for rear or
// diagonal targets. Keep the established horizon-relative path for both.
// Keep a committed turn through the +/-180 heading seam. Deliberately moving
// the target out of the rear sector releases it; poles/reset also clear it.
class RearTurnHold {
    int direction = 0;
public:
    int step(const Basis& b,V aim,bool enabled) {
        const V right=cross(V{0,0,1},b.f);
        if(!enabled || dot(right,right)<.03f) return direction=0;
        const float heading=std::atan2(dot(aim,unit(right)),dot(aim,b.f))/rad;
        if(std::abs(heading)<177) direction=0;
        else if(!direction && std::abs(heading)>=179) direction=heading<0 ? -1 : 1;
        return direction;
    }
};
inline SteeringPlan steering_plan(const Basis& b,V aim,const ResponseSettings& cfg,int rear_direction=0) {
    const V right=cross(V{0,0,1},b.f);
    if(dot(right,right)<.03f) return {};
    float heading=std::atan2(dot(aim,unit(right)),dot(aim,b.f))/rad;
    if(rear_direction && heading*rear_direction<0) heading+=360*rear_direction;
    const float elevation=pitch(aim)-pitch(b.f);
    const float limit=pitch_rate_limit(cfg), boost=limit/(45*cfg.turn_rate_scale);
    return {arrival_rate(heading,limit,90*boost,1.8f*cfg.turn_rate_scale*boost,.2f),
            arrival_rate(elevation,limit,90*boost,2.6f*cfg.turn_rate_scale*boost,.2f),elevation,true};
}
// Angular errors, in degrees. No rate reference or stopping-rate envelope.
// Bank planning and angle-axis allocation use this same direction.
inline SteeringPlan angle_steering_plan(const Basis& b,V aim,int rear_direction=0) {
    const V right=cross(V{0,0,1},b.f);
    if(dot(right,right)<.03f) return {};
    float heading=std::atan2(dot(aim,unit(right)),dot(aim,b.f))/rad;
    if(rear_direction && heading*rear_direction<0) heading+=360*rear_direction;
    const float elevation=pitch(aim)-pitch(b.f);
    // During a long nearly level turn, prioritize small height corrections.
    // Otherwise a 179-degree heading error swamps a few degrees of unwanted
    // climb after input normalization. Pure climbs and large diagonals retain
    // the configured full-input angle; no heading-rate cap is introduced.
    const float height_weight=1+5*std::clamp(std::abs(heading)/45,0.0f,1.0f)*
        (1-std::clamp(std::abs(elevation)/15,0.0f,1.0f));
    return {dead(heading,.2f),dead(elevation,.2f)*height_weight,elevation,true};
}
}
