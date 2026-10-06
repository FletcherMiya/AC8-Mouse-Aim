#pragma once
#include "flight_math.h"

namespace flight {
// Compare full orientations, not Euler components: +/-180 wraps and vertical
// flight are continuous. Leave ample room above the observed aircraft rates.
inline bool pose_discontinuity(const Basis& before,const Basis& after,float seconds) {
    const float cosine=(dot(before.f,after.f)+dot(before.r,after.r)+dot(before.u,after.u)-1)*.5f;
    const float angle=std::acos(std::clamp(cosine,-1.0f,1.0f))/rad;
    return angle>std::max(30.0f,360*seconds+10);
}
}
