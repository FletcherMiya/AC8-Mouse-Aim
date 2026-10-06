#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

// Pure selection policy. Coordinates are doubles, matching UE's world positions.
// The aim is the flight target, never the camera or current aircraft attitude.
namespace mouse_target {
struct Vec { double x=0,y=0,z=0; };
inline bool finite(Vec v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
struct Candidate { uintptr_t id=0; Vec position; bool recent=false; };
struct Choice { uintptr_t id=0; double cosine=-1,distance2=0; bool recent=false; };
inline Choice choose(const Candidate* candidates,size_t count,uintptr_t current,Vec origin,Vec aim) {
    Choice fresh,visited;
    if(!finite(origin)||!finite(aim)) return {};
    const double norm=std::sqrt(aim.x*aim.x+aim.y*aim.y+aim.z*aim.z);
    if(!std::isfinite(norm)||norm<1e-6) return {};
    aim={aim.x/norm,aim.y/norm,aim.z/norm};
    for(size_t i=0;i<count;++i) {
        const auto& c=candidates[i];
        if(!c.id||c.id==current||!finite(c.position)) continue;
        const Vec d{c.position.x-origin.x,c.position.y-origin.y,c.position.z-origin.z};
        const double distance2=d.x*d.x+d.y*d.y+d.z*d.z;
        if(!std::isfinite(distance2)||distance2<1e-6) continue;
        const double cosine=(d.x*aim.x+d.y*aim.y+d.z*aim.z)/std::sqrt(distance2);
        // No enemy in the flight target's forward hemisphere: keep stock choice.
        if(!std::isfinite(cosine)||cosine<=0) continue;
        auto& best=c.recent?visited:fresh;
        if(!best.id||cosine>best.cosine+1e-8||
           (std::abs(cosine-best.cosine)<=1e-8&&distance2<best.distance2))
            best={c.id,cosine,distance2,c.recent};
    }
    // Keep target cycling: omit current/recent targets while fresh ones exist.
    // The game's own SelectTarget still owns and updates the history.
    return fresh.id?fresh:visited;
}
}
