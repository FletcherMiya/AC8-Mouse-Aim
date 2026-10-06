#pragma once
#include <windows.h>
#include <intrin.h>
#include <cstring>
#include <atomic>
#include "target_priority.h"
#include "vendor/minhook/include/MinHook.h"

namespace mouse_target {
// Only this known player's short release path is eligible, for any weapon.
struct Context { bool enabled=false,usable=false,diagnostics=false; uintptr_t pawn=0; Vec aim; };
using Provider=Context(*)();
using Logger=void(*)(const char*,...);
using Select=uintptr_t(__fastcall*)(uintptr_t,bool,uintptr_t,bool);
using Rejected=bool(__fastcall*)(uintptr_t,uintptr_t);
using WeaponAllows=bool(__fastcall*)(uintptr_t,uintptr_t);
inline Provider context_provider=nullptr;
inline Logger logger=nullptr;
inline Select original_select=nullptr;
inline Rejected rejected=nullptr;
inline WeaponAllows weapon_allows=nullptr;
inline uintptr_t image_base=0;
inline std::atomic<bool> faulted{false};
inline constexpr uintptr_t select_rva=0x7296F40,release_return_rva=0x7D630E0;
inline constexpr unsigned char select_prefix[]={
    0x48,0x89,0x5c,0x24,0x18,0x55,0x57,0x41,0x57,0x48,0x83,0xec,0x20,
    0xf3,0x0f,0x10,0x81,0xfc,0x06,0x00,0x00};
inline constexpr unsigned char release_call[]={
    0x48,0x8b,0x8b,0xe8,0x1a,0,0,0x45,0x33,0xc9,0x45,0x33,0xc0,0x33,0xd2,
    0xe8,0x60,0x3e,0x53,0xff};
inline constexpr unsigned char rejected_prefix[]={
    0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xda,0x48,0x8b,0xf9};
inline constexpr uintptr_t weapon_allows_rva=0x729A6F0;
inline constexpr unsigned char weapon_allows_prefix[]={
    0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xda,0x48,0x8b,0xf9,
    0x48,0x85,0xd2,0x74,0x3a,0x8b,0x42,0x08,0xc1,0xe8,0x1e,0xf6,0xd0,0xa8,0x01,0x74};
struct Array { uintptr_t data; int count,capacity; };
static_assert(sizeof(Array)==16);
inline bool pointer(uintptr_t p) { return p>=0x10000&&p<=0x00007fffffff0000ull&&(p&7)==0; }
template<class T> inline T field(uintptr_t p,size_t offset) { return *reinterpret_cast<const T*>(p+offset); }
inline bool actor_position(uintptr_t actor,Vec& out) {
    const uintptr_t root=field<uintptr_t>(actor,0x1A0);
    if(!pointer(root)) return false;
    out=field<Vec>(root,0x220);
    return finite(out)&&std::abs(out.x)<1e12&&std::abs(out.y)<1e12&&std::abs(out.z)<1e12;
}
inline bool target_position(uintptr_t target,Vec& out) {
    // Native scoring at RVA 0x7298BE7 obtains its point through this same
    // LiveGameObject virtual getter (Vec3<double> returned via caller storage).
    // Ships/subtargets can have aim points different from the Actor origin.
    const auto table=field<uintptr_t>(target,0);
    if(!pointer(table)) return false;
    const auto function=field<uintptr_t>(table,0xA50);
    if(function<image_base||function>=image_base+0x2195A000) return false;
    using GetPoint=const Vec*(__fastcall*)(uintptr_t,Vec*);
    Vec storage;
    const auto* point=reinterpret_cast<GetPoint>(function)(target,&storage);
    if(!point) return false;
    out=*point;
    return finite(out)&&std::abs(out.x)<1e12&&std::abs(out.y)<1e12&&std::abs(out.z)<1e12;
}
struct Evaluation { Choice choice; uintptr_t before=0; int pool=0,eligible=0; const char* reason="stock"; };

// Called only at the exact native short-release call site, on the flight bridge
// thread. Reads are bounded; it never writes UE properties or candidate arrays.
inline Evaluation evaluate(uintptr_t selection,const Context& context) {
    Evaluation result;
    __try {
        if(!pointer(selection)||!pointer(context.pawn)||
           field<uintptr_t>(selection,0x840)!=context.pawn||
           field<uintptr_t>(context.pawn,0x1AE8)!=selection) { result.reason="owner"; return result; }
        result.before=field<uintptr_t>(selection,0x750);
        // Native SelectTarget performs this same comparison before consuming an
        // override. Do not turn a long hold into an extra target switch.
        const float limit=field<float>(selection,0x6FC),held=field<float>(selection,0x82C);
        if(!std::isfinite(limit)||!std::isfinite(held)||held<0||!(limit>held)) {
            result.reason="hold_or_timer"; return result;
        }
        const auto pool=field<Array>(selection,0x870); // reflected TargetCandidates
        const auto history=field<Array>(selection,0x9A0); // reflected TargetHistory
        if(pool.count<0||pool.count>1024||pool.capacity<pool.count||
           history.count<0||history.count>32||history.capacity<history.count||
           (pool.count&&!pointer(pool.data))||(history.count&&!pointer(history.data))) {
            result.reason="array_bounds"; return result;
        }
        result.pool=pool.count;
        Vec origin;
        if(!actor_position(context.pawn,origin)) { result.reason="position"; return result; }
        Candidate candidates[1024]{};
        for(int i=0;i<pool.count;++i) {
            const auto id=field<uintptr_t>(pool.data,static_cast<size_t>(i)*8);
            if(!pointer(id)||id==result.before) continue;
            // The original candidate set provides mission/weapon eligibility;
            // the same native predicate used by next-target processing rechecks
            // alive/visible/targetable state at the actual release instant.
            if(rejected(selection,id)) continue;
            // The exact type/validity predicate SelectTarget applies to an
            // override, using the CURRENT weapon's native allowed categories.
            // Filter before ranking so an incompatible target cannot hide a
            // valid runner-up (including immediately after switching weapons).
            if(!weapon_allows(selection,id)) continue;
            Candidate candidate; candidate.id=id;
            if(!target_position(id,candidate.position)) continue;
            for(int j=0;j<history.count;++j)
                if(id==field<uintptr_t>(history.data,static_cast<size_t>(j)*8)) candidate.recent=true;
            candidates[result.eligible++]=candidate;
        }
        result.choice=choose(candidates,static_cast<size_t>(result.eligible),result.before,origin,context.aim);
        result.reason=result.choice.id?"mouse_aim":"no_candidate";
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        faulted.store(true);
        result.choice={}; result.reason="memory_fault_disabled";
    }
    return result;
}
inline uintptr_t dispatch(uintptr_t caller,uintptr_t selection,bool forced,uintptr_t override_target,bool broadcast) {
    // All automatic acquisition, mission overrides, other players and other
    // call sites go through the untouched original function exactly once.
    if(caller!=image_base+release_return_rva||forced||override_target||faulted.load())
        return original_select(selection,forced,override_target,broadcast);
    const auto context=context_provider();
    if(!context.enabled||!context.usable) return original_select(selection,forced,override_target,broadcast);
    const auto evaluation=evaluate(selection,context);
    const auto chosen=evaluation.choice.id?evaluation.choice.id:override_target;
    const auto result=original_select(selection,forced,chosen,broadcast);
    if(context.diagnostics||faulted.load()) logger(
        "TARGET_SELECT reason=%s owner=%llX before=%llX chosen=%llX result=%llX pool=%d eligible=%d aim=(%.6f,%.6f,%.6f) cosine=%.6f recent=%d",
        evaluation.reason,static_cast<unsigned long long>(context.pawn),static_cast<unsigned long long>(evaluation.before),
        static_cast<unsigned long long>(chosen),static_cast<unsigned long long>(result),evaluation.pool,evaluation.eligible,
        context.aim.x,context.aim.y,context.aim.z,evaluation.choice.cosine,evaluation.choice.recent);
    return result;
}
inline uintptr_t __fastcall hook(uintptr_t selection,bool forced,uintptr_t override_target,bool broadcast) {
    return dispatch(reinterpret_cast<uintptr_t>(_ReturnAddress()),selection,forced,override_target,broadcast);
}
inline void absolute_jump(unsigned char* destination,const void* address) {
    destination[0]=0xff; destination[1]=0x25;
    const uint32_t zero=0; std::memcpy(destination+2,&zero,4);
    std::memcpy(destination+6,&address,8);
}
inline bool absolute_fallback(unsigned char* entry) {
    // All 21 copied bytes above are complete instructions, without RIP-relative
    // operands/branches. This fallback is specific to the verified SelectTarget.
    if(std::memcmp(entry,select_prefix,sizeof(select_prefix))) return false;
    auto* trampoline=static_cast<unsigned char*>(VirtualAlloc(nullptr,sizeof(select_prefix)+14,
        MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!trampoline) return false;
    std::memcpy(trampoline,entry,sizeof(select_prefix));
    absolute_jump(trampoline+sizeof(select_prefix),entry+sizeof(select_prefix));
    DWORD protection=0;
    if(!VirtualProtect(trampoline,sizeof(select_prefix)+14,PAGE_EXECUTE_READ,&protection)) {
        VirtualFree(trampoline,0,MEM_RELEASE); return false;
    }
    FlushInstructionCache(GetCurrentProcess(),trampoline,sizeof(select_prefix)+14);
    if(!VirtualProtect(entry,sizeof(select_prefix),PAGE_EXECUTE_READWRITE,&protection)) {
        VirtualFree(trampoline,0,MEM_RELEASE); return false;
    }
    original_select=reinterpret_cast<Select>(trampoline);
    absolute_jump(entry,reinterpret_cast<void*>(&hook));
    std::memset(entry+14,0x90,sizeof(select_prefix)-14);
    FlushInstructionCache(GetCurrentProcess(),entry,sizeof(select_prefix));
    DWORD ignored=0; VirtualProtect(entry,sizeof(select_prefix),protection,&ignored);
    return true;
}
inline bool signatures_match(const unsigned char* base) {
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<0||dos->e_lfanew>0x10000) return false;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.TimeDateStamp!=0x6AA0E27D||
       nt->OptionalHeader.SizeOfImage!=0x2195A000) return false;
    return !std::memcmp(base+select_rva,select_prefix,sizeof(select_prefix))&&
        !std::memcmp(base+release_return_rva-sizeof(release_call),release_call,sizeof(release_call))&&
        !std::memcmp(base+0x729B070,rejected_prefix,sizeof(rejected_prefix))&&
        !std::memcmp(base+weapon_allows_rva,weapon_allows_prefix,sizeof(weapon_allows_prefix))&&
        // UHT metadata independently fixes the reflected layout used above.
        field<uint32_t>(reinterpret_cast<uintptr_t>(base),0xCB5D5C8)==0x08700001&&
        field<uint32_t>(reinterpret_cast<uintptr_t>(base),0xCB5D550)==0x8400001;
}
inline bool install(Provider provider,Logger log) {
    if(original_select) return true;
    context_provider=provider; logger=log;
    auto* base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if(!base||!signatures_match(base)) { logger("TARGET_SELECT hook refused: executable/layout mismatch"); return false; }
    image_base=reinterpret_cast<uintptr_t>(base);
    rejected=reinterpret_cast<Rejected>(base+0x729B070);
    weapon_allows=reinterpret_cast<WeaponAllows>(base+weapon_allows_rva);
    auto status=MH_CreateHook(base+select_rva,reinterpret_cast<void*>(&hook),reinterpret_cast<void**>(&original_select));
    if(status==MH_ERROR_MEMORY_ALLOC&&absolute_fallback(base+select_rva)) {
        logger("TARGET_SELECT native hook installed (absolute fallback); opt-in mouse_target_priority"); return true;
    }
    if(status!=MH_OK) {
        original_select=nullptr;
        logger("TARGET_SELECT hook creation refused: %s",MH_StatusToString(status)); return false;
    }
    status=MH_EnableHook(base+select_rva);
    if(status!=MH_OK) {
        MH_RemoveHook(base+select_rva); original_select=nullptr;
        logger("TARGET_SELECT hook enable refused: %s",MH_StatusToString(status)); return false;
    }
    logger("TARGET_SELECT native hook installed; opt-in mouse_target_priority; all weapons, short release only");
    return true;
}
}
