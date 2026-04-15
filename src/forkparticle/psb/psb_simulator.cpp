#include "forkparticle/psb/psb_simulator.h"

#include <algorithm>
#include <cmath>

namespace lu::assets {

// Client spawn function: FUN_01097c60 (legouniverse.exe)
// All randomized values use lerp(min, max, random01), NOT min + random * variance.
PsbParticle psb_spawn_particle(const PsbFile& psb, const float m[16],
                                std::mt19937& rng) {
    PsbParticle p{};

    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    // Lifetime: lerp(life_min, life_var, random) — FUN_01097c60
    // life_min = PLIFEMIN (minimum), life_var = PLIFEVAR (maximum)
    // Client: maxLife = (life_var - life_min) * random + life_min
    float r = dist01(rng);
    p.maxLife = psb.life_min + (psb.life_var - psb.life_min) * r;
    if (p.maxLife <= 0.0f) p.maxLife = -p.maxLife; // Client: flip sign if <= 0
    if (p.maxLife < 0.001f) p.maxLife = 0.001f;
    p.age = 0.0f; // Client: age starts at 0, increases to maxLife

    // Per-particle size randomization — FUN_01097c60
    // Each size phase: lerp(variation, base, random)
    // iscale_var/tscale_var/fscale_var are the MINIMUM bounds
    // initial_scale/trans_scale/final_scale are the MAXIMUM bounds
    p.sizeStart = psb.iscale_var + (psb.initial_scale - psb.iscale_var) * dist01(rng);
    p.sizeMid   = psb.tscale_var + (psb.trans_scale   - psb.tscale_var) * dist01(rng);
    p.sizeEnd   = psb.fscale_var + (psb.final_scale   - psb.fscale_var) * dist01(rng);

    // Emission position — generate within volume then transform
    // Volume-based position (simplified: point emitter + offset)
    float lx = 0, ly = 0, lz = 0;

    // Emission volume: FUN_01097af0
    uint32_t volType = psb.volume_type & 0xFF;
    switch (volType) {
        case 0: // Point — emit from origin
            break;
        case 1: { // Box — random within plane_w × plane_h × plane_d
            lx = (dist01(rng) - 0.5f) * psb.plane_w;
            ly = (dist01(rng) - 0.5f) * psb.plane_h;
            lz = (dist01(rng) - 0.5f) * psb.plane_d;
            break;
        }
        case 3: { // Sphere — random inside sphere of radius cone_radius
            float u = dist01(rng);
            float theta = dist01(rng) * 2.0f * PSB_PI;
            float phi = std::acos(1.0f - 2.0f * dist01(rng));
            float radius = psb.cone_radius * std::cbrt(u);
            lx = radius * std::sin(phi) * std::cos(theta);
            ly = radius * std::cos(phi);
            lz = radius * std::sin(phi) * std::sin(theta);
            break;
        }
        case 4: { // Cone surface — random on cone
            float angle = dist01(rng) * 2.0f * PSB_PI;
            float h = dist01(rng);
            float rad = psb.cone_radius * h;
            lx = rad * std::cos(angle);
            lz = rad * std::sin(angle);
            ly = h * psb.plane_h;
            break;
        }
        case 5: { // Cylinder — random inside cylinder
            float angle = dist01(rng) * 2.0f * PSB_PI;
            float rad = psb.cone_radius * std::sqrt(dist01(rng));
            lx = rad * std::cos(angle);
            lz = rad * std::sin(angle);
            ly = (dist01(rng) - 0.5f) * psb.plane_h;
            break;
        }
        default: // Arc and others — fall through to point
            break;
    }

    // Add emitter offset (*OFFSET at PSB+0x140-0x148)
    lx += psb.emitter_offset_x;
    ly += psb.emitter_offset_y;
    lz += psb.emitter_offset_z;

    // Transform position to world space
    p.x = m[0]*lx + m[4]*ly + m[8]*lz  + m[12];
    p.y = m[1]*lx + m[5]*ly + m[9]*lz  + m[13];
    p.z = m[2]*lx + m[6]*ly + m[10]*lz + m[14];

    // Velocity: lerp(vel_min, vel_var, random) — FUN_01097c60
    // vel_min = PVELMIN, vel_var = PVELVAR
    float speed = psb.vel_min + (psb.vel_var - psb.vel_min) * dist01(rng);

    // Direction from emission cone (*ECONERAD)
    float coneRad = psb.cone_radius * PSB_DEG_TO_RAD;
    float theta = dist01(rng) * coneRad;
    float phi = dist01(rng) * 2.0f * PSB_PI;
    float sinT = std::sin(theta);

    float lvx = sinT * std::cos(phi) * speed;
    float lvy = std::cos(theta) * speed;
    float lvz = sinT * std::sin(phi) * speed;

    // Transform velocity by emitter rotation (3x3 part of matrix)
    p.vx = m[0]*lvx + m[4]*lvy + m[8]*lvz;
    p.vy = m[1]*lvx + m[5]*lvy + m[9]*lvz;
    p.vz = m[2]*lvx + m[6]*lvy + m[10]*lvz;

    // Spin: lerp(rot_min, rot_var, random) — FUN_01097c60
    float spin = psb.rot_min + (psb.rot_var - psb.rot_min) * dist01(rng);

    // Spin direction from flags — FUN_01097c60
    // flags & 0x400000: always negative spin
    // flags & 0x200000: always positive spin (no negate)
    // otherwise: 50% random negate
    if ((psb.flags & 0x400000) != 0) {
        spin = -spin;
    } else if ((psb.flags & 0x200000) == 0) {
        // 50% chance negate (client: random % 1000 >= 500)
        if (dist01(rng) >= 0.5f) spin = -spin;
    }

    // Decode particle rendering mode from flags — FUN_010cdbf0
    auto mode = decode_particle_mode(psb.flags);

    // For velocity-aligned modes (2, 5): negate spin — FUN_01097c60
    if (mode == ParticleMode::VelocityStreak || mode == ParticleMode::VelocityStreakNoDrag)
        spin = -spin;

    p.spinRate = spin * PSB_DEG_TO_RAD;
    // Client: initial rotation = spinRate (FUN_01097c60: particle[0x14] = particle[0x15])
    p.rotation = p.spinRate;

    // Texture start index: if flags & 1, random start frame
    if ((psb.flags & 1) != 0 && psb.num_assets > 0) {
        p.texStartIdx = static_cast<int>(dist01(rng) * psb.num_assets);
        if (p.texStartIdx >= static_cast<int>(psb.num_assets))
            p.texStartIdx = static_cast<int>(psb.num_assets) - 1;
    }
    p.texIndex = p.texStartIdx;

    return p;
}

// Client update: FUN_01093fc0 + FUN_0109bb30 (legouniverse.exe)
int psb_tick_particles(std::vector<PsbParticle>& particles,
                       const PsbFile& psb, float dt) {
    // flags & 0x100: skip drag/force physics — FUN_0109bb30
    bool skipDragForces = (psb.flags & 0x100) != 0;

    for (auto& p : particles) {
        // Advance age (client: age goes UP from 0 to maxLife)
        p.age += dt;

        if (!skipDragForces) {
            // Drag: vel += vel * drag * dt — FUN_01093fc0
            if (psb.drag != 0.0f) {
                p.vx += p.vx * psb.drag * dt;
                p.vy += p.vy * psb.drag * dt;
                p.vz += p.vz * psb.drag * dt;
            }

            // Gravity: vel.y += gravity * dt — FUN_01093fc0
            p.vy += psb.gravity * dt;
        }

        // Position: pos += vel * dt — FUN_01093fc0
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.z += p.vz * dt;

        // Rotation: rotation += spinRate * dt — FUN_0109bb30
        p.rotation += p.spinRate * dt;
    }

    // Remove dead particles (age >= maxLife) — FUN_0109bb30
    particles.erase(
        std::remove_if(particles.begin(), particles.end(),
                        [](const PsbParticle& p) { return p.age >= p.maxLife; }),
        particles.end());
    return static_cast<int>(particles.size());
}

// Client color interpolation: FUN_0109bb30 (legouniverse.exe)
// Uses precomputed inverse denominators for each phase.
PsbColor psb_lerp_color(const PsbFile& psb, float t) {
    float cr1 = psb.color_ratio_1;
    float cr2 = psb.color_ratio_2;
    if (cr1 <= 0.0f) cr1 = 0.001f;
    if (cr2 <= cr1) cr2 = cr1 + 0.001f;

    PsbColor c;
    if (t < cr1) {
        // Phase 1: initial → transitional 1
        float s = t / cr1;
        c.r = psb.initial_color.r + (psb.trans_color_1.r - psb.initial_color.r) * s;
        c.g = psb.initial_color.g + (psb.trans_color_1.g - psb.initial_color.g) * s;
        c.b = psb.initial_color.b + (psb.trans_color_1.b - psb.initial_color.b) * s;
        c.a = psb.initial_color.a + (psb.trans_color_1.a - psb.initial_color.a) * s;
    } else if (t < cr2) {
        // Phase 2: transitional 1 → transitional 2
        float s = (t - cr1) / (cr2 - cr1);
        c.r = psb.trans_color_1.r + (psb.trans_color_2.r - psb.trans_color_1.r) * s;
        c.g = psb.trans_color_1.g + (psb.trans_color_2.g - psb.trans_color_1.g) * s;
        c.b = psb.trans_color_1.b + (psb.trans_color_2.b - psb.trans_color_1.b) * s;
        c.a = psb.trans_color_1.a + (psb.trans_color_2.a - psb.trans_color_1.a) * s;
    } else {
        // Phase 3: transitional 2 → final
        float denom = 1.0f - cr2;
        if (denom < 0.001f) denom = 0.001f;
        float s = std::min((t - cr2) / denom, 1.0f);
        c.r = psb.trans_color_2.r + (psb.final_color.r - psb.trans_color_2.r) * s;
        c.g = psb.trans_color_2.g + (psb.final_color.g - psb.trans_color_2.g) * s;
        c.b = psb.trans_color_2.b + (psb.final_color.b - psb.trans_color_2.b) * s;
        c.a = psb.trans_color_2.a + (psb.final_color.a - psb.trans_color_2.a) * s;
    }

    // Modulate by tint — FUN_0109bb30 uses emitter tint (PSB+0xA8)
    c.r *= psb.tint.r; c.g *= psb.tint.g;
    c.b *= psb.tint.b; c.a *= psb.tint.a;
    return c;
}

// Client size interpolation: FUN_0109bb30
// Uses per-particle randomized sizes and scale_ratio threshold.
float psb_lerp_size(const PsbParticle& p, const PsbFile& psb, float t) {
    float ratio = psb.scale_ratio;
    if (ratio <= 0.0f) ratio = 0.5f;

    float sz;
    if (t < ratio) {
        // Phase 1: sizeStart → sizeMid
        float s = t / ratio;
        sz = p.sizeStart + (p.sizeMid - p.sizeStart) * s;
    } else {
        // Phase 2: sizeMid → sizeEnd
        float denom = 1.0f - ratio;
        if (denom < 0.001f) denom = 0.001f;
        float s = (t - ratio) / denom;
        sz = p.sizeMid + (p.sizeEnd - p.sizeMid) * s;
    }

    return std::max(sz * PSB_SIZE_SCALE, 0.001f);
}

// Client texture animation: FUN_0109bb30
// texIndex = (startIdx + (int)(anim_speed * age)) % num_assets
int psb_texture_index(const PsbFile& psb, const PsbParticle& p) {
    int numTex = static_cast<int>(psb.texture_uv_rects.size());
    if (numTex <= 1) return 0;

    if ((psb.flags & 2) != 0 && psb.anim_speed > 0) {
        // Animated texture (flags & 2): cycle through frames
        int frame = static_cast<int>(psb.anim_speed * p.age);
        return (p.texStartIdx + frame) % numTex;
    }

    // Random texture (flags & 1): use start index
    return p.texStartIdx % numTex;
}

} // namespace lu::assets
