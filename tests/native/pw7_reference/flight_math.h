#pragma once
#include <cmath>
#include <algorithm>
// MouseFlight-inspired local-space guidance; Unreal axes: X forward, Y right, Z up.
namespace pw7_reference {
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
inline float smooth_range(float value,float low,float high) {
    const float t=std::clamp((value-low)/(high-low),0.0f,1.0f);
    return t*t*(3-2*t);
}
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
inline float arrival_rate(float error,float max_rate,float deceleration,float gain,float zone,float delay=0.15f) {
    float distance=std::max(0.0f,std::abs(error)-zone);
    float delay_speed=deceleration*delay;
    float stoppable=std::sqrt(delay_speed*delay_speed+2*deceleration*distance)-delay_speed;
    return std::copysign(std::min({max_rate,gain*distance,stoppable}),error);
}
inline float rate_command(float wanted,float actual,float full_rate,float limit,
                          float response_gain=1.0f,float countersteer_gain=1.0f/.65f) {
    const bool reversing=actual*wanted<0;
    float command=(wanted+(wanted-actual)*(reversing?countersteer_gain:response_gain))/full_rate;
    // When closing faster than the stopping envelope permits, actively counter-steer.
    if(std::abs(wanted)<.001f || actual*std::copysign(1.0f,wanted)>std::abs(wanted)+3.0f)
        command=(wanted-actual)*countersteer_gain/full_rate;
    return std::clamp(command,-limit,limit);
}
inline float arrival_command(float error,float actual,float max_rate,float deceleration,
                             float gain,float zone,float full_rate,float limit,float delay=0.15f) {
    float wanted=arrival_rate(error,max_rate,deceleration,gain,zone,delay);
    // Preserve the established dive/legacy response, including reversal gain.
    float command=wanted/full_rate+(wanted-actual)/full_rate;
    if(std::abs(error)<=zone || actual*std::copysign(1.0f,error)>std::abs(wanted)+3.0f)
        command=(wanted-actual)/(full_rate*.65f);
    return std::clamp(command,-limit,limit);
}
}
