#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <vector>
#include "angle_guidance.h"
#include "rollout_coordination.h"
#include "terminal_braking.h"
#include "pw7_reference/angle_guidance.h"
#include "pw7_flight_frames.h"
using namespace flight;
struct Commands { float q,r,s; bool timeout=false; };
struct Current {
    ResponseSettings cfg;
    BankSettings limits;
    BankGuidance guide;
    HighGBraking brake;
    RolloutCoordinator rollout;
    TerminalBraking terminal;
    explicit Current(bool enabled=true) { cfg.rollout_coordination=enabled; limits.max_bank=89; cfg.roll_lookahead=.12f; }
    Commands step(Basis b,V aim,float q,float r,float s,float dt,bool high) {
        cfg.high_g_requested=high;
        cfg.high_g_brake_weight=brake.step(high,dt,cfg.high_g_brake_hold,target_distance(b.f,aim),q);
        cfg.high_g_settling=brake.settling();
        const auto bank=guide.step(b,aim,target_distance(b.f,aim),dt,limits,cfg);
        const auto roll=rollout.step(b,aim,bank,q,r,dt,cfg.rollout_coordination);
        const float drift=horizon_roll_drift(b,q,r);
        const auto turn=angle_turn(b,aim,bank,q,r,s+drift,cfg);
        return {terminal.step(b,aim,bank,q,turn,cfg,dt),turn.yaw.command,angle_roll(roll.bank,s,cfg,drift).command,roll.reason==RolloutReason::Timeout};
    }
};
struct Frozen {
    pw7_reference::ResponseSettings cfg;
    pw7_reference::BankSettings limits;
    pw7_reference::BankGuidance guide;
    pw7_reference::HighGBraking brake;
    Frozen() { limits.max_bank=89; cfg.roll_lookahead=.12f; }
    Commands step(Basis b,V aim,float q,float r,float s,float dt,bool high) {
        using PB=pw7_reference::Basis; using PV=pw7_reference::V;
        const auto v=[](V a) { return PV{a.x,a.y,a.z}; };
        const PB frame{v(b.f),v(b.r),v(b.u)};
        cfg.high_g_requested=high;
        cfg.high_g_brake_weight=brake.step(high,dt,cfg.high_g_brake_hold,target_distance(b.f,aim),q);
        cfg.high_g_settling=brake.settling();
        const auto bank=guide.step(frame,v(aim),target_distance(b.f,aim),dt,limits,cfg);
        const float drift=pw7_reference::horizon_roll_drift(frame,q,r);
        const auto turn=pw7_reference::angle_turn(frame,v(aim),bank,q,r,s+drift,cfg);
        return {turn.pitch.command,turn.yaw.command,pw7_reference::angle_roll(bank,s,cfg,drift).command};
    }
};
void state_tests() {
    RolloutCoordinator c; ResponseSettings cfg;
    const Basis b=basis(-2.79f,165.08f,-71.86f); const V aim=basis(-5.21f,163.75f,0).f;
    const BankDemand bank{-71.86f,-5.82f,66.04f,0,false,0,1};
    const auto before=angle_turn(b,aim,bank,34.71f,-4.87f,16.59f,cfg);
    const auto held=c.step(b,aim,bank,34.71f,-4.87f,.016f);
    assert(held.phase==RolloutPhase::Waiting && held.bank.target==bank.current);
    const auto after=angle_turn(b,aim,bank,34.71f,-4.87f,16.59f,cfg);
    assert(before.pitch.command==after.pitch.command && before.yaw.command==after.yaw.command && before.gate==after.gate);
    assert(before.gate==0); // Holding a bank must not falsely open this pitch gate.
    for(int i=0;i<6;++i) assert(c.step(b,aim,bank,4,3,.016f).phase==RolloutPhase::Waiting);
    auto release=c.step(b,aim,bank,4,3,.016f);
    assert(release.phase==RolloutPhase::Returning && release.bank.target>bank.current && release.bank.target<bank.target);
    for(int i=0;i<120;++i) c.step(b,aim,bank,4,3,.016f);
    assert(c.state()==RolloutPhase::Tracking);
    assert(c.step(b,aim,bank,40,0,.016f).phase==RolloutPhase::Tracking); // Once per arrival.
    c.reset(); c.step(b,aim,bank,40,0,.016f);
    assert(c.step(b,basis(-5,150,0).f,bank,40,0,.016f).reason==RolloutReason::TargetMoved);
    c.reset(); c.step(b,aim,bank,40,0,.016f);
    auto disabled=c.step(b,aim,bank,40,0,.016f,false);
    assert(disabled.bank.target==bank.target && c.state()==RolloutPhase::Tracking);
    for(int mode=0;mode<3;++mode) {
        c.reset(); c.step(b,aim,bank,40,0,.016f);
        auto other=bank; Basis frame=b;
        if(mode==0) other.diving=true;
        if(mode==1) other.current=100;
        if(mode==2) frame=basis(89,0,0);
        assert(c.step(frame,aim,other,40,0,.016f).reason==RolloutReason::Bypass);
    }
    c.reset(); RolloutDemand timed{bank};
    for(int i=0;i<121;++i) timed=c.step(b,aim,bank,40,0,1.f/60);
    assert(timed.reason==RolloutReason::Timeout && timed.phase==RolloutPhase::Returning);
    // A new large turn and deliberate roll-in are untouched.
    c.reset(); assert(c.step(b,basis(0,90,0).f,bank,100,0,.016f).phase==RolloutPhase::Tracking);
    const BankDemand roll_in{-30,-80,-50,1};
    assert(c.step(b,aim,roll_in,40,0,.016f).phase==RolloutPhase::Tracking);
    // A filtered high rate already falling fast does not need a new hold.
    c.reset(); c.step(b,basis(-5,150,0).f,bank,60,0,.016f);
    const auto stopping=c.step(b,aim,bank,45,0,.016f);
    assert(stopping.residual_speed==0 && stopping.phase==RolloutPhase::Tracking);
    puts("PASS separate rollout: immutable pitch gate, predicted release, rearm, bypass and timeout");
}
void terminal_tests() {
    TerminalBraking brake; ResponseSettings cfg;
    const Basis b=basis(0,0,45); const V aim=basis(0,2,0).f;
    BankDemand bank{45,0,-45}; AngleTurnDemand turn; turn.pitch.command=-.2f;
    const float command=brake.step(b,aim,bank,35,turn,cfg,.016f);
    assert(command<-.2f && command>=-1 && brake.active());
    // Braking fades on rapid observed slowing, and cannot rearm on a bounce.
    assert(brake.step(b,aim,bank,12,turn,cfg,.016f)==-.2f && !brake.active());
    assert(brake.step(b,aim,bank,35,turn,cfg,.016f)==-.2f && !brake.active());
    brake.step(b,basis(0,40,0).f,bank,35,turn,cfg,.016f);
    assert(brake.step(b,aim,bank,35,turn,cfg,.016f)<-.2f);
    brake.reset(); assert(brake.fade_speed==0 && !brake.active());
    assert(brake.step(b,aim,bank,19,turn,cfg,.016f)==-.2f && !brake.active());
    brake.reset();
    turn.pitch.command=1;
    assert(brake.step(b,aim,bank,90,turn,cfg,.016f)==1);
    turn.pitch.command=-.2f;
    assert(brake.step(b,basis(0,40,0).f,bank,90,turn,cfg,.016f)==-.2f);
    for(int bypass=0;bypass<5;++bypass) {
        brake.reset(); auto limits=bank; auto config=cfg; Basis frame=b;
        if(bypass==0) limits.diving=true;
        if(bypass==1) limits.current=100;
        if(bypass==2) config.rollout_coordination=false;
        if(bypass==3) frame=basis(89,0,0);
        const float dt=bypass==4?.3f:.016f;
        assert(brake.step(frame,aim,limits,35,turn,config,dt)==-.2f && !brake.active());
    }
    // Positive and negative rotation use the same supplement; high-G settling
    // remains pw.7's responsibility, with a smaller supplement on top.
    for(bool settling:{false,true}) {
        brake.reset(); cfg.high_g_requested=true; cfg.high_g_settling=settling;
        const auto positive=brake.step(b,aim,bank,35,turn,cfg,.016f);
        brake.reset(); turn.pitch.command=.2f;
        const auto negative=brake.step(b,aim,bank,-35,turn,cfg,.016f);
        assert(std::abs(positive+negative)<1e-6f && std::abs(positive)<=.4f);
        turn.pitch.command=-.2f;
    }
    puts("PASS terminal braking: reverse only, slow/distant bypass, once per arrival, reset and symmetric output");
}
void compatibility() {
    Current disabled(false),enabled(true); Frozen old;
    for(int i=0;i<2000;++i) {
        const float t=i*.017f;
        const Basis b=basis(80*std::sin(t),170*std::sin(.7f*t),175*std::sin(1.3f*t));
        const V aim=basis(85*std::sin(1.2f*t),179*std::sin(.4f*t),0).f;
        const float q=160*std::sin(t),r=15*std::cos(t),s=200*std::sin(2*t);
        const auto a=disabled.step(b,aim,q,r,s,.017f,i%140<90),z=old.step(b,aim,q,r,s,.017f,i%140<90);
        const auto next=enabled.step(b,aim,q,r,s,.017f,i%140<90);
        assert(a.q==z.q && a.r==z.r && a.s==z.s);
        assert(enabled.cfg.high_g_settling==old.cfg.high_g_settling && enabled.cfg.high_g_brake_weight==old.cfg.high_g_brake_weight);
        assert(next.r==z.r);
        if(next.q!=z.q) assert(z.q*q<0 && std::abs(next.q)>=std::abs(z.q) && target_distance(b.f,aim)<25);
    }
    puts("PASS 2000 frames: disabled exact pw.7, enabled pitch differs only by near-target reverse braking");
}
struct Scenario {
    float dt=1.f/60,lag=.25f,gain=1,delay=0,negative=1;
    int move=0,high=0;
    float sign=1,initial_bank=0;
    bool asymmetric=false,changing=false;
    const FlightFrame* seed=nullptr;
};
struct Result {
    float nose=0,wings=0,late=0,post=0,excess=0,vertical=0,first=20;
    bool entered=false,timeout=false;
};
template<class Controller> Result simulate(const Scenario& x) {
    Controller c; Basis b=basis(x.move==7?85.f:0,0,x.initial_bank);
    V aim=basis(x.move==1?35.f:x.move==2?-35.f:x.move==5?-60.f:x.move==6?70.f:x.move==7?20.f:0,
        (x.move==3?179.f:x.move==6?0.f:90.f)*x.sign,0).f;
    float q=0,r=0,s=0,fq=0,fr=0,fs=0;
    Commands initial{};
    if(x.seed) { const auto& f=*x.seed; b=basis(f.p,f.y,f.b); aim=basis(f.tp,f.ty,0).f;
        q=fq=f.q; r=fr=f.r; s=fs=f.s; initial={f.body_pitch,f.body_yaw,0}; }
    std::deque<Commands> pending(size_t(std::lround(x.delay/x.dt)),initial);
    Result out;
    for(int i=0;i<int(20/x.dt);++i) {
        const float t=i*x.dt;
        if(x.move==4 && t>=2) aim=basis(0,-90*x.sign,0).f;
        if(x.move==8 && t<2) aim=basis(10*std::sin(t*3),90+20*std::sin(t*4),0).f;
        const bool high=x.high!=0 && (x.high!=2 || t<2);
        const bool actual_high=high && x.high!=3 && (x.high!=4 || t>=.35f);
        const auto cmd=c.step(b,aim,fq,fr,fs,x.dt,high);
        assert(std::isfinite(cmd.q) && std::isfinite(cmd.r) && std::isfinite(cmd.s));
        assert(std::max({std::abs(cmd.q),std::abs(cmd.r),std::abs(cmd.s)})<=1);
        out.timeout|=cmd.timeout;
        pending.push_back(cmd); const auto applied=pending.front(); pending.pop_front();
        const float gain=x.gain*(x.changing && t>1.5f?.65f:1);
        q+=(applied.q*(actual_high?110.f:55.f)*gain*(applied.q<0?x.negative:1)-q)*(1-std::exp(-x.dt/x.lag));
        r+=(applied.r*10-r)*(1-std::exp(-x.dt/(x.asymmetric?x.lag*1.6f:x.lag)));
        s+=(applied.s*190-s)*(1-std::exp(-x.dt/(x.asymmetric?x.lag*.6f:x.lag)));
        const V omega=b.r*(-q)+b.u*r-b.f*s;
        const float speed=std::sqrt(dot(omega,omega));
        if(speed>1e-5f) { const V axis=omega*(1/speed);
            b={rotate(b.f,axis,speed*x.dt*rad),rotate(b.r,axis,speed*x.dt*rad),rotate(b.u,axis,speed*x.dt*rad)}; }
        const float filter=1-std::exp(-12*x.dt);
        fq+=(q-fq)*filter; fr+=(r-fr)*filter; fs+=(s-fs)*filter;
        const float error=target_distance(b.f,aim),bank=std::atan2(-b.r.z,b.u.z)/rad;
        const bool final_leg=(x.move!=4 && x.move!=8) || t>=2;
        if(!final_leg) continue;
        if(error>3) out.nose=t+x.dt;
        if(error>3 || std::abs(bank)>5 || std::abs(s)>10) out.wings=t+x.dt+.25f;
        if(t>15) out.late=std::max(out.late,error);
        if(error<=3 && !out.entered) { out.entered=true; out.first=t; }
        if(out.entered) out.post=std::max(out.post,error);
        if(x.move==0 || x.move==3 || x.move==4 || x.seed) out.vertical=std::max(out.vertical,std::abs(pitch(b.f)-pitch(aim)));
    }
    out.excess=std::max(0.f,out.post-3);
    return out;
}
float percentile(std::vector<float> v,float fraction) {
    std::sort(v.begin(),v.end()); return v[size_t((v.size()-1)*fraction)];
}
bool regression(const Result& a,const Result& z) {
    return !a.entered || a.nose>=19 || a.timeout || a.late>std::max(1.f,z.late+.2f) ||
        a.post>z.post+1;
}
int matrices() {
    int count=0,failures=0,extra_count=0,extra_fail=0;
    std::vector<float> before,after,w0,w1,f0,f1;
    for(float dt:{1.f/30,1.f/60,1.f/144}) for(float lag:{.1f,.25f}) for(float gain:{.7f,1.f,1.8f})
    for(int high:{0,1,2,3,4}) for(int move=0;move<8;++move) for(float sign:{-1.f,1.f}) {
        Scenario x; x.dt=dt; x.lag=lag; x.gain=gain; x.high=high; x.move=move; x.sign=sign;
        const auto z=simulate<Frozen>(x),a=simulate<Current>(x); ++count;
        if(z.late<1 && z.post<=5) { before.push_back(z.nose); after.push_back(a.nose); w0.push_back(z.wings); w1.push_back(a.wings); f0.push_back(z.first); f1.push_back(a.first); }
        if(regression(a,z)) {
            if(failures++<12) printf("BASE FAIL move=%d high=%d dt=%.4f lag=%.2f gain=%.1f nose=%.2f->%.2f wings=%.2f->%.2f post=%.2f->%.2f timeout=%d\n",move,high,dt,lag,gain,z.nose,a.nose,z.wings,a.wings,z.post,a.post,a.timeout);
        }
    }
    const auto m0=percentile(before,.5f),m1=percentile(after,.5f),p0=percentile(before,.95f),p1=percentile(after,.95f);
    const auto wing0=percentile(w0,.95f),wing1=percentile(w1,.95f);
    printf("ROLLOUT BASE cases=%d failures=%d noseMedian=%.3f->%.3f noseP95=%.3f->%.3f wingsP95=%.3f->%.3f firstMedian=%.3f->%.3f\n",count,failures,m0,m1,p0,p1,wing0,wing1,percentile(f0,.5f),percentile(f1,.5f));
    if(m1>m0*1.05f || p1>p0*1.10f || wing1>wing0+1) ++failures;
    for(float lag:{.15f,.35f,.5f}) for(float delay:{0.f,.08f,.15f}) for(float gain:{.7f,1.f,1.8f})
    for(int high:{0,1,2,4}) for(int move:{0,1,2,4,8}) for(float bank:{-65.f,0.f,65.f}) {
        Scenario x; x.lag=lag; x.delay=delay; x.gain=gain; x.high=high; x.move=move; x.initial_bank=bank;
        x.negative=.55f; x.asymmetric=true; x.changing=true;
        const auto z=simulate<Frozen>(x),a=simulate<Current>(x); ++extra_count;
        if(regression(a,z)) {
            if(extra_fail++<12) printf("EXTRA FAIL move=%d high=%d lag=%.2f delay=%.2f gain=%.1f bank=%.0f nose=%.2f->%.2f post=%.2f->%.2f timeout=%d\n",move,high,lag,delay,gain,bank,z.nose,a.nose,z.post,a.post,a.timeout);
        }
    }
    printf("ROLLOUT EXTENDED cases=%d failures=%d\n",extra_count,extra_fail);
    std::vector<float> e0,e1;
    int seeded_count=0,seeded_fail=0,height_fail=0;
    // Fixed before tuning: ordinary F-14D/Su-35 premature-leveling cases.
    // The mid-turn high-G climb frame belongs to a later, separate change.
    for(const auto& f:flight_frames) if(f.line==30276 || f.line==30287 || f.line==109301 || f.line==109307)
    for(float lag:{.1f,.25f,.5f}) for(float delay:{0.f,.08f,.15f}) for(float gain:{.7f,1.f,1.8f}) {
        Scenario x; x.seed=&f; x.lag=lag; x.delay=delay; x.gain=gain; x.negative=.55f; x.asymmetric=true;
        const auto z=simulate<Frozen>(x),a=simulate<Current>(x); ++seeded_count;
        e0.push_back(z.excess); e1.push_back(a.excess);
        if(regression(a,z)) {
            if(seeded_fail++<8) printf("SEED FAIL line=%d lag=%.2f delay=%.2f gain=%.1f post=%.2f->%.2f nose=%.2f->%.2f\n",f.line,lag,delay,gain,z.post,a.post,z.nose,a.nose);
        }
        if(a.vertical>z.vertical+1) {
            if(height_fail++<8) printf("HEIGHT FAIL line=%d lag=%.2f delay=%.2f gain=%.1f vertical=%.2f->%.2f\n",f.line,lag,delay,gain,z.vertical,a.vertical);
        }
    }
    const float excess0=percentile(e0,.95f),excess1=percentile(e1,.95f);
    printf("ROLLOUT SEEDED cases=%d failures=%d heightFailures=%d excessP95=%.3f->%.3f requiredReduction=20%%\n",seeded_count,seeded_fail,height_fail,excess0,excess1);
    if(excess1>excess0*.8f) ++seeded_fail;
    return failures+extra_fail+seeded_fail+height_fail;
}
// Additional values selected after tuning: independent interpolation check.
int holdout() {
    int total=0,fail=0;
    for(float dt:{1.f/45,1.f/90}) for(float lag:{.18f,.4f}) for(float delay:{.04f,.12f}) for(float gain:{.85f,1.4f})
    for(int high:{0,1,2,4}) for(int move:{0,1,2,4,8}) for(float bank:{-45.f,45.f}) {
        Scenario x;x.dt=dt;x.lag=lag;x.delay=delay;x.gain=gain;x.high=high;x.move=move;x.initial_bank=bank;x.negative=.55f;x.asymmetric=true;x.changing=true;
        auto z=simulate<Frozen>(x),a=simulate<Current>(x);++total;
        if(regression(a,z)) {++fail;printf("HOLDOUT FAIL dt=%.4f lag=%.2f delay=%.2f gain=%.2f high=%d move=%d bank=%.0f nose=%.2f->%.2f post=%.2f->%.2f\n",dt,lag,delay,gain,high,move,bank,z.nose,a.nose,z.post,a.post);}
    }
    for(const auto& f:flight_frames) if(f.line==30276 || f.line==30287 || f.line==109301 || f.line==109307)
    for(float dt:{1.f/45,1.f/90}) for(float lag:{.18f,.4f}) for(float delay:{.04f,.12f}) for(float gain:{.85f,1.4f}) {
        Scenario x;x.seed=&f;x.dt=dt;x.lag=lag;x.delay=delay;x.gain=gain;x.negative=.55f;x.asymmetric=true;
        auto z=simulate<Frozen>(x),a=simulate<Current>(x);++total;
        if(regression(a,z) || a.vertical>z.vertical+1) {++fail;printf("HOLDOUT SEED FAIL line=%d dt=%.4f lag=%.2f delay=%.2f gain=%.2f nose=%.2f->%.2f post=%.2f->%.2f height=%.2f->%.2f\n",f.line,dt,lag,delay,gain,z.nose,a.nose,z.post,a.post,z.vertical,a.vertical);}
    }
    printf("HOLDOUT total=%d failed=%d\n",total,fail);return fail;
}

int main() {
    _set_error_mode(_OUT_TO_STDERR); setvbuf(stdout,nullptr,_IONBF,0);
    state_tests(); terminal_tests(); compatibility(); const int failures=matrices()+holdout();
    if(failures) { printf("ROLLOUT RELEASE GATE FAILED: %d\n",failures); return 1; }
    puts("PASS rollout release gates");
}
