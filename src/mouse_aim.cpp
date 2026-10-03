// AC8 Mouse Aim: offline-only closed-loop mouse flight control for ACE COMBAT 8.
// Reads aircraft attitude supplied by UE4SS Lua, captures non-exclusive mouse
// deltas, and replaces only the three player input axes. Flight-model state is
// never edited.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <mutex>
#include "vendor/minhook/include/MinHook.h"
#include "yaw_signature.h"
#include "flight_math.h"
#include "free_look.h"

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace {
using Processor = uintptr_t(__fastcall*)(unsigned char*, unsigned char*);
using GetRawInputDataFn = UINT(WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);

struct Config {
    float sensitivity = 0.10f;
    float roll_gain = 0.022f;
    float pitch_gain = 0.026f;
    float yaw_gain = 0.012f;
    float turn_pull = 0.42f;
    float max_bank = 65.0f;
    float smoothing = 0.12f;
    float roll_damping = 0.008f;
    float pitch_damping = 0.010f;
    float yaw_damping = 0.30f;
    float dead_zone = 1.25f;
    float pitch_sign = 1.0f;
    float roll_sign = -1.0f;
    float yaw_sign = -1.0f;
    float horizontal_fov = 100.0f;
    float vertical_fov = 62.0f;
    int pitch_slot = 0;
    int roll_slot = 2;
};

struct Pose {
    uintptr_t pawn = 0;
    float pitch = 0, yaw = 0, roll = 0;
    unsigned long long tick = 0;
};

HMODULE self_module{};
Processor original{};
void* hook_address{};
bool hook_created = false;
std::atomic<bool> running{false};
std::atomic<bool> enabled{true};
std::atomic<bool> hud_enabled{true};
std::atomic<bool> game_paused{false};
std::atomic<bool> gaze_active{false};
std::atomic<bool> resume_center_requested{false};
std::atomic<bool> active{false};
std::atomic<uintptr_t> aircraft{0};
std::atomic<float> pose_pitch{0}, pose_yaw{0}, pose_roll{0};
std::atomic<float> camera_pitch{0}, camera_yaw{0}, camera_roll{0};
std::atomic<float> view_fov{100};
std::atomic<float> view_offset_x{0}, view_offset_y{0}, view_offset_z{0};
std::atomic<float> roll_reference{0};
std::atomic<unsigned long long> pose_tick{0};
std::atomic<long> mouse_dx{0}, mouse_dy{0};
std::atomic<float> command_pitch{0}, command_roll{0}, command_yaw{0};
std::atomic<float> target_pitch{0}, target_yaw{0};
// Camera destination is independent of the flight target while F is held.
std::atomic<float> look_pitch{0}, look_yaw{0};
flight::FreeLook free_look;
std::atomic<bool> recenter_requested{true};
std::atomic<unsigned long long> calls{0}, writes{0};
float previous_pitch{}, previous_yaw{}, previous_roll{};
float filtered_pitch_rate{}, filtered_yaw_rate{}, filtered_roll_rate{};
flight::LevelBlend roll_level_blend;
unsigned long long previous_pose_tick{}, telemetry_tick{};
Config config;
wchar_t module_folder[MAX_PATH]{};
wchar_t status_path[MAX_PATH]{};
wchar_t config_path[MAX_PATH]{};
wchar_t request_path[MAX_PATH]{};
wchar_t pipe_name[] = L"\\\\.\\pipe\\AC8MouseAim";
HANDLE journal = INVALID_HANDLE_VALUE;
HWND game_window{};
HWND overlay_window{};
IDirectInput8W* direct_input{};
IDirectInputDevice8W* mouse_device{};
GetRawInputDataFn original_get_raw_input_data{};
std::atomic<bool> raw_input_hooked{false};

float clamp_axis(float value) { return std::clamp(value, -1.0f, 1.0f); }
float wrap_degrees(float value) {
    while (value > 180.0f) value -= 360.0f;
    while (value < -180.0f) value += 360.0f;
    return value;
}

void init_paths() {
    if (module_folder[0]) return;
    GetModuleFileNameW(self_module, module_folder, MAX_PATH);
    if (auto* slash = wcsrchr(module_folder, L'\\')) *slash = 0;
    swprintf_s(status_path, L"%s\\mouse-aim-status.txt", module_folder);
    swprintf_s(config_path, L"%s\\..\\config.ini", module_folder);
    swprintf_s(request_path, L"%s\\mouse-aim-request.txt", module_folder);
}

void log_line(const char* format, ...) {
    init_paths();
    char message[1024]{};
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (journal == INVALID_HANDLE_VALUE) {
        wchar_t folder[MAX_PATH]{};
        swprintf_s(folder, L"%s\\..\\Logs", module_folder);
        CreateDirectoryW(folder, nullptr);
        wchar_t path[MAX_PATH]{};
        for (int index = 1; index < 10000; ++index) {
            swprintf_s(path, L"%s\\MouseAim-%05d.log", folder, index);
            journal = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (journal != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) break;
        }
    }
    if (journal != INVALID_HANDLE_VALUE) {
        char line[1200]{};
        int length = snprintf(line, sizeof(line), "%llu %s\r\n", GetTickCount64(), message);
        DWORD written{};
        WriteFile(journal, line, static_cast<DWORD>(length), &written, nullptr);
        // Let Windows buffer normal diagnostics; never force a disk flush on
        // the input/camera thread for every telemetry line.
    }
    FILE* status{};
    if (_wfopen_s(&status, status_path, L"w") == 0 && status) {
        fprintf(status, "%s\n", message);
        fclose(status);
    }
}

float read_config_float(const wchar_t* key, float fallback) {
    wchar_t value[64]{};
    wchar_t fallback_text[64]{};
    swprintf_s(fallback_text, L"%.6f", fallback);
    GetPrivateProfileStringW(L"control", key, fallback_text, value, 64, config_path);
    wchar_t* end{};
    float parsed = wcstof(value, &end);
    return end != value && std::isfinite(parsed) ? parsed : fallback;
}

int read_config_int(const wchar_t* key, int fallback) {
    return GetPrivateProfileIntW(L"control", key, fallback, config_path);
}

void load_config() {
    init_paths();
    config.sensitivity = std::clamp(read_config_float(L"sensitivity", config.sensitivity), 0.01f, 1.0f);
    config.roll_gain = std::clamp(read_config_float(L"roll_gain", config.roll_gain), 0.001f, 0.2f);
    config.pitch_gain = std::clamp(read_config_float(L"pitch_gain", config.pitch_gain), 0.001f, 0.2f);
    config.yaw_gain = std::clamp(read_config_float(L"yaw_gain", config.yaw_gain), 0.0f, 0.2f);
    config.turn_pull = std::clamp(read_config_float(L"turn_pull", config.turn_pull), 0.0f, 1.0f);
    config.max_bank = std::clamp(read_config_float(L"max_bank", config.max_bank), 20.0f, 89.0f);
    config.smoothing = std::clamp(read_config_float(L"smoothing", config.smoothing), 0.02f, 1.0f);
    config.roll_damping = std::clamp(read_config_float(L"roll_damping", config.roll_damping), 0.0f, 0.05f);
    config.pitch_damping = std::clamp(read_config_float(L"pitch_damping", config.pitch_damping), 0.0f, 0.05f);
    config.yaw_damping = std::clamp(read_config_float(L"yaw_damping", config.yaw_damping), 0.0f, 2.0f);
    config.dead_zone = std::clamp(read_config_float(L"dead_zone", config.dead_zone), 0.1f, 5.0f);
    config.pitch_sign = read_config_float(L"pitch_sign", config.pitch_sign) < 0 ? -1.0f : 1.0f;
    config.roll_sign = read_config_float(L"roll_sign", config.roll_sign) < 0 ? -1.0f : 1.0f;
    config.yaw_sign = read_config_float(L"yaw_sign", config.yaw_sign) < 0 ? -1.0f : 1.0f;
    config.horizontal_fov = std::clamp(read_config_float(L"horizontal_fov", config.horizontal_fov), 40.0f, 170.0f);
    config.vertical_fov = std::clamp(read_config_float(L"vertical_fov", config.vertical_fov), 30.0f, 120.0f);
    config.pitch_slot = std::clamp(read_config_int(L"pitch_slot", config.pitch_slot), 0, 2);
    config.roll_slot = std::clamp(read_config_int(L"roll_slot", config.roll_slot), 0, 2);
    if (config.roll_slot == config.pitch_slot || config.pitch_slot == 1 || config.roll_slot == 1) {
        config.pitch_slot = 0;
        config.roll_slot = 2;
    }
}

struct WindowCandidate {
    HWND window{};
    long long area{};
};

BOOL CALLBACK find_game_window(HWND window, LPARAM result) {
    DWORD process{};
    GetWindowThreadProcessId(window, &process);
    if (process == GetCurrentProcessId() && window != overlay_window &&
        IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr) {
        wchar_t class_name[64]{};
        GetClassNameW(window, class_name, 64);
        if (wcscmp(class_name, L"AC8MouseAimOverlay") == 0) return TRUE;
        RECT client{};
        if (GetClientRect(window, &client)) {
            const long long width = client.right - client.left;
            const long long height = client.bottom - client.top;
            const long long area = width * height;
            auto* candidate = reinterpret_cast<WindowCandidate*>(result);
            if (width >= 640 && height >= 360 && area > candidate->area) {
                candidate->window = window;
                candidate->area = area;
            }
        }
    }
    return TRUE;
}

HWND locate_game_window() {
    WindowCandidate candidate{};
    EnumWindows(find_game_window, reinterpret_cast<LPARAM>(&candidate));
    return candidate.window;
}

bool foreground_is_game() {
    HWND foreground = GetForegroundWindow();
    DWORD process{};
    GetWindowThreadProcessId(foreground, &process);
    const bool is_game = process == GetCurrentProcessId() && foreground != overlay_window;
    if (is_game) game_window = foreground;
    return is_game;
}

UINT WINAPI capture_get_raw_input_data(HRAWINPUT input, UINT command, LPVOID data,
                                       PUINT size, UINT header_size) {
    const UINT result = original_get_raw_input_data(input, command, data, size, header_size);
    if (result != static_cast<UINT>(-1) && command == RID_INPUT && data &&
        result >= sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE)) {
        const auto* raw = static_cast<const RAWINPUT*>(data);
        if (raw->header.dwType == RIM_TYPEMOUSE &&
            !(raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) &&
            foreground_is_game() && active.load() && enabled.load()) {
            if(!game_paused.load() && !gaze_active.load()) {
                mouse_dx.fetch_add(raw->data.mouse.lLastX);
                mouse_dy.fetch_add(raw->data.mouse.lLastY);
            }
        }
    }
    return result;
}

