#pragma once
#include <cmath>
#include <algorithm>
// MouseFlight-inspired local-space guidance; Unreal axes: X forward, Y right, Z up.
namespace flight {
constexpr float rad = 0.017453292519943295f;
struct V {
    float x{}, y{}, z{};
    V operator+(V b) const { return {x+b.x,y+b.y,z+b.z}; }
    V operator-(V b) const { return {x-b.x,y-b.y,z-b.z}; }
    V operator*(float k) const { return {x*k,y*k,z*k}; }
};
inline float dot(V a,V b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline V cross(V a,V b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline V unit(V a) { float n=std::sqrt(dot(a,a)); return n>1e-6f ? a*(1/n):V{1,0,0}; }
struct Basis { V f,r,u; };
inline Basis basis(float pitch,float yaw,float roll) {
    float p=pitch*rad,y=yaw*rad,r=roll*rad;
    V f{std::cos(p)*std::cos(y),std::cos(p)*std::sin(y),std::sin(p)};
    V right{-std::sin(y),std::cos(y),0};
    V up=cross(f,right);
    return {f,right*std::cos(r)-up*std::sin(r),up*std::cos(r)+right*std::sin(r)};
}
inline V rotate(V v,V axis,float angle) {
    float c=std::cos(angle),s=std::sin(angle);
    return unit(v*c+cross(axis,v)*s+axis*(dot(axis,v)*(1-c)));
}
inline float pitch(V v) { return std::asin(std::clamp(v.z,-1.0f,1.0f))/rad; }
inline float yaw(V v) { return std::atan2(v.y,v.x)/rad; }
inline float dead(float x,float d) { return std::copysign(std::max(0.0f,std::abs(x)-d),x); }
inline float tracking_weight(float angle) {
    float t=std::clamp((angle-0.75f)/4.25f,0.0f,1.0f);
    return t*t*(3-2*t);
}
// Conservative stopping envelope: distance = speed*delay + speed^2/(2*deceleration).
struct LevelBlend {
    bool leveling=false;
    float weight=1;
    void reset() { leveling=false; weight=1; }
    float step(float angle,float dt) {
        if(angle<=3) leveling=true;
        else if(angle>=6) leveling=false;
        const float wanted=leveling?0:tracking_weight(angle);
        const float delta=std::clamp(wanted-weight,-4*dt,4*dt);
        weight=std::clamp(weight+delta,0.0f,1.0f);
        return weight;
    }
};
// Parameters describe an estimated input response, not changes to the flight model.
inline float arrival_rate(float error,float max_rate,float deceleration,float gain,float zone) {
    float distance=std::max(0.0f,std::abs(error)-zone);
    float delay_speed=deceleration*0.15f;
    float stoppable=std::sqrt(delay_speed*delay_speed+2*deceleration*distance)-delay_speed;
    return std::copysign(std::min({max_rate,gain*distance,stoppable}),error);
}
inline float arrival_command(float error,float actual,float max_rate,float deceleration,
                             float gain,float zone,float full_rate,float limit) {
    float wanted=arrival_rate(error,max_rate,deceleration,gain,zone);
    float command=wanted/full_rate+(wanted-actual)/full_rate;
    // When closing faster than the stopping envelope permits, actively counter-steer.
    if(std::abs(error)<=zone || actual*std::copysign(1.0f,error)>std::abs(wanted)+3.0f)
        command=(wanted-actual)/(full_rate*0.65f);
    return std::clamp(command,-limit,limit);
}

inline float smooth_range(float value,float low,float high) {
    float t=std::clamp((value-low)/(high-low),0.0f,1.0f);
    return t*t*(3-2*t);
}
inline float wrapped(float degrees) {
    return std::remainder(degrees,360.0f);
}
struct ArrivalOutput {
    float command{},wanted{},stop_distance{},brake{};
    float acceleration{};
};
// Local response estimate: rate_dot = authority * stick - damping * rate.
// Both the stopping envelope and inverse input calculation use this SAME model.
// 0.2.32 trace, low-bank/no-keyboard samples suggested pitch ~25*u - 0.2*rate;
// this is an initial estimate, not a universal aircraft model or online learning.
inline ArrivalOutput predictive_arrival(float error,float actual,float max_rate,
        float authority,float gain,float zone,float damping,float gate=1) {
    ArrivalOutput out;
    const float deceleration=authority*0.85f; // Leave some input margin for coupling.
    const float distance=std::max(0.0f,std::abs(error)-zone);
    const float sign=std::copysign(1.0f,error);
    const float closing=std::max(0.0f,actual*sign);
    const float remaining=std::max(0.0f,distance-closing*0.08f);
    const float delay_speed=deceleration*0.12f;
    const float safe=std::sqrt(delay_speed*delay_speed+2*deceleration*remaining)-delay_speed;
    const float linear=gain*remaining;
    // Narrower smooth minimum (power 8, previously 4). It remains <= each
    // bound, but avoids slowing far ahead of the actual stopping envelope.
    // Keep its analytic derivative in sync; no hard min / acceleration jump.
    constexpr float blend_power=8.0f;
    auto blend_min=[blend_power](float x,float y) {
        if(x<=0 || y<=0) return 0.0f;
        const float lower_value=std::min(x,y),upper_value=std::max(x,y);
        return lower_value/std::pow(1+std::pow(lower_value/upper_value,blend_power),1.0f/blend_power);
    };
    const float curve=blend_min(linear,safe);
    const float wanted=blend_min(curve,max_rate);
    out.wanted=sign*wanted*gate;
    out.stop_distance=closing*0.20f+closing*closing/(2*deceleration);
    // Derivative of the arrival-rate curve: even when rate == wanted, the
    // reference is slowing down. Old feedback alone supplied zero braking there.
    float slope=0;
    if(linear>1e-5f && safe>1e-5f && curve>1e-5f) {
        const float ds=deceleration/std::sqrt(delay_speed*delay_speed+2*deceleration*remaining);
        slope=(std::pow(curve/linear,blend_power+1)*gain+std::pow(curve/safe,blend_power+1)*ds)*std::pow(wanted/curve,blend_power+1);
    }
    slope*=gate*smooth_range(remaining,0,0.5f);
    const float toward=actual*sign>0?actual:0;
    out.acceleration=4.0f*(out.wanted-actual)-slope*toward;
    out.command=std::clamp((out.acceleration+damping*actual)/authority,-1.0f,1.0f);
    out.brake=std::clamp(-out.command*std::copysign(1.0f,actual),0.0f,1.0f);
    return out;
}
struct GuidanceOutput {
    float pitch{},yaw{},roll{},bank_error{},turn_weight{},lead_scale{1};
    float pitch_error{},yaw_error{};
    float pitch_coupling{},yaw_coupling{};
    float bank_demand{},bank_target_rate{},predicted_roll_rate{};
    ArrivalOutput pitch_arrival,yaw_arrival,roll_arrival;
};
// AC input-response estimates, not aerodynamic forces. No integral or hidden
// near-target output clamp. Reducing demand never reduces braking authority.
inline float rate_command(float wanted,float actual,float full_rate) {
    return std::clamp((2*wanted-actual)/full_rate,-1.0f,1.0f);
}
// Roll tracking uses measured acceleration to compensate residual response lag,
// and separates moving bank demand from actual closing speed. No integral and
// no aircraft-identification assumption. State belongs only to the active pawn.
struct RollTracker {
    bool valid=false;
    float previous_error{},previous_rate{},target_rate{},acceleration{};
    void reset() { *this={}; }
    ArrivalOutput step(float error,float rate,float dt,float& predicted) {
        if(dt<=0 || dt>0.1f) reset();
        if(valid) {
            const float delta=wrapped(error-previous_error);
            const float alpha=1-std::exp(-dt/0.10f);
            // error_dot = target_rate - aircraft_rate. Ignore discontinuous
            // target jumps (recenter/large mouse jumps), not ordinary tracking.
            const float moving=std::abs(delta)<20 ? std::clamp(delta/dt+(rate+previous_rate)*0.5f,-80.0f,80.0f) : 0;
            target_rate+=(moving-target_rate)*alpha;
            const float measured=std::clamp((rate-previous_rate)/dt,-500.0f,500.0f);
            acceleration+=(measured-acceleration)*alpha;
        } else { target_rate=0; acceleration=0; }
        previous_error=error; previous_rate=rate; valid=dt>0;
        predicted=rate+std::clamp(acceleration*0.08f,-25.0f,25.0f);
        auto result=predictive_arrival(error,predicted-target_rate,140,220,3.5f,.35f,1.5f);
        const float absolute_wanted=result.wanted+target_rate;
        result.wanted=std::clamp(absolute_wanted,-140.0f,140.0f);
        result.acceleration+=4.0f*(result.wanted-absolute_wanted);
        // Inverse response damping is based on actual aircraft motion, not
        // relative closing rate; avoid treating target movement as drag.
        result.command=std::clamp((result.acceleration+1.5f*predicted)/220,-1.0f,1.0f);
        result.brake=std::abs(rate)>0.01f ? std::clamp(-result.command*std::copysign(1.0f,rate),0.0f,1.0f) : 0;
        return result;
    }
};
struct CoordinatedGuidance {
    float turn_side=1;
    RollTracker roll_tracker;
    void reset() { turn_side=1; roll_tracker.reset(); }
    GuidanceOutput step(Basis b,V aim,float pitch_rate,float yaw_rate,float roll_rate,float dt=0) {
        GuidanceOutput out;
        const float f=std::clamp(dot(aim,b.f),-1.0f,1.0f);
        const float right=dot(aim,b.r),up=dot(aim,b.u);
        const float tangent=std::sqrt(right*right+up*up);
        const float angle=std::atan2(tangent,f)/rad;
        // Only predict the aircraft's closing motion, not mouse target motion.
        // Bound lead to 65% of remaining error: never reverse/erase real error.
        const float closing=tangent>1e-5f ? (up*pitch_rate+right*yaw_rate)/tangent : 0;
        const float lead=std::clamp(closing*0.18f,0.0f,angle*0.65f);
        out.lead_scale=angle>1e-5f ? (angle-lead)/angle : 1;
        const float level=std::atan2(b.r.z,b.u.z)/rad;
        if(std::abs(right)>0.08f) turn_side=right>0?1.0f:-1.0f;
        float turn=std::atan2(right,up)/rad;
        // At the antipode the turn plane is undefined. Choose pull-up rather
        // than returning zero on all axes. Hold turn side around the down seam.
        if(tangent<1e-4f && f<0) turn=0;
        else if(up<0 && std::abs(right)<0.08f)
            turn=std::atan2(turn_side*std::abs(right),up)/rad;
        // In the capture region, bank demand follows remaining lateral turn
        // demand in a horizon frame. Do not blend directly to wings-level just
        // because total angle is small and leave several degrees to the rudder.
        V horizon_right=cross(V{0,0,1},b.f);
        const bool near_pole=dot(horizon_right,horizon_right)<0.0025f;
        horizon_right=near_pole?b.r:unit(horizon_right);
        const V horizon_up=cross(b.f,horizon_right);
        const float lateral=std::atan2(dot(aim,horizon_right),f)/rad*out.lead_scale;
        const float vertical=std::atan2(dot(aim,horizon_up),f)/rad*out.lead_scale;
        const float lateral_demand=dead(lateral,0.15f)*1.8f;
        const float vertical_demand=std::max(0.0f,dead(vertical,0.15f)*2.0f);
        out.bank_demand=std::atan2(lateral_demand,3.0f+vertical_demand)/rad;
        const float capture_error=wrapped((near_pole?0:level)+out.bank_demand);
        const float acquisition=smooth_range(angle,12.0f,35.0f);
        out.turn_weight=std::max(acquisition,smooth_range(std::abs(lateral),0.15f,5.0f));
        // Modest below-nose requests can be pushed toward directly; do not
        // command an inverted aircraft for a small downward cursor movement.
        if(f>0 && up<0) out.turn_weight*=smooth_range(angle,25.0f,60.0f);
        // Retain large-angle lift-vector acquisition and direct small pushovers.
        float capture=capture_error;
        if(f>0 && up<0) capture=wrapped(level+wrapped(capture_error-level)*smooth_range(angle,25.0f,60.0f));
        out.bank_error=wrapped(capture+wrapped(turn-capture)*acquisition);
        out.roll_arrival=roll_tracker.step(out.bank_error,roll_rate,dt,out.predicted_roll_rate);
        out.bank_target_rate=roll_tracker.target_rate;
        out.roll=out.roll_arrival.command;

        // Pointing error is a tangent-plane rotation vector. Unlike atan2 with
        // a positive-clamped forward component, it retains the rear hemisphere.
        const float pe=tangent>1e-4f ? angle*up/tangent : (f<0?180.0f:0.0f);
        const float ye=tangent>1e-4f ? angle*right/tangent : 0;
        const float alignment=tangent>1e-4f ? std::max(0.0f,up/tangent) : 1;
        const float gate=1-out.turn_weight*(1-alignment*alignment);
        out.pitch_error=pe; out.yaw_error=ye;
        // Gate target rate, NOT stick output: counter-steering remains possible.
        out.pitch_arrival=predictive_arrival(pe,pitch_rate,45,25,2.0f,0.15f,0.2f,gate);
        out.yaw_arrival=predictive_arrival(ye,yaw_rate,7,12,1.8f,0.15f,1.0f);
        // Transport residual nose motion as the body rolls: p_dot += roll*y,
        // y_dot -= roll*p. Apply only during rollout; preserve braking headroom.
        const float rollout=1-out.turn_weight;
        out.pitch_coupling=std::clamp(roll_rate*rad*yaw_rate*rollout,-15.0f,15.0f);
        out.yaw_coupling=std::clamp(-roll_rate*rad*pitch_rate*rollout,-7.2f,7.2f);
        // Compensation must not cancel an axis's existing braking demand.
        if(out.pitch_arrival.command*pitch_rate<0 && out.pitch_coupling*pitch_rate>0) out.pitch_coupling=0;
        if(out.yaw_arrival.command*yaw_rate<0 && out.yaw_coupling*yaw_rate>0) out.yaw_coupling=0;
        out.pitch=std::clamp((out.pitch_arrival.acceleration+0.2f*pitch_rate+out.pitch_coupling)/25,-1.0f,1.0f);
        out.yaw=std::clamp((out.yaw_arrival.acceleration+yaw_rate+out.yaw_coupling)/12,-1.0f,1.0f);
        return out;
    }
};
}
