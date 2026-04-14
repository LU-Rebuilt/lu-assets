#pragma once
// psb_simulator.h — ForkParticle PSB simulation engine.
//
// Platform-independent particle spawn, tick, and color interpolation logic
// matching the original LEGO Universe client (from Ghidra RE of FUN_01092450,
// FUN_0109bb30). No rendering code — tools and the game client provide their
// own rendering backends.

#include "forkparticle/psb/psb_types.h"

#include <cstdint>
#include <random>
#include <vector>

namespace lu::assets {

// Match client's ForkParticle constants from Ghidra RE
constexpr float PSB_SIZE_SCALE = 0.5f;       // DOUBLE_015fcc48
constexpr float PSB_DEG_TO_RAD = 3.14159265f / 180.0f;

struct PsbParticle {
    float x = 0, y = 0, z = 0;
    float vx = 0, vy = 0, vz = 0;
    float life = 0;
    float maxLife = 0;
    float size = 0;
    float rotation = 0;
    float spinRate = 0;
    int texIndex = 0;
};

// Spawn a single particle from a PSB emitter definition.
// transform: 4x4 column-major matrix for emitter world position.
PsbParticle psb_spawn_particle(const PsbFile& psb, const float transform[16],
                                std::mt19937& rng);

// Tick all particles: apply velocity, gravity, acceleration, rotation, and
// remove dead particles. Returns number of particles remaining.
int psb_tick_particles(std::vector<PsbParticle>& particles,
                       const PsbFile& psb, float dt);

// Compute particle color at normalized age t (0 = birth, 1 = death).
// 3-phase lerp matching FUN_0109bb30.
PsbColor psb_lerp_color(const PsbFile& psb, float t);

// Compute particle size at normalized age t (0 = birth, 1 = death).
float psb_lerp_size(const PsbFile& psb, float t);

// Compute texture frame index at given age (seconds since spawn).
int psb_texture_index(const PsbFile& psb, float age);

} // namespace lu::assets