bool prepare_raw_input_capture() {
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) return false;
    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        if (!descriptor->OriginalFirstThunk) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->OriginalFirstThunk);
        auto* imports = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++imports) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            const auto* by_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char*>(by_name->Name), "GetRawInputData") != 0) continue;
            original_get_raw_input_data = reinterpret_cast<GetRawInputDataFn>(imports->u1.Function);
            DWORD previous{};
            if (!VirtualProtect(&imports->u1.Function, sizeof(imports->u1.Function),
                                PAGE_READWRITE, &previous)) return false;
            InterlockedExchangePointer(reinterpret_cast<void**>(&imports->u1.Function),
                                       reinterpret_cast<void*>(&capture_get_raw_input_data));
            DWORD ignored{};
            VirtualProtect(&imports->u1.Function, sizeof(imports->u1.Function), previous, &ignored);
            raw_input_hooked.store(true);
            return true;
        }
    }
    return false;
}

bool prepare_mouse() {
    if (!game_window) game_window = locate_game_window();
    if (!game_window) return false;
    if (!direct_input && FAILED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION,
            IID_IDirectInput8W, reinterpret_cast<void**>(&direct_input), nullptr))) return false;
    if (!mouse_device && FAILED(direct_input->CreateDevice(GUID_SysMouse, &mouse_device, nullptr))) return false;
    if (FAILED(mouse_device->SetDataFormat(&c_dfDIMouse2))) return false;
    if (FAILED(mouse_device->SetCooperativeLevel(game_window, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND))) return false;
    return SUCCEEDED(mouse_device->Acquire()) || GetLastError() == ERROR_SUCCESS;
}

