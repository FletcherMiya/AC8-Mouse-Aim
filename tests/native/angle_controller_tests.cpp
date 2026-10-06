#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include "angle_guidance.h"
#include "pw5_turn_reference.h"
using namespace flight;

float separation(const Basis& b,V aim) { return std::acos(std::clamp(dot(b.f,aim),-1.0f,1.0f))/rad; }
void primitives() {
    ResponseSettings c;
    assert(c.angle_control);
    assert(angle_axis(90,100,30,.15f).command==1); // Above old ordinary pitch ceiling.
    assert(angle_axis(90,150,30,.15f).command==1); // Above old high-G ceiling.
    assert(angle_axis(5,100,30,.15f).command<0);
    assert(angle_axis(5,-100,30,.15f).command>0);
    assert(angle_axis(0,100,30,.15f).command<0);
    assert(angle_axis(.1f,0,30,.15f).command==0);
    assert(angle_axis(-90,0,30,.15f).command== -1);
    const auto b=basis(0,0,0); BankGuidance g; BankSettings bank;
    auto aim=basis(80,0,0).f;
    auto d=g.step(b,aim,separation(b,aim),1.f/60,bank,c);
    assert(angle_turn(b,aim,d,150,0,0,c).pitch.command==1);
    d.diving=true;
    assert(angle_turn(b,aim,d,150,0,0,c).pitch.command==1);
    d.error=160;
    assert(angle_roll(d,250,c).command==1);
    c.turn_rate_scale=.7f; c.roll_rate_scale=.7f; c.high_g_pitch_rate=54;
    c.response_gain=.8f; c.countersteer_gain=3;
    assert(angle_turn(b,aim,d,150,0,0,c).pitch.command==1);
    assert(angle_roll(d,250,c).command==1); // Legacy settings cannot impose a hidden cap.
    for(float p:{-90.f,-40.f,0.f,40.f,90.f}) for(float y:{-180.f,-60.f,0.f,60.f,180.f})
    for(float r:{-179.f,-89.f,0.f,89.f,179.f}) for(float v:{-400.f,0.f,400.f}) {
        BankGuidance guide; const auto frame=basis(p,0,r); aim=basis(-p,y,0).f;
        d=guide.step(frame,aim,separation(frame,aim),1.f/60,bank,c);
        const auto t=angle_turn(frame,aim,d,v,v,v,c); const auto s=angle_roll(d,v,c);
        assert(std::isfinite(t.pitch.command) && std::isfinite(t.yaw.command) && std::isfinite(s.command));
        assert(std::abs(t.pitch.command)<=1 && std::abs(t.yaw.command)<=1 && std::abs(s.command)<=1);
    }
    puts("PASS angle input saturation, anticipatory braking, dead zone, no legacy caps and finite poses");
}
void legacy_equivalence() {
    ResponseSettings c; c.angle_control=false;
    BankSettings settings; pw5_reference::BankSettings old_settings;
    BankGuidance g; pw5_reference::BankGuidance old_g;
    for(int high:{0,1}) for(float p:{-85.f,-40.f,0.f,40.f,85.f})
    for(float y:{-179.f,-60.f,0.f,60.f,179.f}) for(float roll:{-170.f,-85.f,0.f,85.f,170.f}) {
        const auto b=basis(p,10,roll); const auto aim=basis(-p,y,0).f;
        c.high_g_requested=high!=0; g.reset(); old_g.reset();
        for(int i=0;i<5;++i) {
            const auto bank=g.step(b,aim,separation(b,aim),1.f/60,settings,c);
            const auto old=old_g.step(b,aim,separation(b,aim),1.f/60,old_settings,c);
            assert(bank.target==old.target && bank.error==old.error && bank.diving==old.diving);
            const auto a=coordinated_turn(b,aim,bank,65,-5,100,c);
            const auto z=pw5_reference::coordinated_turn(b,aim,old,65,-5,100,c);
            assert(a.pitch_command==z.pitch_command && a.yaw_command==z.yaw_command);
            assert(roll_command(bank,100,c,5)==pw5_reference::roll_command(old,100,c,5));
            assert(dive_pitch_command(30,65,.5f,c)==pw5_reference::dive_pitch_command(30,65,.5f,c));
        }
    }
    puts("PASS legacy mode exact outputs against frozen pw.5 guidance");
}
void flight_regressions() {
    ResponseSettings c; BankSettings limits; limits.max_bank=89;
    BankGuidance g; const auto b=basis(0,0,0);
    const auto small=g.step(b,basis(0,7,0).f,7,1.f/60,limits,c);
    assert(small.target<=25 && small.roll_activity==0 && angle_roll(small,0,c).command<.3f);
    g.reset(); const auto large=g.step(b,basis(0,90,0).f,90,1.f/60,limits,c);
    assert(large.roll_activity==1 && angle_roll(large,0,c).command==1);
    auto mid=large; mid.error=30;
    assert(angle_roll(mid,0,c).command>.85f);
    mid.error=3; assert(angle_roll(mid,80,c).command<-.55f);
    g.reset(); const auto committed=g.step(basis(0,0,80),basis(0,7,0).f,7,1.f/60,limits,c);
    assert(committed.roll_activity==1); // Arrival must not suddenly select the gentle roll profile.

    // F-14D pw.6 log MouseAim-00009, line 881: at 2.29 degrees from
    // a fixed target, 68.60 deg/s persists with only -0.354 pitch input.
    c.high_g_requested=true;
    BankDemand arrival{-84.18f,0,84.18f,0,false,0};
    const auto brake=angle_turn(basis(2.72f,86.45f,-84.18f),basis(.43f,86.64f,0).f,
        arrival,68.60f,-7.27f,1.10f,c);
    assert(brake.pitch.command<-.95f);
    // Line 12912: aligned, target still 55.61 degrees away, but yaw saturation
    // reduced pitch to +0.302. Distant pitch may use its remaining authority.
    BankDemand aligned{-86.46f,-87.17f,-.71f,1,false,0};
    const auto turn=angle_turn(basis(7.55f,161.73f,-86.46f),basis(5.21f,105.79f,0).f,
        aligned,76.30f,-4.43f,-5.60f,c);
    assert(turn.pitch.command>.45f && turn.pitch_priority>.2f);
    aligned.current=-95.96f; aligned.target=-83.53f; aligned.error=12.43f;
    const auto inverted=angle_turn(basis(1.72f,-113.54f,-95.96f),basis(5.21f,105.79f,0).f,
        aligned,67.33f,2.19f,3.54f,c);
    assert(inverted.pitch_priority==0);
    c.high_g_requested=false;
    const auto normal=angle_turn(basis(2.72f,86.45f,-84.18f),basis(.43f,86.64f,0).f,
        arrival,68.60f,-7.27f,1.10f,c);
    assert(std::abs(normal.pitch.command+.354f)<.01f);
    HighGBraking state;
    assert(state.step(true,1.f/60,c.high_g_brake_hold)==1);
    c.high_g_brake_weight=state.step(false,.1f,c.high_g_brake_hold);
    assert(c.high_g_brake_weight>.75f);
    const auto release=angle_turn(basis(2.72f,86.45f,-84.18f),basis(.43f,86.64f,0).f,
        arrival,68.60f,-7.27f,1.10f,c);
    assert(release.pitch.command<-.8f);
    assert(state.step(false,.5f,c.high_g_brake_hold)==0);
    state.step(true,.01f,c.high_g_brake_hold); state.reset();
    assert(state.step(false,.01f,c.high_g_brake_hold)==0);
    state.step(true,.016f,.45f,15,0);
    assert(!state.settling()); // A fresh small high-G request gets no artificial input delay.
    state.step(true,.016f,.45f,90,70);
    state.step(true,.016f,.45f,20,50); assert(!state.settling());
    state.step(true,.016f,.45f,10,-20); assert(state.settling()); // Crossed zero between samples.
    state.step(true,.016f,.45f,20,50); assert(state.settling()); // No repeated strong brake/rebound.
    state.step(true,.016f,.45f,31,50); assert(!state.settling()); // New distant command releases finish.
    state.step(false,1,.45f,0,0); assert(!state.settling());
    puts("PASS flight-log regression: gentle corrections, committed roll, high-G arrival/release braking and distant pitch allocation");
}
struct Result { float settle=0,error=0,peak_pitch=0,peak_bank=0,peak_rate=0,late_error=0,overshoot=0; };
// Synthetic lagged input response, not an AC8 flight model or performance claim.
Result simulate(bool angle_mode,float dt,float lag,float gain,int maneuver,int high_mode,int profile,float sign=1) {
    ResponseSettings c; c.angle_control=angle_mode;
    BankSettings settings;
    if(profile) { settings.max_bank=89; c.roll_lookahead=.12f; c.roll_rate_scale=1.35f; c.high_g_yaw_boost=true; }
    BankGuidance g; HighGBraking braking;
    const float initial_pitch=maneuver==7?85.f:0;
    Basis b=basis(initial_pitch,0,0);
    V aim=basis(maneuver==1?35.f:maneuver==2?-35.f:maneuver==5?-60.f:maneuver==6?70.f:maneuver==7?20.f:0,
                (maneuver==3?179.f:maneuver==6?0.f:90.f)*sign,0).f;
    float q=0,r=0,s=0,fq=0,fr=0,fs=0; Result out;
    for(int i=0;i<int(20/dt);++i) {
        const float t=i*dt;
        if(maneuver==4 && t>=2) aim=basis(0,-90*sign,0).f;
        c.high_g_requested=high_mode!=0 && (high_mode!=2 || t<2);
        if(angle_mode) {
            c.high_g_brake_weight=braking.step(c.high_g_requested,dt,c.high_g_brake_hold,separation(b,aim),fq);
            c.high_g_settling=braking.settling();
        }
        const bool game_high=c.high_g_requested && high_mode!=3 && (high_mode!=4 || t>=.35f);
        const auto bank=g.step(b,aim,separation(b,aim),dt,settings,c);
        const float drift=horizon_roll_drift(b,fq,fr);
        float pc,yc,rc;
        if(angle_mode) {
            const auto turn=angle_turn(b,aim,bank,fq,fr,fs+drift,c);
            pc=turn.pitch.command; yc=turn.yaw.command; rc=angle_roll(bank,fs,c,drift).command;
        } else {
            const auto turn=coordinated_turn(b,aim,bank,fq,fr,fs+drift,c);
            pc=turn.pitch_command; yc=turn.yaw_command; rc=roll_command(bank,fs,c,drift);
            if(bank.diving) {
                const float forward=std::max(.02f,dot(aim,b.f));
                pc=dive_pitch_command(std::atan2(dot(aim,b.u),forward)/rad,fq,1-tracking_weight(separation(b,aim)),c);
                const float yr=std::clamp(dead(std::atan2(dot(aim,b.r),forward)/rad,.2f)*(1.2f+.6f*(1-tracking_weight(separation(b,aim)))),-7.f,7.f);
                yc=std::clamp((yr-fr*.6f)/10,-.7f,.7f);
            }
        }
        assert(std::isfinite(pc) && std::isfinite(yc) && std::isfinite(rc));
        assert(std::max({std::abs(pc),std::abs(yc),std::abs(rc)})<=1);
        const float response=1-std::exp(-dt/lag);
        q+=(pc*(game_high?110.f:55.f)*gain-q)*response;
        // Also exercise changing pitch authority without proportional rudder improvement.
        r+=(yc*10-r)*response; s+=(rc*190-s)*response;
        const V omega=b.r*(-q)+b.u*r-b.f*s;
        const float speed=std::sqrt(dot(omega,omega));
        if(speed>1e-5f) {
            const auto axis=omega*(1/speed);
            b={rotate(b.f,axis,speed*dt*rad),rotate(b.r,axis,speed*dt*rad),rotate(b.u,axis,speed*dt*rad)};
        }
        const float filter=1-std::exp(-12*dt);
        fq+=(q-fq)*filter; fr+=(r-fr)*filter; fs+=(s-fs)*filter;
        const float error=separation(b,aim);
        if(error>3 || std::max({std::abs(q),std::abs(r),std::abs(s)})>5) out.settle=t;
        out.peak_pitch=std::max(out.peak_pitch,std::abs(pitch(b.f)));
        out.peak_bank=std::max(out.peak_bank,std::abs(std::atan2(-b.r.z,b.u.z)/rad));
        out.peak_rate=std::max(out.peak_rate,std::abs(q));
        if(t>15) out.late_error=std::max(out.late_error,error);
        if(maneuver==0) out.overshoot=std::max(out.overshoot,sign*yaw(b.f)-90);
        if(maneuver==6) out.overshoot=std::max(out.overshoot,pitch(b.f)-70);
        out.error=error;
    }
    return out;
}
void dynamics() {
    int count=0,failures=0; float before_sum=0,after_sum=0,peak_bank=0,peak_level_pitch=0,worst_over=0;
    for(float dt:{1.f/30,1.f/60,1.f/144}) for(float lag:{.1f,.25f}) for(float gain:{.7f,1.f,1.8f})
    for(int mode:{0,1,2,3,4}) for(int maneuver=0;maneuver<8;++maneuver) for(int profile:{0,1}) for(float sign:{-1.f,1.f}) {
        const auto a=simulate(true,dt,lag,gain,maneuver,mode,profile,sign);
        const auto old=simulate(false,dt,lag,gain,maneuver,mode,profile,sign);
        ++count; before_sum+=old.settle; after_sum+=a.settle;
        const bool level=maneuver==0 || maneuver==3 || maneuver==4;
        if(level) { peak_level_pitch=std::max(peak_level_pitch,a.peak_pitch); peak_bank=std::max(peak_bank,a.peak_bank); }
        worst_over=std::max(worst_over,a.overshoot);
        const bool bad=a.error>=3 || a.settle>=19 || a.late_error>=1 ||
            (level && (a.peak_pitch>=5 || a.peak_bank>=90)) || a.overshoot>5;
        if(bad) {
            if(failures++<24) printf("ANGLE FAIL dt=%.4f lag=%.2f gain=%.1f move=%d high=%d profile=%d error=%.2f late=%.2f settle=%.2f pitch=%.2f bank=%.2f over=%.2f old=%.2f\n",
                dt,lag,gain,maneuver,mode,profile,a.error,a.late_error,a.settle,a.peak_pitch,a.peak_bank,a.overshoot,old.settle);
        }
    }
    printf("ANGLE MATRIX cases=%d failures=%d oldMean=%.3f angleMean=%.3f peakLevelPitch=%.2f peakLevelBank=%.2f maxOvershoot=%.2f\n",
        count,failures,before_sum/count,after_sum/count,peak_level_pitch,peak_bank,worst_over);
    assert(failures==0);
    for(float dt:{1.f/30,1.f/60,1.f/144}) for(float lag:{.1f,.25f}) for(int high:{0,1}) {
        const auto normal=simulate(true,dt,lag,1,6,high,1),strong=simulate(true,dt,lag,1.8f,6,high,1);
        assert(strong.peak_rate>normal.peak_rate*1.2f && strong.settle<normal.settle);
    }
    puts("PASS angle 3-axis matrix, high-G delay/unavailable/release, personal preset and aircraft response differences");
}
struct PitchResult { float overshoot=0, late_error=0; };
PitchResult delayed_pitch(bool enhanced,float dt,float lag,float delay,float gain,bool release) {
    ResponseSettings c; HighGBraking braking;
    if(!enhanced) { c.high_g_brake_lookahead=.1f; c.high_g_brake_gain=1; c.high_g_brake_hold=0; }
    std::deque<float> pending(size_t(std::lround(delay/dt)),0);
    float p=0,q=0,filtered=0; bool released=false; PitchResult result;
    for(int i=0;i<int(12/dt);++i) {
        const float t=i*dt;
        if(release && p>80) released=true;
        c.high_g_requested=!released;
        c.high_g_brake_weight=braking.step(c.high_g_requested,dt,c.high_g_brake_hold,std::abs(90-p),filtered);
        c.high_g_settling=enhanced && braking.settling();
        // Hold the aircraft side-on so a long pitch-driven turn crosses no
        // Euler pitch pole. Roll/yaw dynamics are covered by the 3-axis matrix.
        const auto turn=angle_turn(basis(0,p,90),basis(0,90,0).f,BankDemand{90,90,0,1},filtered,0,0,c);
        if(t<.35f) assert(turn.pitch.command==1); // Game activation delay adds NO mod wait.
        pending.push_back(turn.pitch.command); const float applied=pending.front(); pending.pop_front();
        const bool actual_high=c.high_g_requested && t>=.35f;
        q+=(applied*(actual_high?110.f:55.f)*gain-q)*(1-std::exp(-dt/lag));
        p+=q*dt; filtered+=(q-filtered)*(1-std::exp(-12*dt));
        result.overshoot=std::max(result.overshoot,p-90);
        if(t>9) result.late_error=std::max(result.late_error,std::abs(p-90));
    }
    return result;
}
void delayed_dynamics() {
    int count=0,failures=0; float before_sum=0,after_sum=0,old_peak=0,new_peak=0;
    for(float dt:{1.f/30,1.f/60,1.f/144}) for(float lag:{.15f,.35f,.5f})
    for(float delay:{0.f,.08f,.15f}) for(float gain:{.7f,1.f,1.8f}) for(bool release:{false,true}) {
        const auto old=delayed_pitch(false,dt,lag,delay,gain,release);
        const auto next=delayed_pitch(true,dt,lag,delay,gain,release);
        before_sum+=old.overshoot; after_sum+=next.overshoot; ++count;
        old_peak=std::max(old_peak,old.overshoot); new_peak=std::max(new_peak,next.overshoot);
        if(next.late_error>1 || next.overshoot>old.overshoot+.5f) {
            if(failures++<12) printf("DELAY FAIL dt=%.4f lag=%.2f delay=%.2f gain=%.1f release=%d before=%.2f after=%.2f late=%.2f\n",
                dt,lag,delay,gain,release,old.overshoot,next.overshoot,next.late_error);
        }
    }
    printf("DELAY MATRIX cases=%d failures=%d meanOvershoot=%.2f->%.2f peakOvershoot=%.2f->%.2f\n",
        count,failures,before_sum/count,after_sum/count,old_peak,new_peak);
    assert(failures==0 && after_sum<before_sum*.7f);
}
int main() {
    _set_error_mode(_OUT_TO_STDERR); setvbuf(stdout,nullptr,_IONBF,0);
    primitives(); legacy_equivalence(); flight_regressions(); dynamics(); delayed_dynamics();
}
