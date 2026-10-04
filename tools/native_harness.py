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
#include "pose_guard.h"
#include "key_bindings.h"
#include "free_look.h"
#include "version.h"
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
    config=Config{}; key_bindings=input::Bindings{};
    std::fill(std::begin(keys),std::end(keys),false); front=true;
    active=true; enabled=true; game_paused=false; gaze_active=false;
    previous_pose_tick=0; test_now+=100; pose_tick=test_now;
    pose_pitch=0; pose_yaw=0; pose_roll=0;
    camera_pitch=0; camera_yaw=0; camera_roll=0;
    target_pitch=0; target_yaw=30;
    mouse_dx=0; mouse_dy=0; free_look.reset(); bank_guidance.reset();
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
void runtime_packaged_configs(const wchar_t* defaults,const wchar_t* preset) {
    for (int personal=0;personal<2;++personal) {
        const wchar_t* path=personal ? preset : defaults;
        assert(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES);
        assert(GetFullPathNameW(path,MAX_PATH,config_path,nullptr)<MAX_PATH);
        config=Config{}; running=false; messages.clear(); load_config();
        assert(config.max_bank==(personal ? 89 : 85));
        assert(config.roll_rate_scale==(personal ? 1.35f : 1.25f));
        assert(config.roll_lookahead==(personal ? .12f : .10f));
        assert(config.high_g_yaw_boost==bool(personal) && config.diagnostics==bool(personal));
        assert(config.turn_rate_scale==1.2f && config.response_gain==1.2f && config.countersteer_gain==2);
        assert(config.high_g_enabled && config.high_g_pitch_rate==110);
        assert(key_bindings==input::Bindings{} && !logged("invalid") && !logged("duplicate"));
        running=true; config.max_bank=20; load_config();
        assert(config.max_bank==(personal ? 89 : 85) && key_bindings==input::Bindings{});
    }
    puts("PASS packaged default and F-14D INI through actual native loader: startup/reload/bindings");
}
int wmain(int argc,wchar_t** argv) {
    _set_error_mode(_OUT_TO_STDERR);
    assert(argc==3);
    runtime_guidance(); runtime_axes(); runtime_pose_guard(); runtime_config();
    runtime_packaged_configs(argv[1],argv[2]);
}
'''
    destination.write_text(prefix+config+globals_+loader+update+process+tests,encoding='utf-8')