void mouse_loop() {
    bool f8_down = false, f9_down = false;
    ULONGLONG snapshot_tick=0;
    ULONGLONG published_tick=0;
    bool published_on=false;
    float published_pitch=0, published_yaw=0;
    uintptr_t published_pawn=0;
    while (running.load()) {
        if (!raw_input_hooked.load()) {
            if (!mouse_device && !prepare_mouse()) {
                Sleep(50);
                continue;
            }
            DIMOUSESTATE2 state{};
            HRESULT result = mouse_device->GetDeviceState(sizeof(state), &state);
            if (FAILED(result)) {
                mouse_device->Acquire();
            } else if (foreground_is_game() && active.load()) {
                if(!game_paused.load() && !gaze_active.load()) {
                    mouse_dx.fetch_add(state.lX);
                    mouse_dy.fetch_add(state.lY);
                }
            }
        }
        static bool f7_down=false;
        bool f7=(GetAsyncKeyState(VK_F7)&0x8000)!=0;
        if(f7 && !f7_down && foreground_is_game()) {
            hud_enabled.store(!hud_enabled.load());
            log_line("HUD only: %s",hud_enabled.load()?"ON":"OFF");
        }
        f7_down=f7;
        bool f8 = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        bool f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        if (f8 && !f8_down) enabled.store(!enabled.load());
        if (f9 && !f9_down) recenter_requested.store(true);
        f8_down = f8;
        f9_down = f9;
        const auto now=GetTickCount64();
        if (now-snapshot_tick>=33) {
            snapshot_tick=now;
            const bool on=active.load() && enabled.load() && now-pose_tick.load()<250 &&
                foreground_is_game();
            const float p=look_pitch.load(), y=look_yaw.load();
            const uintptr_t pawn=aircraft.load();
            // No repeated replacement of an identical target file during steady
            // flight. A 0.5s heartbeat stays well inside Lua's 2s stale timeout.
            const bool changed=on!=published_on || p!=published_pitch ||
                y!=published_yaw || pawn!=published_pawn;
            if (changed || now-published_tick>=500) {
                wchar_t temp[MAX_PATH]{}, path[MAX_PATH]{};
                swprintf_s(temp,L"%s\\camera-target.tmp",module_folder);
                swprintf_s(path,L"%s\\camera-target.txt",module_folder);
                FILE* out{};
                if (_wfopen_s(&out,temp,L"w")==0 && out) {
                    fprintf(out,"%llu %d %.8f %.8f %llX\n",now,on?1:0,p,y,
                        static_cast<unsigned long long>(pawn));
                    fclose(out);
                    if (MoveFileExW(temp,path,MOVEFILE_REPLACE_EXISTING)) {
                        published_tick=now; published_on=on; published_pitch=p;
                        published_yaw=y; published_pawn=pawn;
                    }
                }
            }
        }
        Sleep(4);
    }
}

