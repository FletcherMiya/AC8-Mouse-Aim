#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <array>
#include <limits>
#include "native_target_selection.h"
using namespace mouse_target;

namespace {
Context context;
int calls=0,rejection_calls=0,weapon_checks=0;
uintptr_t last_override=0,bad_candidate=0,incompatible_candidate=0,checked_component=0;
bool deny_all=false;
bool last_forced=false,last_broadcast=false;
Context provide() { return context; }
void log_noop(const char*,...) {}
uintptr_t __fastcall stock(uintptr_t,bool forced,uintptr_t target,bool broadcast) {
    ++calls; last_override=target; last_forced=forced; last_broadcast=broadcast;
    return target?target:0x7770;
}
bool __fastcall invalid(uintptr_t,uintptr_t target) { ++rejection_calls; return target==bad_candidate; }
bool __fastcall allowed(uintptr_t selection,uintptr_t target) {
    ++weapon_checks; checked_component=selection;
    return !deny_all&&target!=incompatible_candidate;
}
template<class T> void set(uintptr_t base,size_t offset,T value) { std::memcpy(reinterpret_cast<void*>(base+offset),&value,sizeof(value)); }
const Vec* __fastcall game_point(uintptr_t actor,Vec* output) {
    actor_position(actor,*output); output->z+=10; return output;
}
struct Actor {
    alignas(8) std::array<unsigned char,0x1B00> object{};
    alignas(8) std::array<unsigned char,0x240> root{};
    alignas(8) std::array<unsigned char,0xA58> table{};
    uintptr_t id() { return reinterpret_cast<uintptr_t>(object.data()); }
    void at(Vec position) {
        set(id(),0,reinterpret_cast<uintptr_t>(table.data()));
        set(reinterpret_cast<uintptr_t>(table.data()),0xA50,reinterpret_cast<uintptr_t>(&game_point));
        set(id(),0x1A0,reinterpret_cast<uintptr_t>(root.data()));
        set(reinterpret_cast<uintptr_t>(root.data()),0x220,position);
    }
};
struct Fixture {
    Actor pawn,front,mouse,third;
    alignas(8) std::array<unsigned char,0xD40> selection{};
    std::array<uintptr_t,3> pool{},history{};
    uintptr_t component() { return reinterpret_cast<uintptr_t>(selection.data()); }
    Fixture() {
        pawn.at({1e6,2e6,300}); front.at({1e6+1000,2e6,300});
        mouse.at({1e6,2e6+9000,300}); third.at({1e6+200,2e6+1000,300});
        pool={front.id(),mouse.id(),third.id()};
        set(component(),0x840,pawn.id()); set(pawn.id(),0x1AE8,component());
        set(component(),0x6FC,0.3f); set(component(),0x82C,0.05f);
        set(component(),0x870,Array{reinterpret_cast<uintptr_t>(pool.data()),3,3});
        set(component(),0x9A0,Array{reinterpret_cast<uintptr_t>(history.data()),0,3});
        context={true,true,true,pawn.id(),{0,1,0}};
        context_provider=provide; logger=log_noop; original_select=stock; rejected=invalid; weapon_allows=allowed;
        image_base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)); faulted=false; bad_candidate=0; calls=0; rejection_calls=0;
        incompatible_candidate=0; deny_all=false; weapon_checks=0; checked_component=0;
    }
    uintptr_t run(uintptr_t caller=0,bool forced=false,uintptr_t target=0,bool broadcast=false) {
        return dispatch(caller?caller:image_base+release_return_rva,component(),forced,target,broadcast);
    }
};
void ranking() {
    Candidate a[]={{1,{100,0,0},false},{2,{0,10000,0},false},{3,{0,900,0},false}};
    assert(choose(a,3,0,{},{0,1,0}).id==3); // Angle before distance; distance only breaks a tie.
    assert(choose(a,3,0,{},{1,0,0}).id==1);
    assert(choose(a,3,3,{},{0,1,0}).id==2); // Don't reselect current.
    a[2].recent=true;
    assert(choose(a,3,0,{},{0,1,0}).id==2);
    a[1].recent=true;
    assert(choose(a,3,0,{},{0,1,0}).id==3); // Recent fallback when no fresh forward candidate.
    assert(choose(a,3,0,{},{0,-1,0}).id==0);
    assert(choose(a,3,0,{},{0,0,0}).id==0);
    assert(choose(a,3,0,{},{std::numeric_limits<double>::quiet_NaN(),0,0}).id==0);
    Candidate b[]={{5,{1e8,1e8,1000},false},{6,{1e8,1e8,2000},false}};
    assert(choose(b,2,0,{1e8,1e8,0},{0,0,1}).id==5);
    b[0].position.x=std::numeric_limits<double>::infinity();
    assert(choose(b,2,0,{1e8,1e8,0},{0,0,1}).id==6);
    assert(choose(nullptr,0,0,{},{1,0,0}).id==0);
    puts("PASS target ranking: flight aim, angle/distance, cycle/history, poles and invalid geometry");
}
void runtime() {
    { Fixture f; Vec point; assert(target_position(f.mouse.id(),point)); assert(point.z==310); }
    { Fixture f; assert(f.run()==f.mouse.id()); assert(calls==1&&last_override==f.mouse.id()); }
    { Fixture f; context.enabled=false; assert(f.run()==0x7770); assert(calls==1&&rejection_calls==0); }
    { Fixture f; context.usable=false; assert(f.run()==0x7770); assert(calls==1&&rejection_calls==0); }
    { Fixture f; assert(f.run(image_base+0x728F364)==0x7770); assert(calls==1&&rejection_calls==0); }
    { Fixture f; assert(f.run(0,true,0,true)==0x7770); assert(calls==1&&last_forced&&last_broadcast); }
    { Fixture f; assert(f.run(0,false,f.front.id(),true)==f.front.id()); assert(last_broadcast&&calls==1); }
    { Fixture f; context.pawn=f.front.id(); assert(f.run()==0x7770); assert(calls==1&&rejection_calls==0); }
    { Fixture f; set(f.component(),0x82C,0.3f); assert(f.run()==0x7770); assert(rejection_calls==0); }
    { Fixture f; set(f.component(),0x9D9,static_cast<unsigned char>(1)); assert(f.run()==f.mouse.id()); }
    { Fixture f; bad_candidate=f.mouse.id(); assert(f.run()==f.third.id()); assert(calls==1); }
    { Fixture f; set(f.component(),0x750,f.mouse.id()); assert(f.run()==f.third.id()); }
    { Fixture f; f.history[0]=f.mouse.id(); set(f.component(),0x9A0,Array{reinterpret_cast<uintptr_t>(f.history.data()),1,3});
      assert(f.run()==f.third.id()); }
    { Fixture f; set(f.component(),0x870,Array{0,0,0}); assert(f.run()==0x7770); }
    { Fixture f; set(f.component(),0x870,Array{0x10000,1025,1025}); assert(f.run()==0x7770); assert(!faulted); }
    { Fixture f; context.aim={0,-1,0}; assert(f.run()==0x7770); }
    { Fixture f; set(f.component(),0x870,Array{0x10000,1,1}); assert(f.run()==0x7770); assert(faulted);
      assert(f.run()==0x7770); assert(calls==2); }
    puts("PASS actual target dispatch: original called once, release-only, owner/state/timer guards, both weapon slots and fault fallback");
}
void weapons() {
    for(const int special:{0,1}) {
        Fixture f; set(f.component(),0x9D9,static_cast<unsigned char>(special));
        // Opaque game-owned limits and seeker/multilock state must remain intact.
        set(f.component(),0x834,0.875f); set(f.component(),0x838,275000.0f);
        set(f.component(),0x9C4,static_cast<unsigned char>(1));
        set(f.component(),0x9E8,8); set(f.component(),0x9BC,0.65f);
        const auto component_before=f.selection; const auto pool_before=f.pool,history_before=f.history;
        incompatible_candidate=f.mouse.id();
        assert(f.run()==f.third.id()); // Closest but wrong category never masks next valid target.
        assert(weapon_checks==3&&checked_component==f.component());
        assert(calls==1&&!last_forced&&!last_broadcast);
        assert(f.selection==component_before&&f.pool==pool_before&&f.history==history_before);
        // Native weapon predicate changes immediately, even with an old candidate pool.
        incompatible_candidate=f.third.id();
        assert(f.run()==f.mouse.id()); assert(calls==2);
        deny_all=true; assert(f.run()==0x7770); assert(last_override==0&&calls==3);
        assert(f.selection==component_before&&f.pool==pool_before&&f.history==history_before);
    }
    puts("PASS all-weapon priority: current native eligibility, incompatible/empty fallback, limits and multi-seeker state untouched");
}
void signatures() {
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2195A000,MEM_RESERVE,PAGE_READWRITE));
    assert(memory);
    auto page=[&](size_t offset) { assert(VirtualAlloc(memory+(offset&~size_t(4095)),4096,MEM_COMMIT,PAGE_READWRITE)); };
    page(0); page(select_rva); page(release_return_rva); page(0x729B070); page(0xCB5D5C8); page(weapon_allows_rva);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(memory); dos->e_magic=IMAGE_DOS_SIGNATURE; dos->e_lfanew=0x100;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(memory+0x100);
    nt->Signature=IMAGE_NT_SIGNATURE; nt->FileHeader.TimeDateStamp=0x6AA0E27D; nt->OptionalHeader.SizeOfImage=0x2195A000;
    std::memcpy(memory+select_rva,select_prefix,sizeof(select_prefix));
    std::memcpy(memory+release_return_rva-sizeof(release_call),release_call,sizeof(release_call));
    std::memcpy(memory+0x729B070,rejected_prefix,sizeof(rejected_prefix));
    std::memcpy(memory+weapon_allows_rva,weapon_allows_prefix,sizeof(weapon_allows_prefix));
    set(reinterpret_cast<uintptr_t>(memory),0xCB5D5C8,uint32_t(0x08700001));
    set(reinterpret_cast<uintptr_t>(memory),0xCB5D550,uint32_t(0x08400001));
    assert(signatures_match(memory));
    memory[select_rva]^=1; assert(!signatures_match(memory)); memory[select_rva]^=1;
    memory[release_return_rva-1]^=1; assert(!signatures_match(memory)); memory[release_return_rva-1]^=1;
    memory[0xCB5D5C8]^=1; assert(!signatures_match(memory)); memory[0xCB5D5C8]^=1;
    memory[weapon_allows_rva+31]^=1; assert(!signatures_match(memory)); memory[weapon_allows_rva+31]^=1;
    ++nt->FileHeader.TimeDateStamp; assert(!signatures_match(memory));
    VirtualFree(memory,0,MEM_RELEASE);
    puts("PASS executable/layout identity: mismatched instructions and UHT offsets refused");
}
void trampoline() {
    Fixture f;
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)); assert(code);
    std::memcpy(code,select_prefix,sizeof(select_prefix));
    const unsigned char tail[]={0x48,0x83,0xc4,0x20,0x41,0x5f,0x5f,0x5d,0x4c,0x89,0xc0,0xc3};
    std::memcpy(code+sizeof(select_prefix),tail,sizeof(tail));
    DWORD old=0; assert(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old));
    assert(absolute_fallback(code));
    const auto call=reinterpret_cast<Select>(code);
    assert(call(f.component(),false,f.mouse.id(),false)==f.mouse.id());
    assert(call(f.component(),true,f.front.id(),true)==f.front.id());
    VirtualFree(reinterpret_cast<void*>(original_select),0,MEM_RELEASE); original_select=stock;
    VirtualFree(code,0,MEM_RELEASE);
    puts("PASS actual absolute trampoline executes preserved 21-byte prologue and returns original result");
}
}
int main() { SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX); ranking(); runtime(); weapons(); signatures(); trampoline(); }
