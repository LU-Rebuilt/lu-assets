#pragma once
// psb_simulator.h — ForkParticle PSB simulation engine.
//
// Platform-independent particle spawn, tick, and color interpolation logic
// matching the original LEGO Universe client. Verified against:
//   - FUN_0109bb30: per-particle update (color, size, texture, age, death)
//   - FUN_01093fc0: velocity update (drag, point forces, gravity, position)
//   - FUN_01097c60: particle spawn (lifetime, velocity, spin, size randomization)
//   - FUN_010d2f90: emitter update (emission rate, accumulator, max_particles cap)
//   - MediaFX CEmitterStatsDlg (FUN_0046f890): field name verification

#include "forkparticle/psb/psb_types.h"

#include <cstdint>
#include <random>
#include <vector>

namespace lu::assets {

// Match client's ForkParticle constants from Ghidra RE
constexpr float PSB_SIZE_SCALE = 0.5f;       // DOUBLE_015fcc48
constexpr float PSB_DEG_TO_RAD = 3.14159265f / 180.0f;
constexpr float PSB_PI = 3.14159265f;

// Particle rendering mode — decoded from PSB flags (PSB+0x68) by FUN_010cdbf0.
// This is NOT the same as blend_mode (EBLENDMODE at PSB+0x10C).
//
// RE SOURCE: FUN_010cdbf0 (legouniverse.exe), RenderParticleDLL.dll,
//            GeomObjectDLL.dll (.PAX DirectX mesh loading)
//
// Modes 0-5: Sprite-based (billboard quads rendered by RenderParticleDLL.dll)
// Modes 6-9: Model-based (3D meshes in .PAX format rendered by GeomObjectDLL.dll)
//
// Billboard modes (0,1,3,4): camera-facing quad with per-particle rotation.
//   Modes 1,4,8 negate the emitter's initial rotation (PSB+0x98) — FUN_01092450.
//
// Velocity-aligned modes (2,5): quad stretches along velocity direction to
//   create streak/spark effects. Spin is negated — FUN_01097c60.
//   Mode 5 is often paired with flags & 0x100 which skips drag/forces.
//
// Model modes (6-9): each particle renders a DirectX .x mesh (.PAX file)
//   instead of a billboard quad. Loaded by GeomObjectDLL.dll.
enum class ParticleMode : int {
    Billboard = 0,          // Camera-facing billboard (default)
    BillboardNeg = 1,       // Camera-facing, negated initial rotation
    VelocityStreak = 2,     // Quad stretched along velocity direction (sparks, streaks)
    BillboardAlt = 3,       // Billboard variant
    BillboardNeg2 = 4,      // Camera-facing, negated initial rotation
    VelocityStreakNoDrag = 5,// Velocity-aligned, often skips drag (flags & 0x100)
    Model1 = 6,             // 3D mesh particle (GeomObjectDLL, .PAX format)
    Model2 = 7,             // 3D mesh variant
    Model3 = 8,             // 3D mesh, negated rotation
    Model4 = 9,             // 3D mesh variant
};

// Decode particle rendering mode from PSB flags — matches FUN_010cdbf0
inline ParticleMode decode_particle_mode(uint32_t flags) {
    if      (flags & 0x8)       return ParticleMode::BillboardNeg;
    else if (flags & 0x10)      return ParticleMode::VelocityStreak;
    else if (flags & 0x20)      return ParticleMode::BillboardAlt;
    else if (flags & 0x40)      return ParticleMode::BillboardNeg2;
    else if (flags & 0x80000)   return ParticleMode::VelocityStreakNoDrag;
    else if (flags & 0x4000000) return ParticleMode::Model1;
    else if (flags & 0x800000)  return ParticleMode::Model2;
    else if (flags & 0x1000000) return ParticleMode::Model3;
    else if (flags & 0x2000000) return ParticleMode::Model4;
    return ParticleMode::Billboard;
}

struct PsbParticle {
    float x = 0, y = 0, z = 0;       // position
    float vx = 0, vy = 0, vz = 0;    // velocity
    float age = 0;                    // elapsed time since birth (increases)
    float maxLife = 0;                // lifetime at death
    float rotation = 0;               // current rotation angle (radians)
    float spinRate = 0;               // angular velocity (radians/s)
    // Per-particle randomized sizes (client: FUN_01097c60)
    float sizeStart = 0;              // lerp(iscale_var, initial_scale, random)
    float sizeMid = 0;                // lerp(tscale_var, trans_scale, random)
    float sizeEnd = 0;                // lerp(fscale_var, final_scale, random)
    int texStartIdx = 0;              // random start index for texture (flags & 1)
    int texIndex = 0;                 // current texture frame
};

// Spawn a single particle from a PSB emitter definition.
// transform: 4x4 column-major matrix for emitter world position.
PsbParticle psb_spawn_particle(const PsbFile& psb, const float transform[16],
                                std::mt19937& rng);

// Tick all particles: apply velocity, gravity, drag, rotation, and
// remove dead particles. Returns number of particles remaining.
int psb_tick_particles(std::vector<PsbParticle>& particles,
                       const PsbFile& psb, float dt);

// Compute particle color at normalized age t (0 = birth, 1 = death).
// 3-phase lerp matching FUN_0109bb30.
PsbColor psb_lerp_color(const PsbFile& psb, float t);

// Compute particle size at normalized age t using per-particle randomized sizes.
// 2-phase interpolation at scale_ratio threshold.
float psb_lerp_size(const PsbParticle& p, const PsbFile& psb, float t);

// Compute texture frame index at given age (seconds since spawn).
int psb_texture_index(const PsbFile& psb, const PsbParticle& p);

} // namespace lu::assets