void update_commands() {
    using namespace flight;
    if (!active.load() || !enabled.load() || game_paused.load() || gaze_active.load()) {
        free_look.reset();
        command_pitch.store(0); command_roll.store(0); command_yaw.store(0);
        if(game_paused.load() || gaze_active.load()) { mouse_dx.store(0); mouse_dy.store(0); }
        previous_pose_tick = 0;
        return;
    }
    const auto now = GetTickCount64();
    const float dt = previous_pose_tick ? std::clamp((now-previous_pose_tick)/1000.0f,0.001f,0.1f) : 1.0f/60;
    const Basis b = basis(pose_pitch.load(),pose_yaw.load(),pose_roll.load());
    const Basis old = basis(previous_pitch,previous_yaw,previous_roll);
    // Estimate body angular velocity from the moving basis, avoiding Euler wrap/pole artifacts.
    V omega = (cross(old.f,b.f)+cross(old.r,b.r)+cross(old.u,b.u))*(0.5f/dt/rad);
    float a = 1-std::exp(-12*dt);
    if (!previous_pose_tick) { omega={}; roll_level_blend.reset(); }
    filtered_pitch_rate += (-dot(omega,b.r)-filtered_pitch_rate)*a;
    filtered_yaw_rate += (dot(omega,b.u)-filtered_yaw_rate)*a;
    filtered_roll_rate += (-dot(omega,b.f)-filtered_roll_rate)*a;
    previous_pitch=pose_pitch.load(); previous_yaw=pose_yaw.load(); previous_roll=pose_roll.load();
    previous_pose_tick=now;
    V aim=basis(target_pitch.load(),target_yaw.load(),0).f;
    const bool manual = !foreground_is_game();
    if (recenter_requested.exchange(false) || manual) {
        aim=b.f;
        mouse_dx.store(0); mouse_dy.store(0);
    }
    const Basis view=basis(camera_pitch.load(),camera_yaw.load(),camera_roll.load());
    if(resume_center_requested.exchange(false)) {
        // Intersect the camera-centre ray with the HUD's 500m aim sphere.
        V offset{view_offset_x.load(),view_offset_y.load(),view_offset_z.load()};
        const float along=dot(offset,view.f);
        const float t=-along+std::sqrt(std::max(0.0f,along*along+50000.0f*50000.0f-dot(offset,offset)));
        aim=unit(offset+view.f*t);
        mouse_dx.store(0); mouse_dy.store(0);
        filtered_pitch_rate=filtered_yaw_rate=filtered_roll_rate=0;
    }
    const bool looking=!manual && (GetAsyncKeyState('F')&0x8000)!=0;
    const V camera_target=free_look.step(looking,aim,view,
        mouse_dx.exchange(0)*config.sensitivity,mouse_dy.exchange(0)*config.sensitivity);
    look_pitch.store(flight::pitch(camera_target)); look_yaw.store(flight::yaw(camera_target));
    target_pitch.store(flight::pitch(aim)); target_yaw.store(flight::yaw(aim));
    const float f=dot(aim,b.f), right=dot(aim,b.r), up=dot(aim,b.u);
    const float angle=std::acos(std::clamp(f,-1.0f,1.0f))/rad;
    // Direct MouseFlight Plane.RunAutopilot port: normalized local target * 5.
    // Unity (right, up, forward) -> Unreal (forward, right, up).
    // Positive UE Pitch raises the nose; positive UE Roll lowers the right wing.
    const float p=up*5.0f, y=right*5.0f;
    // AC-specific rate-limited PD control. Body rates are degrees/second.
    // The reference proportional demand alone does not brake AC's fast roll response.
    const float pitch_error=std::atan2(up,std::max(0.02f,f))/rad;
    const float yaw_error=std::atan2(right,std::max(0.02f,f))/rad;
    const float level_error=std::atan2(b.r.z,b.u.z)/rad;
    const float near_blend=tracking_weight(angle);
    // Bank until the target lies above the aircraft, then pull toward it.
    const float turn_bank_error=std::clamp(std::atan2(right,std::max(0.12f,up))/rad,-90.0f,90.0f);
    const float roll_blend=roll_level_blend.step(angle,dt);
    const float roll_error=level_error*(1-roll_blend)+turn_bank_error*roll_blend;
    // Separate fast tracking from gentler final leveling. Braking remains available.
    const float roll_speed=70.0f+70.0f*roll_blend;
    const float rcmd=arrival_command(roll_error,filtered_roll_rate,roll_speed,240.0f,3.5f,1.0f,170.0f,1.0f);
    const float final_gain=1.0f-near_blend;
    const float pcmd=arrival_command(pitch_error,filtered_pitch_rate,45.0f,90.0f,1.8f+0.8f*final_gain,0.2f,55.0f,0.85f);
    const float yaw_rate=std::clamp(dead(yaw_error,0.2f)*(1.2f+0.6f*final_gain),-7.0f,7.0f);
    const float ycmd=std::clamp((yaw_rate-filtered_yaw_rate*0.6f)/10.0f,-0.7f,0.7f);
    command_pitch.store(manual?0:pcmd*config.pitch_sign);
    command_yaw.store(manual?0:ycmd*config.yaw_sign);
    command_roll.store(manual?0:rcmd*config.roll_sign);
    if(now-telemetry_tick>=10000) {
        telemetry_tick=now;
        log_line("0.2.18 adaptive localScaled=(%.3f,%.3f) angle=%.1f roll=%.1f rollErrorDeg=%.3f bodyrate=(%.1f,%.1f,%.1f) cmd=(%.2f,%.2f,%.2f) rollBlend=%.3f leveling=%d",
          p,y,angle,pose_roll.load(),roll_error,filtered_pitch_rate,filtered_yaw_rate,filtered_roll_rate,
          command_pitch.load(),command_yaw.load(),command_roll.load(),roll_blend,roll_level_blend.leveling);
    }
}
#include "native_camera.h"

