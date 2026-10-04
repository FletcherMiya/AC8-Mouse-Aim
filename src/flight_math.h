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
struct GuidanceOutput {
    float pitch{},yaw{},roll{},bank_error{},turn_weight{},lead_scale{1};
};
// AC input-response estimates, not aerodynamic forces. No integral or hidden
// near-target output clamp. Reducing demand never reduces braking authority.
inline float rate_command(float wanted,float actual,float full_rate) {
    return std::clamp((2*wanted-actual)/full_rate,-1.0f,1.0f);
}
struct CoordinatedGuidance {
    float turn_side=1;
    void reset() { turn_side=1; }
    GuidanceOutput step(Basis b,V aim,float pitch_rate,float yaw_rate,float roll_rate) {
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
        const float predicted_angle=angle-lead;
        const float level=std::atan2(b.r.z,b.u.z)/rad;
        if(std::abs(right)>0.08f) turn_side=right>0?1.0f:-1.0f;
        float turn=std::atan2(right,up)/rad;
        // At the antipode the turn plane is undefined. Choose pull-up rather
        // than returning zero on all axes. Hold turn side around the down seam.
        if(tangent<1e-4f && f<0) turn=0;
        else if(up<0 && std::abs(right)<0.08f)
            turn=std::atan2(turn_side*std::abs(right),up)/rad;
        out.turn_weight=smooth_range(predicted_angle,2.0f,18.0f);
        // Modest below-nose requests can be pushed toward directly; do not
        // command an inverted aircraft for a small downward cursor movement.
        if(f>0 && up<0) out.turn_weight*=smooth_range(angle,25.0f,60.0f);
        out.bank_error=wrapped(level+wrapped(turn-level)*out.turn_weight);
        out.roll=rate_command(arrival_rate(out.bank_error,140,240,3.5f,0.35f),roll_rate,170);

        // Pointing error is a tangent-plane rotation vector. Unlike atan2 with
        // a positive-clamped forward component, it retains the rear hemisphere.
        const float pe=tangent>1e-4f ? angle*up/tangent : (f<0?180.0f:0.0f);
        const float ye=tangent>1e-4f ? angle*right/tangent : 0;
        const float alignment=tangent>1e-4f ? std::max(0.0f,up/tangent) : 1;
        const float gate=1-out.turn_weight*(1-alignment*alignment);
        float pitch_wanted=arrival_rate(pe,45,90,2.6f,0.15f);
        // Gate target rate, NOT stick output: counter-steering remains possible.
        pitch_wanted*=gate;
        out.pitch=rate_command(pitch_wanted,pitch_rate,55);
        const float yaw_wanted=arrival_rate(ye,7,30,1.8f,0.15f);
        out.yaw=rate_command(yaw_wanted,yaw_rate,14);
        return out;
    }
};
}
