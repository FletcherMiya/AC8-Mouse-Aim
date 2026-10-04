#pragma once
#include <array>
#include <string>
#include <string_view>
#include <cwctype>

namespace input {
enum Binding : size_t { Pitch1, Pitch2, Roll1, Roll2, Yaw1, Yaw2, FreeLook,
                       ToggleHud, ToggleEnabled, Recenter, Reload, GazeProbe, Perf, HighG1, HighG2, Count };
struct Bindings {
    std::array<int,Count> keys{'W','S','A','D','Q','E','F',0x76,0x77,0x78,0x79,0x75,0x74,0x11,0x20};
    int operator[](Binding key) const { return keys[key]; }
    bool operator==(const Bindings&) const = default;
};
struct Field { const wchar_t* name; const wchar_t* fallback; Binding first; size_t count; };
inline constexpr Field fields[] = {
    {L"pitch_keys",L"W,S",Pitch1,2}, {L"roll_keys",L"A,D",Roll1,2},
    {L"yaw_keys",L"Q,E",Yaw1,2}, {L"free_look",L"F",FreeLook,1},
    {L"toggle_hud",L"F7",ToggleHud,1}, {L"toggle_enabled",L"F8",ToggleEnabled,1},
    {L"recenter",L"F9",Recenter,1}, {L"reload_config",L"F10",Reload,1},
    {L"gaze_probe",L"F6",GazeProbe,1}, {L"perf_report",L"F5",Perf,1},
    {L"high_g_keys",L"CTRL,SPACE",HighG1,2}
};
inline std::wstring normalized(std::wstring_view text) {
    const auto start = text.find_first_not_of(L" \t\r\n");
    if (start == text.npos) return {};
    std::wstring result(text.substr(start,text.find_last_not_of(L" \t\r\n")-start+1));
    for (auto& c: result) c = static_cast<wchar_t>(std::towupper(c));
    return result;
}
inline int key_code(std::wstring_view text) {
    const auto key = normalized(text);
    if (key.size()==1 && ((key[0]>=L'A' && key[0]<=L'Z') || (key[0]>=L'0' && key[0]<=L'9'))) return key[0];
    if (key.size()>=2 && key.size()<=3 && key[0]==L'F' && key[1]>=L'1' && key[1]<=L'9') {
        int number = key[1]-L'0';
        if (key.size()==3) {
            if (key[2]<L'0' || key[2]>L'9') return 0;
            number = number*10+key[2]-L'0';
        }
        if (number<=24) return 0x6f+number;
    }
    struct Named { const wchar_t* name; int code; };
    static constexpr Named names[] = {
        {L"BACKSPACE",0x08},{L"TAB",0x09},{L"ENTER",0x0d},{L"RETURN",0x0d},
        {L"SHIFT",0x10},{L"CTRL",0x11},{L"CONTROL",0x11},{L"ALT",0x12},
        {L"PAUSE",0x13},{L"CAPSLOCK",0x14},{L"ESC",0x1b},{L"ESCAPE",0x1b},
        {L"SPACE",0x20},{L"PAGEUP",0x21},{L"PAGEDOWN",0x22},{L"END",0x23},{L"HOME",0x24},
        {L"LEFT",0x25},{L"UP",0x26},{L"RIGHT",0x27},{L"DOWN",0x28},
        {L"INSERT",0x2d},{L"DELETE",0x2e},
        {L"LSHIFT",0xa0},{L"RSHIFT",0xa1},{L"LCTRL",0xa2},{L"RCTRL",0xa3},
        {L"LALT",0xa4},{L"RALT",0xa5}
    };
    for (const auto& entry: names) if (key==entry.name) return entry.code;
    if (key.size()==7 && key.substr(0,6)==L"NUMPAD" && key[6]>=L'0' && key[6]<=L'9') return 0x60+key[6]-L'0';
    return 0;
}
inline bool parse_field(Bindings& bindings, const Field& field, std::wstring_view text) {
    const auto comma = text.find(L',');
    if ((field.count==1 && comma!=text.npos) || (field.count==2 && comma==text.npos)) return false;
    const int first = key_code(text.substr(0,comma));
    const int second = field.count==2 ? key_code(text.substr(comma+1)) : 0;
    if (!first || (field.count==2 && !second)) return false;
    bindings.keys[field.first]=first;
    if (field.count==2) bindings.keys[field.first+1]=second;
    return true;
}
inline bool overlaps(int a, int b) {
    if (a==b) return true;
    if (a>b) { const int tmp=a; a=b; b=tmp; }
    return (a==0x10 && (b==0xa0 || b==0xa1)) ||
           (a==0x11 && (b==0xa2 || b==0xa3)) || (a==0x12 && (b==0xa4 || b==0xa5));
}
inline bool unique(const Bindings& bindings) {
    // High-G keys observe existing game actions; they do not consume keys or
    // register shortcuts. Preserve old bindings that share a modifier with them.
    if(overlaps(bindings[HighG1],bindings[HighG2])) return false;
    for (size_t i=0;i<HighG1;++i) for (size_t j=i+1;j<HighG1;++j)
        if (overlaps(bindings.keys[i],bindings.keys[j])) return false;
    return true;
}
struct ManualAxes { bool pitch, roll, yaw; };
template<class Held> bool high_g_requested(const Bindings& keys,Held held) {
    return held(keys[HighG1]) && held(keys[HighG2]);
}
template<class Held> ManualAxes manual_axes(const Bindings& keys, Held held) {
    const bool pitch = held(keys[Pitch1]) || held(keys[Pitch2]);
    return {pitch, pitch || held(keys[Roll1]) || held(keys[Roll2]),
            held(keys[Yaw1]) || held(keys[Yaw2])};
}
}