void parse_pose(const char* line) {
    if(strncmp(line,"GAZE ",5)==0) {
        int value=-1;
        if(sscanf_s(line,"GAZE %d",&value)==1 && (value==0 || value==1)) {
            if(gaze_active.exchange(value!=0)!=(value!=0)) {
                mouse_dx.store(0); mouse_dy.store(0);
                command_pitch.store(0); command_yaw.store(0); command_roll.store(0);
                log_line("gaze: %s",value?"native camera and controls; mouse target frozen":"mouse mode resumed");
            }
        }
        return;
    }
    if(strncmp(line,"PAUSE ",6)==0) {
        int value=-1;
        if(sscanf_s(line,"PAUSE %d",&value)==1 && (value==0 || value==1)) {
            const bool was_paused=game_paused.exchange(value!=0);
            if(was_paused!=(value!=0)) {
                mouse_dx.store(0); mouse_dy.store(0);
                if(was_paused) resume_center_requested.store(true);
                else { command_pitch.store(0); command_roll.store(0); command_yaw.store(0); }
                log_line("game pause: %s",value?"paused; aim frozen":"resumed; centre aim on next pose");
            }
        }
        return;
    }
    if(strncmp(line,"CAMERA ",7)==0) { receive_camera(line); return; }
    unsigned long long address{};
    float pitch{}, yaw{}, roll{}, view_pitch{}, view_yaw{}, view_roll{}, fov=100;
    float ox{},oy{},oz{};
    const int fields = sscanf_s(line, "POSE %llx %f %f %f %f %f %f %f %f %f %f", &address,
                                &pitch, &yaw, &roll, &view_pitch, &view_yaw, &view_roll,&fov,&ox,&oy,&oz);
    if (fields != 4 && fields != 7 && fields != 8 && fields != 11) return;
    if(!std::isfinite(ox)||!std::isfinite(oy)||!std::isfinite(oz)) return;
    view_offset_x.store(ox); view_offset_y.store(oy); view_offset_z.store(oz);
    if(std::isfinite(fov)) view_fov.store(std::clamp(fov,30.0f,150.0f));
    if (fields == 4) {
        view_pitch = pitch;
        view_yaw = yaw;
        view_roll = roll;
    }
    if (address < 0x10000 || address > 0x00007fffffff0000ull || (address & 7) ||
        !std::isfinite(pitch) || !std::isfinite(yaw) || !std::isfinite(roll) ||
        !std::isfinite(view_pitch) || !std::isfinite(view_yaw) || !std::isfinite(view_roll)) return;
    if (aircraft.load() != static_cast<uintptr_t>(address)) {
        aircraft.store(static_cast<uintptr_t>(address));
        roll_reference.store(roll);
        previous_pose_tick = 0;
        telemetry_tick = 0;
        recenter_requested.store(true);
        free_look.reset();
        log_line("aircraft acquired 0x%llX pose=(%.3f,%.3f,%.3f) camera=(%.3f,%.3f,%.3f)",
                 address, pitch, yaw, roll, view_pitch, view_yaw, view_roll);
    }
    pose_pitch.store(pitch);
    pose_yaw.store(yaw);
    pose_roll.store(roll);
    camera_pitch.store(view_pitch);
    camera_yaw.store(view_yaw);
    camera_roll.store(view_roll);
    pose_tick.store(GetTickCount64());
    active.store(true);
    update_commands();
}

