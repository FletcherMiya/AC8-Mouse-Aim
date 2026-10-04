#include <cassert>
#include <cstdio>
#include <cstdlib>
#include "bank_guidance.h"
#include "turn_guidance.h"
#include "key_bindings.h"
#include "free_look.h"
#include "pose_guard.h"
#include "pw2_turn_reference.h"
#include "pw3_turn_reference.h"
using namespace flight;
float angle_to(const Basis& b,V aim) { return std::acos(std::clamp(dot(b.f,aim),-1.0f,1.0f))/rad; }
BankDemand demand(BankGuidance& g,Basis b,V aim,const BankSettings& c,float dt=1.f/60) {
    return g.step(b,aim,angle_to(b,aim),dt,c);
}
void normal_and_recovery() {
    BankSettings c; c.max_bank=65;
    for (float roll: {-170.f,-120.f,-100.f,0.f,100.f,120.f,170.f}) {
        for (float yaw: {-170.f,-60.f,0.f,60.f,170.f}) {
            BankGuidance g;
            auto d=demand(g,basis(0,0,roll),basis(-5,yaw,0).f,c);
            assert(!d.diving && std::abs(d.target)<=65);
            if (roll>90) assert(d.error<0);
            if (roll< -90) assert(d.error>0);
        }
    }
    BankGuidance g;
    auto d=demand(g,basis(0,0,120),basis(-4.981f,8.682f,0).f,c);
    assert(d.error < -55); // Original controller's inverted equilibrium.
    g.reset(); d=demand(g,basis(0,0,0),basis(-10,0,0).f,c);
    assert(std::abs(d.target)<.001 && !d.diving);
    g.reset(); demand(g,basis(0,0,179),basis(0,45,0).f,c);
    d=demand(g,basis(0,0,-179),basis(0,45,0).f,c);
    assert(d.current>180 && d.error<0); // Inertia must not change recovery route.
    puts("PASS normal bank limit, shallow descent and inverted recovery");
}
void dive_modes() {
    BankSettings c; c.max_bank=65; BankGuidance g;
    auto d=demand(g,basis(0,0,0),basis(-60,.1f,0).f,c);
    assert(d.diving && d.target>170);
    d=demand(g,basis(0,0,179),basis(-60,-.1f,0).f,c);
    assert(d.diving && d.target>170 && std::abs(d.error)<2);
    d=demand(g,basis(0,0,-179),basis(-60,.1f,0).f,c);
    assert(d.diving && d.current>180 && std::abs(d.error)<2);
    d=demand(g,basis(0,0,120),basis(-40,0,0).f,c);
    assert(d.diving); // Hysteresis between -45 and -30.
    d=demand(g,basis(0,0,120),basis(-29,0,0).f,c);
    assert(!d.diving && d.error<0);
    g.reset(); demand(g,basis(0,0,0),basis(-60,0,0).f,c);
    d=demand(g,basis(-60,0,179),basis(-60,0,0).f,c);
    assert(!d.diving && d.error<0); // Exit on arrival.
    g.reset(); c.allow_dive_inversion=false;
    d=demand(g,basis(0,0,0),basis(-70,0,0).f,c);
    assert(!d.diving && std::abs(d.target)<=65);
    puts("PASS steep-dive entry, direction lock, hysteresis and exit");
}
void poles_and_rear() {
    for (float dt: {1.f/30,1.f/60,1.f/144}) {
        BankGuidance g; BankSettings c; c.max_bank=65; c.allow_dive_inversion=false;
        float previous=0; bool first=true;
        for (float p=70;p<=110;p+=.25f) {
            auto d=demand(g,basis(p,0,30),basis(-20,180,0).f,c,dt);
            assert(std::isfinite(d.error) && std::abs(d.target)<=65);
            if (!first) assert(std::abs(d.current-previous)<120*dt+1);
            first=false; previous=d.current;
        }
        g.reset(); auto d=demand(g,basis(90,0,0),basis(-90,180,0).f,c,dt);
        assert(std::isfinite(d.error));
        const auto turn=coordinated_turn(basis(90,0,0),basis(-90,180,0).f,d,0,0);
        assert(std::isfinite(turn.pitch_command) && std::isfinite(turn.yaw_command));
        g.reset(); d=demand(g,basis(0,0,0),basis(0,180,0).f,c,dt);
        assert(std::isfinite(d.error) && std::abs(d.target)<=65);
    }
    puts("PASS pole crossing and rear-target finite/stable commands at 30/60/144 Hz");
}
void braking_response() {
    // Synthetic response checks braking, NOT a model of any particular AC8 plane.
    for (float dt: {1.f/30,1.f/60,1.f/144}) for (float speed: {0.f,70.f,140.f}) {
        float roll=50, rate=speed, peak=roll; BankGuidance g; BankSettings c; c.max_bank=65;
        for (int i=0;i<int(8/dt);++i) {
            auto d=demand(g,basis(0,0,roll),basis(0,90,0).f,c,dt);
            float cmd=roll_command(d,rate,ResponseSettings{});
            if (i==0 && speed==140) assert(cmd<0);
            rate += std::clamp((cmd*170-rate)/.15f,-500.f,500.f)*dt;
            roll+=rate*dt; peak=std::max(peak,roll);
        }
        assert(peak<90 && std::abs(roll-65)<2 && std::abs(rate)<2);
    }
    puts("PASS synthetic roll braking and settling at 30/60/144 Hz");
}
void keyboard_config() {
    using namespace input;
    Bindings keys; assert(unique(keys));
    assert(key_code(L" f24 ")==0x87 && key_code(L"numpad0")==0x60);
    assert(key_code(L"Ctrl")==0x11 && key_code(L"f1")==0x70);
    for (auto bad: {L"",L"F25",L"F01",L"F1X",L"Ctrl+F",L"Mouse4",L"0x20"}) assert(!key_code(bad));
    assert(parse_field(keys,fields[0],L"up, down"));
    assert(keys[Pitch1]==0x26 && keys[Pitch2]==0x28);
    assert(!parse_field(keys,fields[0],L"W,S,A"));
    assert(!parse_field(keys,fields[0],L"W"));
    assert(!parse_field(keys,fields[3],L"F,G"));
    keys.keys[input::FreeLook]=keys[Pitch1]; assert(!unique(keys));
    keys=Bindings{}; keys.keys[input::FreeLook]=0x11; keys.keys[Perf]=0xa2; assert(!unique(keys));
    keys=Bindings{}; keys.keys[Pitch1]=0x26;
    auto axes=manual_axes(keys,[](int key){return key==0x26;});
    assert(axes.pitch && axes.roll && !axes.yaw);
    axes=manual_axes(keys,[](int key){return key=='W';});
    assert(!axes.pitch && !axes.roll && !axes.yaw);
    axes=manual_axes(keys,[](int key){return key=='Q';});
    assert(!axes.pitch && !axes.roll && axes.yaw);
    puts("PASS keyboard names, conflicts and remapped manual axes");
}
void free_look_regression() {
    FreeLook look; V aim=basis(0,20,0).f, before=aim; Basis view=basis(0,0,0);
    look.step(true,aim,view,100,100); // Press boundary is discarded.
    auto camera=look.step(true,aim,view,10,5);
    assert(dot(aim,before)>.99999 && dot(camera,view.f)<.999);
    look.step(false,aim,view,100,100); // Release boundary is discarded.
    assert(dot(aim,before)>.99999);
    puts("PASS free-look target persistence and transition deltas");
}
void turn_allocation() {
    BankSettings c;
    for (float sign: {-1.f,1.f}) for (float roll: {0.f,26.f,45.f,65.f,85.f}) {
        BankGuidance g; const auto b=basis(0,0,roll*sign); const auto aim=basis(0,60*sign,0).f;
        auto bank=demand(g,b,aim,c);
        const auto turn=coordinated_turn(b,aim,bank,0,0);
        const float elevation=turn.pitch_rate*std::cos(roll*sign*rad)-turn.yaw_rate*std::sin(roll*sign*rad);
        assert(std::abs(elevation)<.001 && std::abs(turn.yaw_rate)<=8.401);
        if(roll<=45) assert(std::abs(turn.pitch_rate)<.001); // Roll before horizontal pull.
        if(roll==85) assert(turn.pitch_rate>40 && turn.horizontal_rate*sign>40);
    }
    // A user retaining 65 degrees gets a slower, coordinated turn, not climb.
    c.max_bank=65; BankGuidance g; const auto b=basis(0,0,65); const auto aim=basis(0,60,0).f;
    auto bank=demand(g,b,aim,c); auto turn=coordinated_turn(b,aim,bank,0,0);
    assert(std::abs(turn.vertical_rate)<.001 && turn.horizontal_rate<20);
    assert(std::abs(turn.pitch_rate*std::cos(65*rad)-turn.yaw_rate*std::sin(65*rad))<.001);
    g.reset(); bank=demand(g,basis(0,0,0),basis(20,0,0).f,c);
    turn=coordinated_turn(basis(0,0,0),basis(20,0,0).f,bank,0,0);
    assert(turn.pitch_rate>0 && std::abs(turn.yaw_rate)<.001); // Intentional climb retained.
    puts("PASS coordinated horizontal rates, roll-first gating and intentional climb");
}
void turn_dynamics() {
    // Integrate all three body rotations with a synthetic lagged response.
    // This tests geometric coupling; it is not an AC8 flight-model prediction.
    for(float dt: {1.f/30,1.f/60,1.f/144}) for(float sign: {-1.f,1.f}) for(float pitch_gain: {.7f,1.f,1.8f}) {
        Basis b=basis(0,0,0); BankSettings cfg; BankGuidance g;
        float q=0,r=0,s=0,fq=0,fr=0,fs=0,max_pitch=0,max_bank=0;
        const V aim=basis(0,60*sign,0).f;
        for(int i=0;i<int(14/dt);++i) {
            const auto bank=demand(g,b,aim,cfg,dt);
            const auto turn=coordinated_turn(b,aim,bank,fq,fr,fs+horizon_roll_drift(b,fq,fr));
            const float roll_cmd=roll_command(bank,fs,ResponseSettings{},horizon_roll_drift(b,fq,fr));
            const float response=1-std::exp(-dt/.15f);
            q+=(turn.pitch_command*55*pitch_gain-q)*response;
            r+=(turn.yaw_command*10-r)*response;
            s+=(roll_cmd*170-s)*response;
            V axis=(b.r*(-q)+b.u*r-b.f*s);
            const float speed=std::sqrt(dot(axis,axis));
            if(speed>1e-5f) {
                axis=axis*(1/speed);
                b={rotate(b.f,axis,speed*dt*rad),rotate(b.r,axis,speed*dt*rad),rotate(b.u,axis,speed*dt*rad)};
            }
            const float a=1-std::exp(-12*dt);
            fq+=(q-fq)*a; fr+=(r-fr)*a; fs+=(s-fs)*a;
            max_pitch=std::max(max_pitch,std::abs(pitch(b.f)));
            max_bank=std::max(max_bank,std::abs(std::atan2(-b.r.z,b.u.z)/rad));
        }
        printf("TURN dt=%.4f sign=%.0f pitchGain=%.1f peakPitch=%.2f peakBank=%.2f finalError=%.2f\n",dt,sign,pitch_gain,max_pitch,max_bank,angle_to(b,aim));
        assert(max_pitch<5 && max_bank<90 && angle_to(b,aim)<3);
    }
    puts("PASS synthetic 3-axis turns at 30/60/144 Hz without climb/inversion");
}
void predictive_response() {
    BankSettings c; BankGuidance g; ResponseSettings cfg;
    const auto b=basis(0,0,50); const V aim=basis(0,60,0).f;
    const auto bank=demand(g,b,aim,c);
    const auto still=coordinated_turn(b,aim,bank,0,0,0,cfg);
    const auto toward=coordinated_turn(b,aim,bank,0,0,100,cfg);
    const auto away=coordinated_turn(b,aim,bank,0,0,-100,cfg);
    assert(toward.roll_gate>still.roll_gate && away.roll_gate==still.roll_gate);
    assert(toward.roll_advance<=12 && toward.roll_advance<=std::abs(bank.error));
    assert(std::abs(toward.pitch_rate*std::cos(50*rad)-toward.yaw_rate*std::sin(50*rad))<.001);
    const float gentle=rate_command(0,20,55,1,1,1/.65f);
    const float strong=rate_command(0,20,55,1,cfg.response_gain,cfg.countersteer_gain);
    assert(strong<gentle && strong>=-1); // Brakes on existing velocity, no target integral.
    assert(rate_command(-10,20,55,1,cfg.response_gain,cfg.countersteer_gain)<strong);
    assert(std::abs(rate_command(0,0,55,1,cfg.response_gain,cfg.countersteer_gain))<.001);
    const BankDemand near_bank{83,85,2,1,false};
    assert(roll_command(near_bank,100,cfg)<0); // Brake before crossing the bank target.
    cfg.turn_rate_scale=1.4f; cfg.roll_rate_scale=1.4f; cfg.response_gain=1.6f;
    cfg.countersteer_gain=3; cfg.roll_lookahead=.15f;
    for(float roll: {-170.f,-85.f,0.f,85.f,170.f}) for(float velocity: {-300.f,0.f,300.f}) {
        BankGuidance limits; const auto frame=basis(0,0,roll);
        const auto d=demand(limits,frame,aim,c);
        const auto t=coordinated_turn(frame,aim,d,velocity,velocity,velocity,cfg);
        assert(std::isfinite(t.pitch_command) && std::isfinite(t.yaw_command));
        assert(std::abs(t.pitch_command)<=1 && std::abs(t.yaw_command)<=1 && std::abs(roll_command(d,velocity,cfg))<=1);
        if(std::abs(roll)>90) assert(t.roll_advance==0);
    }
    puts("PASS predictive gate direction, current-attitude allocation and stronger countersteer");
}
struct MotionResult { float settled,peak_pitch,peak_bank,error; };
MotionResult simulate(bool previous,int maneuver,float dt,float lag,float pitch_gain,float yaw_gain,float roll_gain,float sign) {
    Basis b=basis(0,0,0); BankSettings cfg; BankGuidance g; ResponseSettings response;
    pw2_reference::BankSettings old_cfg; pw2_reference::BankGuidance old_g;
    float q=0,r=0,s=0,fq=0,fr=0,fs=0,max_pitch=0,max_bank=0,last_unsettled=0;
    V aim=basis(maneuver==3?25.f:0.f,maneuver==3?0.f:60*sign,0).f;
    bool changed=false;
    for(int i=0;i<int(14/dt);++i) {
        const float t=i*dt;
        if(!changed && maneuver==1 && t>=2) { aim=basis(0,-60*sign,0).f; changed=true; }
        if(!changed && maneuver==2 && t>=1.2f) { aim=b.f; changed=true; }
        const auto bank=demand(g,b,aim,cfg,dt);
        float pc,yc,rc;
        if(previous) {
            const auto old_bank=old_g.step(b,aim,angle_to(b,aim),dt,old_cfg);
            const auto turn=pw2_reference::coordinated_turn(b,aim,old_bank,fq,fr);
            pc=turn.pitch_command; yc=turn.yaw_command;
            rc=arrival_command(old_bank.error,fs,70+40*old_bank.blend,120,3.5f,1,170,1,.25f);
        } else {
            const auto turn=coordinated_turn(b,aim,bank,fq,fr,fs+horizon_roll_drift(b,fq,fr),response);
            pc=turn.pitch_command; yc=turn.yaw_command; rc=roll_command(bank,fs,response,horizon_roll_drift(b,fq,fr));
        }
        assert(std::isfinite(pc) && std::isfinite(yc) && std::isfinite(rc));
        assert(std::abs(pc)<=1 && std::abs(yc)<=1 && std::abs(rc)<=1);
        const float a=1-std::exp(-dt/lag);
        q+=(pc*55*pitch_gain-q)*a; r+=(yc*10*yaw_gain-r)*a; s+=(rc*170*roll_gain-s)*a;
        V axis=b.r*(-q)+b.u*r-b.f*s;
        const float speed=std::sqrt(dot(axis,axis));
        if(speed>1e-5f) {
            axis=axis*(1/speed);
            b={rotate(b.f,axis,speed*dt*rad),rotate(b.r,axis,speed*dt*rad),rotate(b.u,axis,speed*dt*rad)};
        }
        const float filter=1-std::exp(-12*dt);
        fq+=(q-fq)*filter; fr+=(r-fr)*filter; fs+=(s-fs)*filter;
        max_pitch=std::max(max_pitch,std::abs(pitch(b.f)));
        max_bank=std::max(max_bank,std::abs(std::atan2(-b.r.z,b.u.z)/rad));
        if(angle_to(b,aim)>3 || std::max({std::abs(q),std::abs(r),std::abs(s)})>5) last_unsettled=t;
    }
    return {last_unsettled,max_pitch,max_bank,angle_to(b,aim)};
}
void response_comparison() {
    float old_time=0,new_time=0,peak_pitch=0,peak_bank=0;
    float old_modes[4]{},new_modes[4]{}; int cases=0;
    for(float dt: {1.f/30,1.f/60,1.f/144}) for(float lag: {.1f,.25f})
    for(float pitch_gain: {.7f,1.f,1.8f}) for(float sign: {-1.f,1.f}) for(int maneuver=0;maneuver<4;++maneuver) {
        const auto before=simulate(true,maneuver,dt,lag,pitch_gain,.8f,1.2f,sign);
        const auto after=simulate(false,maneuver,dt,lag,pitch_gain,.8f,1.2f,sign);
        old_time+=before.settled; new_time+=after.settled; ++cases;
        old_modes[maneuver]+=before.settled; new_modes[maneuver]+=after.settled;
        if(maneuver!=3) peak_pitch=std::max(peak_pitch,after.peak_pitch);
        peak_bank=std::max(peak_bank,after.peak_bank);
        if(after.error>=3 || after.peak_bank>=90 || (maneuver!=3 && after.peak_pitch>=5) || after.settled>=13)
            printf("UNSTABLE mode=%d dt=%.4f lag=%.2f gain=%.1f sign=%.0f settled=%.2f pitch=%.2f bank=%.2f error=%.2f\n",maneuver,dt,lag,pitch_gain,sign,after.settled,after.peak_pitch,after.peak_bank,after.error);
        assert(after.error<3 && after.peak_bank<90 && after.settled<13);
        assert(maneuver==3 ? after.peak_pitch<30 : after.peak_pitch<5);
    }
    printf("COMPARE cases=%d pw2MeanSettle=%.3f currentMeanSettle=%.3f peakLevelPitch=%.2f peakBank=%.2f\n",cases,old_time/cases,new_time/cases,peak_pitch,peak_bank);
    for(int m=0;m<4;++m) {
        printf("COMPARE mode=%d pw2MeanSettle=%.3f currentMeanSettle=%.3f\n",m,old_modes[m]/(cases/4),new_modes[m]/(cases/4));
        assert(new_modes[m]<old_modes[m]);
    }
    assert(new_time<old_time); // Same targets and synthetic plants; no real-aircraft claim.
    puts("PASS step, reversal, sudden target stop and climb against frozen pw.2 baseline");
}
void diagonal_allocation() {
    // Actual F-14D pw.3 log tick 10008468. Old bank direction and desired
    // world-elevation motion disagree while the goal remains 137.6 degrees away.
    const auto b=basis(31.51f,10.34f,31.41f); const auto aim=basis(5.23f,168.14f,0).f;
    pw3_reference::BankGuidance old_g; pw3_reference::BankSettings old_c;
    const auto old_bank=old_g.step(b,aim,angle_to(b,aim),1.f/60,old_c);
    const auto old=pw3_reference::coordinated_turn(b,aim,old_bank,.25f,4.67f,4.38f);
    BankGuidance g; BankSettings c; ResponseSettings cfg;
    const auto bank=g.step(b,aim,angle_to(b,aim),1.f/60,c,cfg);
    assert(old_bank.target<40 && old.rate_scale<.15f && std::abs(old.pitch_rate)<3);
    assert(bank.target>75); // Lift axis now points toward the requested path.
    for(float sign: {-1.f,1.f}) for(float elevation: {-35.f,35.f,60.f}) {
        BankGuidance guide; const auto target=basis(elevation,90*sign,0).f;
        auto d=guide.step(basis(0,0,0),target,angle_to(basis(0,0,0),target),1.f/60,c,cfg);
        const auto aligned=basis(0,0,d.target);
        d=guide.step(aligned,target,angle_to(aligned,target),1.f/60,c,cfg);
        auto turn=coordinated_turn(aligned,target,d,0,0,0,cfg);
        assert(std::abs(turn.pitch_rate)>30 && std::abs(turn.pitch_command)>.9f);
        assert(turn.horizontal_rate*sign>0 && turn.vertical_rate*elevation>=-.001f);
    }
    cfg.high_g_requested=true; g.reset();
    auto hg_bank=g.step(basis(0,0,85),basis(0,90,0).f,90,1.f/60,c,cfg);
    auto high=coordinated_turn(basis(0,0,85),basis(0,90,0).f,hg_bank,65,0,0,cfg);
    auto normal=coordinated_turn(basis(0,0,85),basis(0,90,0).f,hg_bank,65,0,0,ResponseSettings{});
    assert(high.pitch_rate>90 && high.pitch_command>.99f && normal.pitch_command<0);
    assert(dive_pitch_command(90,65,0,cfg)>.99f);
    input::Bindings keys;
    assert(!input::high_g_requested(keys,[](int key){return key==0x11;}));
    assert(input::high_g_requested(keys,[](int key){return key==0x11 || key==0x20;}));
    assert(input::parse_field(keys,input::fields[10],L"H,J"));
    assert(!input::high_g_requested(keys,[](int key){return key==0x11 || key==0x20;}));
    assert(input::high_g_requested(keys,[](int key){return key=='H' || key=='J';}));
    puts("PASS logged diagonal stall, shared bank path, pitch authority and observed high-G chord");
}
struct DiagonalResult { float settle,error,peak_bank,peak_pitch_rate,peak_pitch; };
DiagonalResult diagonal_simulate(bool previous,float initial_pitch,float target_pitch,float target_yaw,
                                 float dt,float lag,float gain,int high_g_mode,bool yaw_boost=false,bool refinements=true) {
    Basis b=basis(initial_pitch,0,0); const V aim=basis(target_pitch,target_yaw,0).f;
    BankGuidance g; BankSettings c; ResponseSettings cfg;
    cfg.high_g_yaw_boost=yaw_boost; cfg.rear_turn_hold=cfg.dive_pitch_boost=refinements;
    pw3_reference::BankGuidance old_g; pw3_reference::BankSettings old_c;
    float q=0,r=0,s=0,fq=0,fr=0,fs=0,settle=0,peak_bank=0,peak_rate=0,peak_pitch=0;
    for(int i=0;i<int(20/dt);++i) {
        const float time=i*dt;
        const bool high=high_g_mode==1 || high_g_mode==3 || (high_g_mode==2 && time<2);
        cfg.high_g_requested=high;
        const float angle=angle_to(b,aim);
        float pc,yc,rc;
        if(previous) {
            const auto d=old_g.step(b,aim,angle,dt,old_c);
            const auto turn=pw3_reference::coordinated_turn(b,aim,d,fq,fr,fs);
            pc=turn.pitch_command; yc=turn.yaw_command; rc=pw3_reference::roll_command(d,fs,ResponseSettings{});
            if(d.diving) {
                pc=arrival_command(std::atan2(dot(aim,b.u),std::max(.02f,dot(aim,b.f)))/rad,fq,45,90,1.8f+.8f*(1-tracking_weight(angle)),.2f,55,.85f);
                const float ye=std::atan2(dot(aim,b.r),std::max(.02f,dot(aim,b.f)))/rad;
                const float yr=std::clamp(dead(ye,.2f)*(1.2f+.6f*(1-tracking_weight(angle))),-7.f,7.f);
                yc=std::clamp((yr-fr*.6f)/10,-.7f,.7f);
            }
        } else {
            const auto d=g.step(b,aim,angle,dt,c,cfg);
            const auto turn=coordinated_turn(b,aim,d,fq,fr,fs+horizon_roll_drift(b,fq,fr),cfg);
            pc=turn.pitch_command; yc=turn.yaw_command; rc=roll_command(d,fs,cfg,horizon_roll_drift(b,fq,fr));
            if(d.diving) {
                pc=dive_pitch_command(std::atan2(dot(aim,b.u),std::max(.02f,dot(aim,b.f)))/rad,fq,1-tracking_weight(angle),cfg);
                const float ye=std::atan2(dot(aim,b.r),std::max(.02f,dot(aim,b.f)))/rad;
                const float yr=std::clamp(dead(ye,.2f)*(1.2f+.6f*(1-tracking_weight(angle))),-7.f,7.f);
                yc=std::clamp((yr-fr*.6f)/10,-.7f,.7f);
            }
        }
        assert(std::isfinite(pc) && std::abs(pc)<=1 && std::abs(yc)<=1 && std::abs(rc)<=1);
        // Also test requesting high G when the game does NOT supply extra authority.
        const float full_pitch=(high && high_g_mode!=3 ? 110.f : 55.f)*gain;
        const float a=1-std::exp(-dt/lag);
        q+=(pc*full_pitch-q)*a; r+=(yc*8-r)*a; s+=(rc*190-s)*a;
        V axis=b.r*(-q)+b.u*r-b.f*s;
        const float speed=std::sqrt(dot(axis,axis));
        if(speed>1e-5f) {
            axis=axis*(1/speed);
            b={rotate(b.f,axis,speed*dt*rad),rotate(b.r,axis,speed*dt*rad),rotate(b.u,axis,speed*dt*rad)};
        }
        const float filter=1-std::exp(-12*dt);
        fq+=(q-fq)*filter; fr+=(r-fr)*filter; fs+=(s-fs)*filter;
        peak_bank=std::max(peak_bank,std::abs(std::atan2(-b.r.z,b.u.z)/rad));
        peak_rate=std::max(peak_rate,std::abs(q));
        peak_pitch=std::max(peak_pitch,std::abs(pitch(b.f)));
        if(angle_to(b,aim)>3 || std::max({std::abs(q),std::abs(r),std::abs(s)})>5) settle=time;
    }
    return {settle,angle_to(b,aim),peak_bank,peak_rate,peak_pitch};
}
void diagonal_dynamics() {
    int count=0; float before_total=0,after_total=0,max_bank=0,max_error=0;
    for(float dt: {1.f/30,1.f/60,1.f/144}) for(float lag: {.1f,.25f})
    for(float target_pitch: {-35.f,35.f,60.f}) for(float yaw: {-150.f,-60.f,60.f,150.f})
    for(int mode: {0,1,2,3}) for(float gain: {.7f,1.f,1.25f}) {
        auto before=diagonal_simulate(true,0,target_pitch,yaw,dt,lag,gain,mode);
        auto after=diagonal_simulate(false,0,target_pitch,yaw,dt,lag,gain,mode);
        before_total+=before.settle; after_total+=after.settle; ++count;
        max_bank=std::max(max_bank,after.peak_bank); max_error=std::max(max_error,after.error);
        if(after.settle>=19 || after.error>=3 || after.peak_bank>=95)
            printf("DIAGONAL FAIL p=%.0f yaw=%.0f dt=%.4f lag=%.2f mode=%d old=%.2f new=%.2f error=%.2f bank=%.2f\n",target_pitch,yaw,dt,lag,mode,before.settle,after.settle,after.error,after.peak_bank);
        assert(after.settle<19 && after.error<3 && after.peak_bank<95);
    }
    printf("DIAGONAL cases=%d pw3Mean=%.3f currentMean=%.3f maxError=%.2f maxBank=%.2f\n",count,before_total/count,after_total/count,max_error,max_bank);
    assert(after_total<before_total);
    for(float dt: {1.f/30,1.f/60,1.f/144}) for(float target_pitch: {-60.f,-75.f})
    for(float yaw: {-60.f,60.f}) for(int mode: {0,1,2}) {
        const auto result=diagonal_simulate(false,0,target_pitch,yaw,dt,.15f,1,mode);
        if(result.settle>=19 || result.error>=3)
            printf("DIVE FAIL p=%.0f yaw=%.0f dt=%.4f mode=%d settle=%.2f error=%.2f\n",target_pitch,yaw,dt,mode,result.settle,result.error);
        assert(result.settle<19 && result.error<3);
    }
    const auto logged=diagonal_simulate(false,31.51f,5.23f,157.8f,1.f/60,.15f,1,0);
    assert(logged.settle<15 && logged.error<3);
    for(float start: {-85.f,85.f}) for(float yaw: {-90.f,90.f}) for(int mode: {0,1}) {
        const auto result=diagonal_simulate(false,start,std::copysign(35.f,start),yaw,1.f/60,.15f,1,mode);
        if(result.settle>=19 || result.error>=3)
            printf("POLE EXIT FAIL start=%.0f yaw=%.0f mode=%d settle=%.2f error=%.2f\n",start,yaw,mode,result.settle,result.error);
        assert(result.settle<19 && result.error<3);
    }
    puts("PASS diagonal up/down, side/rear targets, high-G request/release synthetic dynamics");
}
void rear_direction_regression() {
    // Fixed target from pw.4 log ticks 12141421/12141500/12141562.
    // Mirror the exact seam crossing to verify both turn directions.
    for(float side: {-1.f,1.f}) {
        BankGuidance g,legacy; BankSettings c; ResponseSettings cfg,old;
        cfg.high_g_requested=old.high_g_requested=true; old.rear_turn_hold=false;
        const auto aim=basis(10.31f,48.44f*side,0).f;
        const Basis frames[]={basis(5.21f,-131.58f*side,-36.23f*side),
            basis(5.42f,-131.55f*side,-43.52f*side),basis(5.58f,-131.57f*side,-47.62f*side)};
        float previous_old=0; int flips=0;
        for(const auto& b:frames) {
            const auto d=g.step(b,aim,angle_to(b,aim),.07f,c,cfg);
            const auto before=legacy.step(b,aim,angle_to(b,aim),.07f,c,old);
            assert(d.target*side< -70 && d.rear_turn_sign*side<0);
            if(previous_old*before.target<0) ++flips;
            previous_old=before.target;
            const auto aligned=basis(pitch(b.f),yaw(b.f),d.target);
            auto aligned_bank=d; aligned_bank.current=d.target; aligned_bank.error=0;
            const auto turn=coordinated_turn(aligned,aim,aligned_bank,0,0,0,cfg);
            assert(turn.horizontal_rate*side<0); // Bank and pitch/yaw agree on held direction.
        }
        assert(flips==2);
        RearTurnHold latch;
        assert(latch.step(basis(0,0,0),basis(0,179.8f*side,0).f,true)*side>0);
        assert(latch.step(basis(0,0,0),basis(0,-178*side,0).f,true)*side>0);
        assert(latch.step(basis(0,0,0),basis(0,-176*side,0).f,true)==0);
        // Clear user reversal out of the rear sector is honored immediately.
        const auto b=frames[2]; const auto other=basis(0,yaw(b.f)+140*side,0).f;
        auto d=g.step(b,other,140,.017f,c,cfg);
        assert(d.target*side>0 && d.rear_turn_sign==0);
        g.reset(); d=g.step(b,basis(0,yaw(b.f)-179.8f*side,0).f,179.8f,.017f,c,cfg);
        assert(d.rear_turn_sign*side<0);
        g.reset(); d=g.step(b,basis(0,yaw(b.f)+179.8f*side,0).f,179.8f,.017f,c,cfg);
        assert(d.rear_turn_sign*side>0);
        d=g.step(basis(89,0,0),other,angle_to(basis(89,0,0),other),.017f,c,cfg);
        assert(d.rear_turn_sign==0);
    }
    puts("PASS logged rear-seam replay, mirrored direction, shared allocation, reversal/reset/pole exit");
}
void local_authority() {
    ResponseSettings cfg,old; old.dive_pitch_boost=false;
    for(float side: {-1.f,1.f}) {
        assert(std::abs(dive_pitch_command(60*side,0,0,cfg)-side)<.001f);
        assert(std::abs(dive_pitch_command(60*side,0,0,old)-.85f*side)<.001f);
        for(float error: {0.f,5.f,20.f,30.f,60.f}) for(float actual: {-100.f,0.f,40.f,100.f}) {
            const float before=dive_pitch_command(error*side,actual*side,.3f,old);
            const float after=dive_pitch_command(error*side,actual*side,.3f,cfg);
            if(error<=20 || before*side<0) assert(std::abs(before-after)<1e-6f);
            assert(std::isfinite(after) && std::abs(after)<=1);
        }
        cfg.high_g_requested=old.high_g_requested=true; cfg.high_g_yaw_boost=true;
        const auto b=basis(0,0,85*side); const auto aim=basis(0,90*side,0).f;
        BankGuidance g; BankSettings c;
        const auto bank=g.step(b,aim,90,.017f,c,cfg);
        const auto base=coordinated_turn(b,aim,bank,65,0,0,old);
        const auto boost=coordinated_turn(b,aim,bank,65,0,0,cfg);
        assert(boost.yaw_boost>.99f && std::abs(boost.yaw_command)>.99f);
        assert(boost.pitch_rate>base.pitch_rate && std::abs(boost.vertical_rate)<.001f);
        auto changing=bank; changing.error=45;
        assert(coordinated_turn(b,aim,changing,0,0,0,cfg).yaw_boost==0);
        assert(coordinated_turn(b,basis(20,90*side,0).f,bank,0,0,0,cfg).yaw_boost==0);
        assert(coordinated_turn(b,basis(0,10*side,0).f,bank,0,0,0,cfg).yaw_boost==0);
        cfg.high_g_requested=false;
        assert(coordinated_turn(b,aim,bank,0,0,0,cfg).yaw_boost==0);
        old.high_g_requested=false;
    }
    puts("PASS distant-dive authority with unchanged braking and scoped opt-in yaw boost");
}
void pose_guard_geometry() {
    assert(pose_discontinuity(basis(0,0,0),basis(0,-106.31f,0),.017f));
    assert(!pose_discontinuity(basis(0,179,0),basis(0,-179,0),.017f));
    assert(!pose_discontinuity(basis(89,0,20),basis(91,0,20),.017f));
    for(float dt: {1.f/30,1.f/60,1.f/144,.1f})
        assert(!pose_discontinuity(basis(0,0,0),basis(140*dt,0,200*dt),dt));
    puts("PASS full-orientation discontinuity threshold preserves fast flight and Euler poles");
}
void refinement_dynamics() {
    int count=0; float base_time=0,new_time=0,peak_bank=0,max_error=0;
    float totals[2][2]{},baseline[2][2]{},worst_delta=0,peak_level_pitch=0;
    for(float dt: {1.f/30,1.f/60,1.f/144}) for(float lag: {.1f,.25f})
    for(float target_pitch: {-35.f,0.f,35.f}) for(float target_yaw: {-179.f,-90.f,90.f,179.f})
    for(int mode: {0,1,2,3}) for(bool boost: {false,true}) {
        const auto before=diagonal_simulate(false,0,target_pitch,target_yaw,dt,lag,1,mode,false,false);
        const auto after=diagonal_simulate(false,0,target_pitch,target_yaw,dt,lag,1,mode,boost,true);
        if(after.settle>=19 || after.error>=3 || after.peak_bank>=95)
            printf("REFINEMENT FAIL p=%.0f yaw=%.0f dt=%.4f lag=%.2f mode=%d boost=%d settle=%.2f error=%.2f bank=%.2f\n",
                target_pitch,target_yaw,dt,lag,mode,boost,after.settle,after.error,after.peak_bank);
        assert(after.settle<19 && after.error<3 && after.peak_bank<95);
        if(target_pitch==0) {
            peak_level_pitch=std::max(peak_level_pitch,after.peak_pitch);
            assert(after.peak_pitch<5);
        }
        base_time+=before.settle; new_time+=after.settle; ++count;
        const int rear=std::abs(target_yaw)>170 ? 1 : 0;
        totals[boost][rear]+=after.settle; baseline[boost][rear]+=before.settle;
        if(after.settle-before.settle>worst_delta) {
            worst_delta=after.settle-before.settle;
            printf("REFINEMENT DELTA p=%.0f yaw=%.0f dt=%.4f lag=%.2f mode=%d boost=%d before=%.3f after=%.3f\n",
                target_pitch,target_yaw,dt,lag,mode,boost,before.settle,after.settle);
        }
        peak_bank=std::max(peak_bank,after.peak_bank); max_error=std::max(max_error,after.error);
    }
    printf("REFINEMENTS cases=%d disabledMean=%.3f enabledMean=%.3f maxError=%.2f peakBank=%.2f\n",
        count,base_time/count,new_time/count,max_error,peak_bank);
    for(int boost=0;boost<2;++boost) for(int rear=0;rear<2;++rear)
        printf("REFINEMENT GROUP boost=%d rear=%d before=%.3f after=%.3f\n",boost,rear,baseline[boost][rear]/(count/4),totals[boost][rear]/(count/4));
    assert(worst_delta<.3f); // Bound the stability/route-choice tradeoff on this matrix.
    printf("REFINEMENT peakLevelPitch=%.3f worstSettleDelta=%.3f\n",peak_level_pitch,worst_delta);
    puts("PASS rear/level/diagonal dynamics, yaw option on/off and high-G request/release/unavailable");
}
int main() {
    _set_error_mode(_OUT_TO_STDERR);
    setvbuf(stdout,nullptr,_IONBF,0);
    normal_and_recovery(); dive_modes(); poles_and_rear(); braking_response();
    keyboard_config(); free_look_regression(); turn_allocation(); turn_dynamics();
    predictive_response(); response_comparison();
    diagonal_allocation(); diagonal_dynamics();
    rear_direction_regression(); local_authority(); pose_guard_geometry();
    refinement_dynamics();
}
