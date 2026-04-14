#pragma once
// effect_types.h — ForkParticle effect definition data structures.
// Each effect has one or more emitters, each referencing a .psb file.

#include <string>
#include <vector>

namespace lu::assets {

struct EffectEmitter {
    std::string name;           // Maps to {name}.psb in the same directory
    float transform[16] = {    // 4x4 row-major transform matrix
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };
    int facing = 0;
    int rot = 0;
    int trail = 0;
    float time = 0.0f;
    int ds = 0;                 // distance sort?
    int se = 0;                 // ?
    int mt = 0;                 // ?
    float dist = 0.0f;          // max render distance
    float dmin = 0.0f;          // min render distance
    int prio = 0;               // priority
    int loop = 0;               // loop override
};

struct EffectFile {
    std::vector<EffectEmitter> emitters;
};

} // namespace lu::assets