void pipe_loop() {
    while (running.load()) {
        HANDLE pipe = CreateNamedPipeW(pipe_name, PIPE_ACCESS_INBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(500);
            continue;
        }
        BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : GetLastError() == ERROR_PIPE_CONNECTED;
        if (connected) {
            char buffer[4096]{};
            std::string pending;
            DWORD read{};
            while (running.load() && ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read) {
                pending.append(buffer, read);
                size_t newline{};
                while ((newline = pending.find('\n')) != std::string::npos) {
                    parse_pose(pending.substr(0, newline).c_str());
                    pending.erase(0, newline + 1);
                }
            }
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        active.store(false);
        aircraft.store(0);
    }
}

void draw_overlay(HWND window, HDC dc, const RECT& rect, uint32_t* pixels) {
        RECT alpha_rects[4]={{20,35,1100,85}};
        unsigned alpha_count=1;
        FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,RGB(245,245,245));
        char label[160]{};
        snprintf(label,sizeof(label),"MouseFlight 0.2.29 POST-CAMERA %s | aim %.1f / %.1f | camera %.1f / %.1f",
            active.load() && enabled.load()?"ON":"STANDBY",
            target_pitch.load(),target_yaw.load(),camera_pitch.load(),camera_yaw.load());
        TextOutA(dc,28,40,label,static_cast<int>(strlen(label)));
        if (active.load() && enabled.load()) {
            const int width=rect.right-rect.left, height=rect.bottom-rect.top;
            const auto view=flight::basis(camera_pitch.load(),camera_yaw.load(),camera_roll.load());
            const auto aim=flight::basis(target_pitch.load(),target_yaw.load(),0).f;
            auto project=[&](flight::V v,int& sx,int& sy) {
                // MouseFlight HUD projects points 500m ahead of the aircraft.
                v=v*50000.0f-flight::V{view_offset_x.load(),view_offset_y.load(),view_offset_z.load()};
                float depth=flight::dot(v,view.f);
                if(depth<=0.01f) return false;
                float focal=width*0.5f/std::tan(view_fov.load()*0.5f*flight::rad);
                sx=static_cast<int>(width*0.5f+focal*flight::dot(v,view.r)/depth);
                sy=static_cast<int>(height*0.5f-focal*flight::dot(v,view.u)/depth);
                return sx>=0 && sy>=0 && sx<width && sy<height;
            };
            int x{},y{};
            const bool aim_visible=project(aim,x,y);
            static ULONGLONG draw_report=0;
            if(GetTickCount64()-draw_report>5000) {
                draw_report=GetTickCount64();
                log_line("overlay paint %dx%d target=%s at=(%d,%d) visible=%d",
                    width,height,aim_visible?"inside":"outside",x,y,IsWindowVisible(window));
            }
            // MouseFlight Demo sprites: white ring/bars with dark outlines.
            // Near-black must differ from the overlay's pure-black transparency key.
            const float scale=std::max(0.75f,height/1080.0f);
            auto px=[&](float n) { return std::max(1,static_cast<int>(std::lround(n*scale))); };
            HPEN outline=CreatePen(PS_SOLID,px(4),RGB(12,12,12));
            HPEN white=CreatePen(PS_SOLID,px(2),RGB(255,255,255));
            HGDIOBJ old_pen = SelectObject(dc, outline);
            HGDIOBJ old_brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
            if(aim_visible) {
                const int radius=px(25); // 50px diameter at 1080p, 100px at 4K.
                const int bound=radius+px(4);
                alpha_rects[alpha_count++]={x-bound,y-bound,x+bound+1,y+bound+1};
                Ellipse(dc,x-radius,y-radius,x+radius,y+radius);
                SelectObject(dc,white);
                Ellipse(dc,x-radius,y-radius,x+radius,y+radius);
            } else {
                const char* offscreen="TARGET OUTSIDE VIEW";
                TextOutA(dc,28,62,offscreen,static_cast<int>(strlen(offscreen)));
            }
            int bx{},by{};
            if(project(flight::basis(pose_pitch.load(),pose_yaw.load(),pose_roll.load()).f,bx,by)) {
                const int bound=px(16);
                alpha_rects[alpha_count++]={bx-bound,by-bound,bx+bound+1,by+bound+1};
                auto bars=[&]() {
                    MoveToEx(dc,bx-px(12),by,nullptr); LineTo(dc,bx-px(5),by);
                    MoveToEx(dc,bx+px(5),by,nullptr); LineTo(dc,bx+px(12),by);
                    MoveToEx(dc,bx,by-px(12),nullptr); LineTo(dc,bx,by-px(5));
                    MoveToEx(dc,bx,by+px(5),nullptr); LineTo(dc,bx,by+px(12));
                };
                SelectObject(dc,outline); bars();
                SelectObject(dc,white); bars();
            }
            SelectObject(dc, old_brush);
            SelectObject(dc, old_pen);
            DeleteObject(outline);
            DeleteObject(white);
        }
        GdiFlush();
        // GDI produces RGB with zero alpha. Set alpha only in the small HUD
        // regions, not by scanning the entire 4K surface every frame.
        for(unsigned n=0;n<alpha_count;++n) {
            const RECT& a=alpha_rects[n];
            for(int y=std::max(0L,a.top);y<std::min(rect.bottom,a.bottom);++y)
                for(int x=std::max(0L,a.left);x<std::min(rect.right,a.right);++x) {
                    auto& pixel=pixels[static_cast<size_t>(y)*rect.right+x];
                    if(pixel&0x00FFFFFF) pixel|=0xFF000000;
                }
        }
}

LRESULT CALLBACK overlay_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_ERASEBKGND) return 1;
    return DefWindowProcW(window, message, wparam, lparam);
}

