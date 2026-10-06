"""Compile actual native functions in isolation; no DLL loading or game hooks."""
from pathlib import Path

def section(text, begin, end):
    assert text.count(begin) == 1, begin
    return text.split(begin, 1)[1].split(end, 1)[0]

def generate(source: Path, destination: Path):
    text = source.read_text(encoding='utf-8')
    config = 'struct Config :' + section(text, 'struct Config :', 'struct Pose')
    globals_ = 'std::atomic<bool> enabled' + section(text, 'std::atomic<bool> enabled', 'wchar_t module_folder')
    loader = 'float read_config_float' + section(text, 'float read_config_float', 'struct WindowCandidate')
    update = 'void update_commands()' + section(text, 'void update_commands()', '#include "native_camera.h"')
    process = 'uintptr_t __fastcall process_input' + section(text, 'uintptr_t __fastcall process_input', 'bool prepare_hook()')
    target_context = 'DWORD bridge_thread=0;\nbool on_bridge_thread()' + section(text, 'bool on_bridge_thread()', '} // namespace')
    prefix = r'''
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include "bank_guidance.h"
#include "turn_guidance.h"
#include "angle_guidance.h"
#include "rollout_coordination.h"
#include "terminal_braking.h"
#include "pose_guard.h"
#include "key_bindings.h"
#include "free_look.h"
#include "version.h"
#include "native_target_selection.h"
unsigned long long test_now=10000;
bool keys[256]{}, front=true;
unsigned long long test_clock() { return test_now; }
short test_key(int key) { return keys[key]?short(-32768):0; }
bool foreground_is_game() { return front; }
#define GetTickCount64 test_clock
#define GetAsyncKeyState test_key
std::vector<std::string> messages;
void log_line(const char* format,...) {
    char out[2048]; va_list args; va_start(args,format);
    vsnprintf(out,sizeof(out),format,args); va_end(args); messages.emplace_back(out);
}
bool logged(const char* text) {
    for (const auto& m:messages) if(m.find(text)!=std::string::npos) return true;
    return false;
}
wchar_t config_path[MAX_PATH]{};
void init_paths() {}
std::atomic<bool> running{false};
std::atomic<bool> perf_enabled{false};
std::atomic<unsigned long long> perf_other_inputs{0},perf_player_inputs{0};
uintptr_t __fastcall fake_original(unsigned char* state,unsigned char*) {
    *reinterpret_cast<double*>(state+8)=0.123; return 42;
}
auto original=fake_original;
'''
    tests = r'''
void reset_pose() {
    config=Config{}; config.angle_control=false; controller_reset_pending=false; key_bindings=input::Bindings{};
    std::fill(std::begin(keys),std::end(keys),false); front=true;
    active=true; enabled=true; game_paused=false; gaze_active=false;
    previous_pose_tick=0; test_now+=100; pose_tick=test_now;
    pose_pitch=0; pose_yaw=0; pose_roll=0;
    camera_pitch=0; camera_yaw=0; camera_roll=0;
    target_pitch=0; target_yaw=30;
    mouse_dx=0; mouse_dy=0; free_look.reset(); bank_guidance.reset(); high_g_braking.reset(); rollout_coordinator.reset(); terminal_braking.reset();
    recenter_requested=false; resume_center_requested=false; telemetry_tick=0;
}
void runtime_guidance() {
    reset_pose(); pose_roll=26; target_pitch=0; target_yaw=60;
    update_commands(); assert(std::abs(command_pitch.load())<.001);
    assert(std::abs(command_yaw.load())<.001 && command_roll<0); // Roll first, no premature pull.
    reset_pose(); pose_roll=120; target_pitch=-4.981f; target_yaw=8.682f;
    update_commands(); assert(command_roll>0.4f); // roll_sign=-1, positive command rights aircraft.
    reset_pose(); key_bindings.keys[input::FreeLook]='L'; keys['L']=true;
    update_commands(); test_now+=17; mouse_dx=20; update_commands();
    assert(std::abs(target_yaw.load()-30)<.01 && std::abs(look_yaw.load())>.1);
    keys['L']=false; test_now+=17; update_commands();
    keys['F']=true; mouse_dx=20; test_now+=17; update_commands();
    assert(std::abs(target_yaw.load()-30)>1); // Original F no longer freezes target.
    const float saved=target_yaw; camera_yaw=100; test_now+=17; update_commands();
    assert(std::abs(target_yaw.load()-saved)<.01); // Camera movement alone leaves target.
    front=false; update_commands();
    assert(command_pitch==0 && command_roll==0 && command_yaw==0);
    front=true; game_paused=true; update_commands();
    assert(command_pitch==0 && command_roll==0 && command_yaw==0);
    // Seed a measured 65 deg/s pitch rate: ordinary guidance brakes, but an
    // observed Ctrl+Space request must allow further high-G pulling.
    auto fast_turn=[] {
        reset_pose(); pose_roll=85; target_yaw=90;
        previous_pose_tick=test_now-17; previous_pitch=0; previous_yaw=0; previous_roll=85;
        filtered_pitch_rate=80; filtered_yaw_rate=0; filtered_roll_rate=0;
    };
    fast_turn(); keys[VK_CONTROL]=true; update_commands(); assert(command_pitch<0);
    fast_turn(); keys[VK_CONTROL]=keys[VK_SPACE]=true; messages.clear();
    update_commands(); assert(command_pitch>.99f && logged("highGRequested=1"));
    fast_turn(); keys[VK_CONTROL]=keys[VK_SPACE]=true; config.high_g_enabled=false;
    update_commands(); assert(command_pitch<0);
    fast_turn(); key_bindings.keys[input::HighG1]='H'; key_bindings.keys[input::HighG2]='J';
    keys[VK_CONTROL]=keys[VK_SPACE]=true; update_commands(); assert(command_pitch<0);
    fast_turn(); key_bindings.keys[input::HighG1]='H'; key_bindings.keys[input::HighG2]='J';
    keys['H']=keys['J']=true; update_commands(); assert(command_pitch>.99f);
    puts("PASS actual update_commands: recovery, remapped look, hold target, focus/pause");
}
void runtime_axes() {
    reset_pose(); alignas(16) unsigned char memory[0x3000]{},context[16]{};
    aircraft=reinterpret_cast<uintptr_t>(memory);
    auto state=memory+0x22a0; auto axes=reinterpret_cast<float*>(memory+0x2268);
    *reinterpret_cast<uintptr_t*>(context)=reinterpret_cast<uintptr_t>(memory+0x2268);
    *reinterpret_cast<uintptr_t*>(context+8)=reinterpret_cast<uintptr_t>(memory+0x2c28);
    command_pitch=.4f; command_yaw=.2f; command_roll=.6f;
    key_bindings.keys[input::Pitch1]='U'; keys['U']=true;
    axes[0]=.9f; axes[1]=.7f; axes[2]=.8f;
    assert(process_input(state,context)==42);
    assert(axes[0]==.9f && axes[2]==.8f && axes[1]==.2f);
    assert(std::abs(*reinterpret_cast<double*>(state+8)-.2)<1e-6);
    keys['U']=false; keys['W']=true;
    process_input(state,context); assert(axes[0]==.4f && axes[2]==.6f);
    keys['W']=false; keys['Q']=true; axes[1]=.7f;
    process_input(state,context); assert(axes[1]==.7f && *reinterpret_cast<double*>(state+8)==.123);
    keys['Q']=false; keys['A']=true; axes[2]=.8f;
    process_input(state,context); assert(axes[2]==.8f && axes[0]==.4f);
    keys['A']=false; front=false; axes[0]=.9f;
    process_input(state,context); assert(axes[0]==.9f);
    front=true; test_now+=1001;
    process_input(state,context); assert(axes[0]==.9f);
    pose_tick=test_now; *reinterpret_cast<uintptr_t*>(context)=0;
    process_input(state,context); assert(axes[0]==.9f);
    puts("PASS actual input hook: remapped axes, manual yaw, stale pose/focus/context fallback");
}
void runtime_pose_guard() {
    // Reproduce the logged spawn orientation jump without loading a DLL.
    reset_pose(); update_commands(); messages.clear();
    test_now+=17; pose_yaw=-106.31f; mouse_dx=400;
    update_commands();
    assert(logged("POSE_RESET reason=discontinuity"));
    assert(filtered_pitch_rate==0 && filtered_yaw_rate==0 && filtered_roll_rate==0);
    assert(command_pitch==0 && command_yaw==0 && command_roll==0);
    assert(std::abs(target_yaw.load()+106.31f)<.01f && mouse_dx==0);
    test_now+=17; update_commands();
    assert(std::abs(command_pitch.load())<.001f && std::abs(command_roll.load())<.001f);
    assert(std::abs(command_yaw.load())<.001f);
    // A sampling gap invalidates velocity, but retains a still valid target.
    target_yaw=-40; messages.clear(); test_now+=300; update_commands();
    assert(logged("POSE_RESET reason=gap") && target_yaw== -40);
    assert(command_pitch==0 && command_yaw==0 && command_roll==0);
    test_now+=17; update_commands(); assert(command_roll<0);
    // A normal Euler wrap must not reset the target/rate estimator.
    reset_pose(); pose_yaw=179; target_yaw=150; update_commands(); messages.clear();
    test_now+=17; pose_yaw=-179; update_commands();
    assert(!logged("POSE_RESET") && std::abs(target_yaw.load()-150)<.01f);
    puts("PASS actual pose-jump/gap reset, neutral update, resumed control and Euler wrap");
}
void runtime_config() {
    GetFullPathNameW(L"test-config.ini",MAX_PATH,config_path,nullptr);
    DeleteFileW(config_path); running=false; config=Config{}; load_config();
    assert(key_bindings==input::Bindings{} && config.max_bank==85);
    assert(config.turn_rate_scale==1.2f && config.roll_rate_scale==1.25f);
    assert(config.response_gain==1.2f && config.countersteer_gain==2 && config.roll_lookahead==.1f);
    assert(config.high_g_enabled && config.high_g_pitch_rate==110);
    assert(config.rear_turn_hold && config.dive_pitch_boost && !config.high_g_yaw_boost);
    assert(key_bindings[input::HighG1]==VK_CONTROL && key_bindings[input::HighG2]==VK_SPACE);
    WritePrivateProfileStringW(L"keys",L"pitch_keys",L"Up,Down",config_path);
    WritePrivateProfileStringW(L"keys",L"reload_config",L"F12",config_path);
    WritePrivateProfileStringW(L"control",L"max_bank",L"50",config_path);
    load_config(); assert(key_bindings[input::Pitch1]==VK_UP && key_bindings[input::Reload]==VK_F12 && config.max_bank==50);
    running=true; WritePrivateProfileStringW(L"keys",L"reload_config",L"F11",config_path);
    WritePrivateProfileStringW(L"control",L"turn_rate_scale",L"1.1",config_path);
    WritePrivateProfileStringW(L"control",L"countersteer_gain",L"2.2",config_path);
    WritePrivateProfileStringW(L"control",L"rear_turn_hold",L"0",config_path);
    WritePrivateProfileStringW(L"control",L"dive_pitch_boost",L"0",config_path);
    WritePrivateProfileStringW(L"control",L"high_g_yaw_boost",L"1",config_path);
    messages.clear(); load_config();
    assert(key_bindings[input::Reload]==VK_F12 && logged("restart game"));
    assert(config.turn_rate_scale==1.1f && config.countersteer_gain==2.2f && logged("RESPONSE"));
    assert(!config.rear_turn_hold && !config.dive_pitch_boost && config.high_g_yaw_boost && logged("REFINEMENTS"));
    running=false; load_config(); assert(key_bindings[input::Reload]==VK_F11);
    WritePrivateProfileStringW(L"keys",L"free_look",L"UP",config_path);
    messages.clear(); load_config(); assert(key_bindings==input::Bindings{} && logged("duplicate"));
    WritePrivateProfileStringW(L"keys",L"free_look",L"Ctrl+F",config_path);
    messages.clear(); load_config(); assert(key_bindings==input::Bindings{} && logged("invalid"));
    WritePrivateProfileStringW(L"control",L"dive_enter_pitch",L"-40",config_path);
    WritePrivateProfileStringW(L"control",L"dive_exit_pitch",L"-80",config_path);
    WritePrivateProfileStringW(L"control",L"max_bank",L"nan",config_path);
    load_config(); assert(config.dive_exit_pitch== -35 && std::isfinite(config.max_bank));
    WritePrivateProfileStringW(L"control",L"turn_rate_scale",L"inf",config_path);
    WritePrivateProfileStringW(L"control",L"roll_rate_scale",L"999",config_path);
    WritePrivateProfileStringW(L"control",L"response_gain",L"-1",config_path);
    WritePrivateProfileStringW(L"control",L"countersteer_gain",L"999",config_path);
    WritePrivateProfileStringW(L"control",L"roll_lookahead",L"1.5",config_path);
    WritePrivateProfileStringW(L"control",L"high_g_pitch_rate",L"999",config_path);
    WritePrivateProfileStringW(L"control",L"high_g_enabled",L"0",config_path);
    load_config();
    assert(config.turn_rate_scale==1.1f && config.roll_rate_scale==1.4f);
    assert(config.response_gain==.8f && config.countersteer_gain==3 && config.roll_lookahead==.15f);
    assert(!config.high_g_enabled && config.high_g_pitch_rate==140);
    WritePrivateProfileStringW(L"control",L"high_g_pitch_rate",L"nan",config_path);
    load_config(); assert(config.high_g_pitch_rate==140);
    DeleteFileW(config_path);
    puts("PASS actual INI loader: defaults, custom keys, restart, invalid fallback and bounds");
}
void runtime_angle() {
    reset_pose(); config.angle_control=true; target_pitch=80; target_yaw=0;
    previous_pose_tick=test_now-17; previous_pitch=previous_yaw=previous_roll=0;
    filtered_pitch_rate=180; messages.clear(); update_commands();
    assert(command_pitch==1 && logged("ANGLE_GUIDANCE") && !logged("wantedPY="));
    // Same actual runtime function in the steep dive and near-pole branches.
    reset_pose(); config.angle_control=true; pose_roll=179; target_pitch=-70; target_yaw=0;
    messages.clear(); update_commands(); assert(command_pitch>0.9f && logged("mode=dive"));
    reset_pose(); config.angle_control=true; pose_pitch=90; target_pitch=0; target_yaw=0;
    update_commands(); assert(std::abs(command_pitch.load())>.9f);
    reset_pose(); config.angle_control=true; target_pitch=5; target_yaw=0;
    previous_pose_tick=test_now-17; previous_pitch=previous_yaw=previous_roll=0;
    filtered_pitch_rate=100; update_commands(); assert(command_pitch<0);
    reset_pose(); config.angle_control=true; target_pitch=70; target_yaw=0;
    // High-G observation must not add a neutral frame or wait for an activation flag.
    keys[VK_CONTROL]=keys[VK_SPACE]=true; update_commands(); assert(command_pitch==1);
    keys[VK_SPACE]=false; test_now+=17; update_commands(); assert(command_pitch==1);
    game_paused=true; update_commands(); assert(command_pitch==0 && command_roll==0 && command_yaw==0);
    game_paused=false; gaze_active=true; update_commands(); assert(command_pitch==0);
    gaze_active=false; front=false; update_commands(); assert(command_pitch==0);
    puts("PASS actual angle runtime: full input above old rates, dive/pole, braking, high-G and lifecycle");
}
void runtime_brake_lifecycle() {
    reset_pose(); config.angle_control=true;
    keys[VK_CONTROL]=true; keys[VK_SPACE]=true; target_pitch=70; target_yaw=0;
    update_commands(); assert(logged("brakeWeight=1.000") && command_pitch==1);
    // Seed the terminal state, then exercise actual lifecycle clearing.
    high_g_braking.step(true,.016f,.45f,90,70);
    high_g_braking.step(true,.016f,.45f,10,0); assert(high_g_braking.settling());
    game_paused=true; update_commands(); assert(!high_g_braking.settling());
    game_paused=false; keys[VK_CONTROL]=false; keys[VK_SPACE]=false;
    test_now+=100; telemetry_tick=0; messages.clear(); update_commands();
    assert(logged("brakeWeight=0.000") && logged("highGSettling=0"));
    keys[VK_CONTROL]=true; keys[VK_SPACE]=true; test_now+=17; update_commands();
    keys[VK_CONTROL]=false; keys[VK_SPACE]=false; test_now+=17; telemetry_tick=0;
    messages.clear(); update_commands();
    assert(!logged("brakeWeight=0.000") && logged("highGRequested=0"));
    enabled=false; update_commands(); enabled=true; test_now+=17; telemetry_tick=0;
    messages.clear(); update_commands(); assert(logged("brakeWeight=0.000"));
    puts("PASS actual high-G immediate input, release tail, terminal diagnostics and pause/toggle reset");
}
void runtime_angle_config() {
    GetFullPathNameW(L"test-angle-config.ini",MAX_PATH,config_path,nullptr);
    DeleteFileW(config_path); reset_pose(); running=false; config=Config{}; load_config();
    assert(config.angle_control && config.pitch_full_input_angle==20 && config.roll_full_input_angle==45);
    assert(config.turn_brake_lookahead==.10f && config.roll_brake_lookahead==.25f);
    assert(config.angle_pitch_yaw_ratio==5.5f && config.angle_high_g_pitch_yaw_ratio==11);
    const wchar_t* names[]={L"pitch_full_input_angle",L"roll_full_input_angle",L"turn_brake_lookahead",L"roll_brake_lookahead",L"angle_pitch_yaw_ratio",L"angle_high_g_pitch_yaw_ratio",L"roll_small_input_scale",L"roll_large_input_scale",L"roll_small_bank",L"roll_brake_gain",L"high_g_brake_lookahead",L"high_g_brake_gain",L"high_g_brake_hold",L"high_g_pitch_priority"};
    const float defaults[]={20,45,.10f,.25f,5.5f,11,.5f,1.35f,25,1.5f,.30f,1.5f,.45f,1};
    for(int i=0;i<14;++i) for(const auto invalid:{L"nan",L"inf",L"-1",L"9999",L"12garbage",L"\"   \""}) {
        WritePrivateProfileStringW(L"control",names[i],invalid,config_path); messages.clear(); load_config();
        const float values[]={config.pitch_full_input_angle,config.roll_full_input_angle,config.turn_brake_lookahead,config.roll_brake_lookahead,config.angle_pitch_yaw_ratio,config.angle_high_g_pitch_yaw_ratio,config.roll_small_input_scale,config.roll_large_input_scale,config.roll_small_bank,config.roll_brake_gain,config.high_g_brake_lookahead,config.high_g_brake_gain,config.high_g_brake_hold,config.high_g_pitch_priority};
        assert(values[i]==defaults[i] && logged("ANGLE_CONFIG invalid"));
        WritePrivateProfileStringW(L"control",names[i],nullptr,config_path);
    }
    for(const auto invalid:{L"nan",L"2",L"-1",L"0.5",L"bad"}) {
        WritePrivateProfileStringW(L"control",L"angle_control",invalid,config_path); messages.clear(); load_config();
        assert(config.angle_control && logged("ANGLE_CONFIG invalid"));
    }
    WritePrivateProfileStringW(L"control",L"angle_control",L"0",config_path);
    running=true; target_pitch=20; target_yaw=30; messages.clear(); load_config();
    assert(!config.angle_control && controller_reset_pending);
    test_now+=17; update_commands();
    assert(command_pitch==0 && command_roll==0 && command_yaw==0 && logged("CONTROLLER_RESET"));
    assert(target_pitch==20 && target_yaw==30 && !controller_reset_pending);
    test_now+=17; update_commands(); assert(command_roll!=0);
    const float saved_pitch=target_pitch.load(),saved_yaw=target_yaw.load();
    WritePrivateProfileStringW(L"control",L"angle_control",L"1",config_path);
    WritePrivateProfileStringW(L"control",L"pitch_full_input_angle",L"22",config_path);
    WritePrivateProfileStringW(L"control",L"turn_brake_lookahead",L"0.12",config_path);
    load_config(); assert(config.angle_control && config.pitch_full_input_angle==22 && config.turn_brake_lookahead==.12f);
    test_now+=17; update_commands(); assert(command_pitch==0 && command_roll==0);
    assert(target_pitch==saved_pitch && target_yaw==saved_yaw);
    test_now+=17; update_commands(); assert(command_roll!=0);
    // All old F-14D settings remain available when switching back.
    WritePrivateProfileStringW(L"control",L"max_bank",L"89",config_path);
    WritePrivateProfileStringW(L"control",L"roll_rate_scale",L"1.35",config_path);
    WritePrivateProfileStringW(L"control",L"roll_lookahead",L"0.12",config_path);
    WritePrivateProfileStringW(L"control",L"high_g_yaw_boost",L"1",config_path);
    WritePrivateProfileStringW(L"control",L"diagnostics",L"1",config_path); load_config();
    assert(config.max_bank==89 && config.roll_rate_scale==1.35f && config.roll_lookahead==.12f && config.high_g_yaw_boost && config.diagnostics);
    DeleteFileW(config_path); running=false;
    puts("PASS actual angle INI validation/defaults, hot reload, neutral switch, retained target and personal settings");
}
void runtime_target_selection_context() {
    reset_pose(); running=true; aircraft=0x123400; bridge_thread=GetCurrentThreadId();
    config.mouse_target_priority=true; target_pitch=30; target_yaw=90;
    pose_pitch=-20; pose_yaw=0; look_pitch=-40; look_yaw=-90;
    auto context=target_selection_context();
    assert(context.enabled && context.usable && context.pawn==0x123400);
    assert(std::abs(context.aim.x)<1e-5 && std::abs(context.aim.y-std::sqrt(.75))<1e-5 && std::abs(context.aim.z-.5)<1e-5);
    keys[key_bindings[input::FreeLook]]=true; assert(!target_selection_context().usable);
    keys[key_bindings[input::FreeLook]]=false;
    for(auto* state:{&running,&active,&enabled}) {
        state->store(false); assert(!target_selection_context().usable); state->store(true);
    }
    for(auto* state:{&game_paused,&gaze_active}) {
        state->store(true); assert(!target_selection_context().usable); state->store(false);
    }
    front=false; assert(!target_selection_context().usable); front=true;
    pose_tick=test_now-251; assert(!target_selection_context().usable); pose_tick=test_now;
    aircraft=0; assert(!target_selection_context().usable); aircraft=0x123400;
    bridge_thread=0; assert(!target_selection_context().enabled); bridge_thread=GetCurrentThreadId();
    config.mouse_target_priority=false; assert(!target_selection_context().enabled);
    GetFullPathNameW(L"test-target-config.ini",MAX_PATH,config_path,nullptr); DeleteFileW(config_path);
    load_config(); assert(!config.mouse_target_priority);
    for(const auto invalid:{L"nan",L"inf",L"2",L"-1",L"0.5",L"bad",L"1bad"}) {
        WritePrivateProfileStringW(L"control",L"mouse_target_priority",invalid,config_path);
        config.mouse_target_priority=true; load_config(); assert(!config.mouse_target_priority);
    }
    for(const auto setting:{L"1",L"0",L"1"}) {
        WritePrivateProfileStringW(L"control",L"mouse_target_priority",setting,config_path); load_config();
        assert(config.mouse_target_priority==(setting[0]==L'1'));
    }
    running=false; DeleteFileW(config_path);
    puts("PASS actual target context: PW aim independent of camera/nose, state/thread guards and fail-closed INI reload");
}
void runtime_source_config(const wchar_t* source,bool target_priority=false) {
    GetFullPathNameW(L"test-source-config.ini",MAX_PATH,config_path,nullptr);
    assert(CopyFileW(source,config_path,FALSE));
    reset_pose(); config=Config{}; running=false; load_config();
    assert(config.angle_control && config.pitch_full_input_angle==20 && config.turn_brake_lookahead==.1f);
    assert(config.roll_full_input_angle==45 && config.roll_brake_lookahead==.25f);
    assert(config.angle_pitch_yaw_ratio==5.5f && config.angle_high_g_pitch_yaw_ratio==11);
    assert(config.max_bank==89 && config.roll_rate_scale==1.35f && config.roll_lookahead==.12f);
    assert(config.roll_small_input_scale==.5f && config.roll_large_input_scale==1.35f && config.roll_small_bank==25 && config.roll_brake_gain==1.5f && config.high_g_brake_lookahead==.30f && config.high_g_brake_gain==1.5f && config.high_g_brake_hold==.45f && config.high_g_pitch_priority==1);
    assert(config.high_g_yaw_boost && config.diagnostics && key_bindings==input::Bindings{} && config.mouse_target_priority==target_priority);
    running=true; load_config(); assert(!controller_reset_pending && config.angle_control);
    running=false; DeleteFileW(config_path);
    puts("PASS shipped INI actual startup/reload, angle defaults and preserved F-14D preset");
}
void runtime_rollout() {
    const auto arrival=[](bool enabled) {
        reset_pose(); config.angle_control=true; config.rollout_coordination=enabled;
        pose_pitch=-2.79f; pose_yaw=165.08f; pose_roll=-71.86f;
        target_pitch=-5.21f; target_yaw=163.75f;
        previous_pose_tick=test_now-17;
        previous_pitch=pose_pitch; previous_yaw=pose_yaw; previous_roll=pose_roll;
        filtered_pitch_rate=43; filtered_yaw_rate=-6; filtered_roll_rate=18;
        messages.clear(); update_commands();
    };
    arrival(false); const float pq=command_pitch,py=command_yaw,pr=command_roll;
    arrival(true);
    assert(command_pitch<0 && std::abs(command_pitch.load())>=std::abs(pq) && command_yaw==py && command_roll!=pr);
    assert(terminal_braking.active() && logged("terminalActive=1") && logged("terminalFade="));
    assert(rollout_coordinator.state()==flight::RolloutPhase::Waiting && logged("rolloutPhase=1") && logged("rolloutWait="));
    for(const auto& m:messages) assert(m.size()<1024);
    const auto reset=[] { return rollout_coordinator.state()==flight::RolloutPhase::Tracking && !terminal_braking.active() && terminal_braking.fade_speed==0; };
    game_paused=true; update_commands(); assert(reset());
    arrival(true); front=false; update_commands(); assert(reset());
    arrival(true); keys[key_bindings[input::Pitch1]]=true; update_commands();
    assert(reset());
    arrival(true); keys[key_bindings[input::Yaw1]]=true; update_commands();
    assert(reset());
    arrival(true); keys[key_bindings[input::Roll1]]=true; update_commands(); assert(reset());
    arrival(true); enabled=false; update_commands(); assert(reset());
    arrival(true); gaze_active=true; update_commands(); assert(reset());
    arrival(true); test_now+=300; update_commands(); assert(reset());
    GetFullPathNameW(L"test-rollout-config.ini",MAX_PATH,config_path,nullptr);
    DeleteFileW(config_path); reset_pose(); running=false; load_config(); assert(config.rollout_coordination);
    for(const auto invalid:{L"nan",L"inf",L"2",L"-1",L"0.5",L"bad",L"0junk"}) {
        WritePrivateProfileStringW(L"control",L"rollout_coordination",invalid,config_path);
        messages.clear(); load_config(); assert(config.rollout_coordination && logged("ANGLE_CONFIG invalid"));
    }
    running=true; target_pitch=20; target_yaw=30;
    WritePrivateProfileStringW(L"control",L"rollout_coordination",L"0",config_path); load_config();
    assert(!config.rollout_coordination && controller_reset_pending);
    update_commands(); assert(command_pitch==0 && command_yaw==0 && command_roll==0);
    assert(reset());
    assert(target_pitch==20 && target_yaw==30);
    WritePrivateProfileStringW(L"control",L"rollout_coordination",L"1",config_path); load_config();
    assert(config.rollout_coordination && controller_reset_pending);
    update_commands(); assert(command_pitch==0 && target_pitch==20 && target_yaw==30);
    running=false; DeleteFileW(config_path);
    puts("PASS actual rollout runtime: unchanged yaw, separate roll, terminal brake, lifecycle/keys, bounded logs and INI switch");
}
int wmain(int argc,wchar_t** argv) {
    _set_error_mode(_OUT_TO_STDERR); setvbuf(stdout,nullptr,_IONBF,0);
    runtime_guidance(); runtime_axes(); runtime_pose_guard(); runtime_config(); runtime_angle(); runtime_brake_lifecycle(); runtime_angle_config();
    runtime_rollout(); runtime_target_selection_context(); assert(argc==3); runtime_source_config(argv[1]); runtime_source_config(argv[2],true);
}
'''
    destination.write_text(prefix+config+globals_+loader+update+process+target_context+tests,encoding='utf-8')