void overlay_loop() {
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = overlay_proc;
    window_class.hInstance = self_module;
    window_class.lpszClassName = L"AC8MouseAimOverlay";
    window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    RegisterClassW(&window_class);
    overlay_window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        window_class.lpszClassName, L"AC8 Mouse Aim", WS_POPUP, 0, 0, 100, 100,
        nullptr, nullptr, self_module, nullptr);
    if (!overlay_window) { log_line("overlay creation failed: %lu", GetLastError()); return; }
    // Submit a complete backing surface explicitly instead of relying on WM_PAINT
    // redirection. Do not mix SetLayeredWindowAttributes with UpdateLayeredWindow.
    HDC screen=GetDC(nullptr);
    HDC surface=CreateCompatibleDC(screen);
    HBITMAP bitmap=nullptr;
    uint32_t* pixels=nullptr;
    HGDIOBJ original_bitmap=nullptr;
    SIZE surface_size{};
    HWND owner_window=nullptr;
    ULONGLONG present_report=0;
    bool window_reported = false;
    ULONGLONG window_check=0;
    MSG message{};
    while (running.load()) {
        if (!game_window || !IsWindow(game_window) || GetTickCount64()-window_check>1000) {
            HWND found=locate_game_window();
            if(found) game_window=found;
            window_check=GetTickCount64();
        }
        if (hud_enabled.load() && !game_paused.load() && !gaze_active.load() && game_window && !IsIconic(game_window)) {
            RECT client{};
            GetClientRect(game_window, &client);
            if (!window_reported) {
                wchar_t title[128]{};
                GetWindowTextW(game_window, title, 128);
                log_line("overlay attached to game window %dx%d", client.right - client.left,
                         client.bottom - client.top);
                window_reported = true;
            }
            POINT origin{client.left, client.top};
            ClientToScreen(game_window, &origin);
            if (owner_window!=game_window) {
                SetWindowLongPtrW(overlay_window,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(game_window));
                owner_window=game_window;
            }
            SIZE size{client.right-client.left,client.bottom-client.top};
            if (size.cx>0 && size.cy>0 && surface &&
                (!bitmap || size.cx!=surface_size.cx || size.cy!=surface_size.cy)) {
                if (bitmap) { SelectObject(surface,original_bitmap); DeleteObject(bitmap); }
                BITMAPINFO info{};
                info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
                info.bmiHeader.biWidth=size.cx; info.bmiHeader.biHeight=-size.cy;
                info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
                info.bmiHeader.biCompression=BI_RGB;
                bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);
                if (bitmap) { original_bitmap=SelectObject(surface,bitmap); surface_size=size; }
            }
            if (bitmap && size.cx>0 && size.cy>0) {
                GdiFlush();
                memset(pixels,0,static_cast<size_t>(size.cx)*size.cy*4);
                draw_overlay(overlay_window,surface,client,pixels);
                POINT source{};
                BLENDFUNCTION blend{AC_SRC_OVER,0,115,AC_SRC_ALPHA};
                BOOL presented=UpdateLayeredWindow(overlay_window,screen,&origin,&size,surface,
                    &source,0,&blend,ULW_ALPHA);
                DWORD error=presented?0:GetLastError();
                if (GetTickCount64()-present_report>5000) {
                    present_report=GetTickCount64();
                    log_line("overlay surface submit=%d error=%lu size=%ldx%ld",presented,error,size.cx,size.cy);
                }
            }
            SetWindowPos(overlay_window, HWND_TOPMOST, origin.x, origin.y,
                client.right - client.left, client.bottom - client.top,
                SWP_NOACTIVATE | SWP_SHOWWINDOW);
        } else {
            ShowWindow(overlay_window,SW_HIDE);
        }
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(33); // HUD only; game, camera and input update rates are unchanged.
    }
    if (bitmap) { SelectObject(surface,original_bitmap); DeleteObject(bitmap); }
    if (surface) DeleteDC(surface);
    if (screen) ReleaseDC(nullptr,screen);
    if (overlay_window) DestroyWindow(overlay_window);
    UnregisterClassW(window_class.lpszClassName, self_module);
}

unsigned char* find_hook() {
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    auto* sections = IMAGE_FIRST_SECTION(nt);
    unsigned char* found{};
    for (unsigned index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        auto& section = sections[index];
        if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        auto* begin = base + section.VirtualAddress;
        const size_t size = section.Misc.VirtualSize;
        for (size_t offset = 0; offset + sizeof(yawCode) <= size; ++offset) {
            if (begin[offset] == yawCode[0] && matchesYawCode(begin + offset, size - offset)) {
                if (found) return nullptr;
                found = begin + offset;
            }
        }
    }
    return found;
}

bool create_absolute_hook(void* target, void* detour, void** trampoline_out) {
    // The matched AC8 function starts with 16 bytes of complete instructions and
    // none of them use RIP-relative addressing. Copying those instructions lets
    // us place the trampoline anywhere in the 64-bit address space instead of
    // relying on MinHook finding a free executable page within +/-2 GB.
    constexpr size_t copied_size = 16;
    constexpr size_t jump_size = 14;
    auto* target_bytes = static_cast<unsigned char*>(target);
    if (memcmp(target_bytes, yawCode, copied_size) != 0) return false;

    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, copied_size + jump_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!trampoline) return false;

    auto write_absolute_jump = [](unsigned char* destination, const void* address) {
        destination[0] = 0xFF;
        destination[1] = 0x25;
        *reinterpret_cast<uint32_t*>(destination + 2) = 0;
        *reinterpret_cast<uintptr_t*>(destination + 6) = reinterpret_cast<uintptr_t>(address);
    };

    memcpy(trampoline, target_bytes, copied_size);
    write_absolute_jump(trampoline + copied_size, target_bytes + copied_size);
    DWORD previous_trampoline_protection{};
    if (!VirtualProtect(trampoline, copied_size + jump_size, PAGE_EXECUTE_READ,
                        &previous_trampoline_protection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), trampoline, copied_size + jump_size);

    DWORD previous_target_protection{};
    if (!VirtualProtect(target_bytes, copied_size, PAGE_EXECUTE_READWRITE,
                        &previous_target_protection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    write_absolute_jump(target_bytes, detour);
    memset(target_bytes + jump_size, 0x90, copied_size - jump_size);
    FlushInstructionCache(GetCurrentProcess(), target_bytes, copied_size);
    DWORD ignored{};
    VirtualProtect(target_bytes, copied_size, previous_target_protection, &ignored);

    *trampoline_out = trampoline;
    return true;
}

uintptr_t __fastcall process_input(unsigned char* state, unsigned char* context) {
    ++calls;
    // Manual pitch also suspends automatic roll, matching MouseFlight maneuvers.
    // Preserve AC's native keyboard values, including opposing-key handling.
    auto held=[](int key) { return (GetAsyncKeyState(key)&0x8000)!=0; };
    const bool keyboard_pitch=held('W') || held('S');
    const bool keyboard_roll=keyboard_pitch || held('A') || held('D');
    const bool keyboard_yaw=held('Q') || held('E');
    const uintptr_t pawn = aircraft.load();
    const bool valid_pose = active.load() && pawn && GetTickCount64() - pose_tick.load() <= 1000;
    bool override_input = valid_pose && enabled.load() && !game_paused.load() && !gaze_active.load() && foreground_is_game();
    if (override_input && reinterpret_cast<uintptr_t>(state) == pawn + 0x22a0) {
        __try {
            if (*reinterpret_cast<uintptr_t*>(context) != pawn + 0x2268 ||
                *reinterpret_cast<uintptr_t*>(context + 8) != pawn + 0x2c28) {
                override_input = false;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            override_input = false;
        }
    }
    if (override_input && reinterpret_cast<uintptr_t>(state) == pawn + 0x22a0) {
        __try {
            float* axes = reinterpret_cast<float*>(pawn + 0x2268);
            if(!keyboard_pitch) axes[config.pitch_slot] = command_pitch.load();
            if(!keyboard_yaw) axes[1] = command_yaw.load();
            if(!keyboard_roll) axes[config.roll_slot] = command_roll.load();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            active.store(false);
            override_input = false;
        }
    }
    uintptr_t result = original(state, context);
    if (override_input && reinterpret_cast<uintptr_t>(state) == pawn + 0x22a0) {
        __try {
            // AC8's stock yaw path turns every non-zero axis value into full yaw.
            // Preserve only the proportional input target; downstream response remains stock.
            if(!keyboard_yaw)
                *reinterpret_cast<double*>(state + 8) = static_cast<double>(command_yaw.load());
            ++writes;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            active.store(false);
        }
    }
    return result;
}

bool prepare_hook() {
    if (hook_created) return true;
    hook_address = find_hook();
    if (!hook_address) {
        log_line("error input processor signature missing or ambiguous");
        return false;
    }
    MH_STATUS result = MH_Initialize();
    if (result != MH_OK && result != MH_ERROR_ALREADY_INITIALIZED) return false;
    result = MH_CreateHook(hook_address, reinterpret_cast<void*>(&process_input), reinterpret_cast<void**>(&original));
    if (result == MH_ERROR_MEMORY_ALLOC) {
        if (create_absolute_hook(hook_address, reinterpret_cast<void*>(&process_input),
                                 reinterpret_cast<void**>(&original))) {
            hook_created = true;
            log_line("input hook installed with absolute trampoline fallback");
            return true;
        }
        log_line("error absolute hook fallback: win32=%lu", GetLastError());
    }
    if (result != MH_OK) {
        log_line("error create hook: %s", MH_StatusToString(result));
        return false;
    }
    result = MH_EnableHook(hook_address);
    if (result != MH_OK) {
        log_line("error enable hook: %s", MH_StatusToString(result));
        return false;
    }
    hook_created = true;
    return true;
}

bool offline_authorized() {
    wchar_t value[16]{};
    return GetEnvironmentVariableW(L"EOS_USE_ANTICHEATCLIENTNULL", value, 16) > 0 && wcscmp(value, L"1") == 0;
}
} // namespace

extern "C" __declspec(dllexport) int ac8_mouseaim_start(void*) {
    if (running.load()) return 0;
    init_paths();
    if (!offline_authorized()) {
        log_line("inactive: offline launch marker missing; multiplayer-safe refusal");
        return 0;
    }
    load_config();
    if (!prepare_hook()) return 0;
    install_native_camera();
    if (prepare_raw_input_capture()) {
        log_line("mouse capture attached to AC8 raw input");
    } else {
        log_line("warning raw input hook unavailable; using DirectInput fallback");
    }
    running.store(true);
    std::thread(mouse_loop).detach();
    std::thread(pipe_loop).detach();
    std::thread(overlay_loop).detach();
    log_line("ready: F8 toggle, F9 recenter; RMB reserved for game actions");
    return 0;
}

extern "C" __declspec(dllexport) int ac8_mouseaim_reload(void*) {
    load_config();
    recenter_requested.store(true);
    log_line("configuration reloaded");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        self_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
