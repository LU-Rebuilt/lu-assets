#include "forkparticle/psb/psb_simulator.h"

#include <algorithm>
#include <cmath>

namespace lu::assets {

PsbParticle psb_spawn_particle(const PsbFile& psb, const float m[16],
                                std::mt19937& rng) {
    PsbParticle p{};

    // Lifetime (PSB+0x58)
    p.maxLife = psb.life_max;
    p.life = p.maxLife;
    if (p.life <= 0) p.life = p.maxLife = 1.0f;

    // Local position from designer offsets
    float lx = psb.designer_offset_x;
    float ly = psb.designer_offset_y;
    float lz = psb.designer_offset_z;

    // Transform to world space
    p.x = m[0]*lx + m[4]*ly + m[8]*lz  + m[12];
    p.y = m[1]*lx + m[5]*ly + m[9]*lz  + m[13];
    p.z = m[2]*lx + m[6]*ly + m[10]*lz + m[14];

    // Velocity from spread cone
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    std::uniform_real_distribution<float> distPM(-1.0f, 1.0f);

    float spreadRad = psb.spread_angle * PSB_DEG_TO_RAD;  // PSB+0x80
    float theta = dist01(rng) * spreadRad;
    float phi = dist01(rng) * 2.0f * 3.14159265f;
    float sinT = std::sin(theta);

    float speed = psb.emit_speed;  // PSB+0x6C
    float lvx = sinT * std::cos(phi) * speed + psb.speed_x;  // PSB+0x70
    float lvy = std::cos(theta) * speed + psb.speed_y;        // PSB+0x74
    float lvz = sinT * std::sin(phi) * speed; // No speed_z — PSB+0x78 is size_transition

    // Transform velocity by emitter rotation
    p.vx = m[0]*lvx + m[4]*lvy + m[8]*lvz;
    p.vy = m[1]*lvx + m[5]*lvy + m[9]*lvz;
    p.vz = m[2]*lvx + m[6]*lvy + m[10]*lvz;

    // Rotation: convert to radians
    float rotRad = psb.initial_rotation * PSB_DEG_TO_RAD;  // PSB+0x98
    int bm = static_cast<int>(psb.texture_blend_mode);
    if (bm == 0 || bm == 1 || bm == 4 || bm == 8) rotRad *= -1.0f;
    p.rotation = rotRad + psb.spin_start * PSB_DEG_TO_RAD
                 + distPM(rng) * psb.spin_var * PSB_DEG_TO_RAD;

    // Spin rate
    float spinMin = psb.spin_min, spinMax = psb.spin_max;
    if (spinMax < spinMin) std::swap(spinMin, spinMax);
    p.spinRate = (psb.rotation_speed + psb.spin_speed
                  + spinMin + dist01(rng) * (spinMax - spinMin)) * PSB_DEG_TO_RAD;

    p.texIndex = 0;
    return p;
}

int psb_tick_particles(std::vector<PsbParticle>& particles,
                       const PsbFile& psb, float dt) {
    for (auto& p : particles) {
        p.life -= dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.z += p.vz * dt;
        p.vy -= psb.gravity * dt;     // PSB+0x7C
        p.vx += psb.accel_x * dt;     // PSB+0xB8
        p.vy += psb.accel_y * dt;     // PSB+0xBC
        p.vz += psb.accel_z * dt;     // PSB+0xC0
        p.rotation += p.spinRate * dt;
    }
    particles.erase(
        std::remove_if(particles.begin(), particles.end(),
                        [](const PsbParticle& p) { return p.life <= 0; }),
        particles.end());
    return static_cast<int>(particles.size());
}

PsbColor psb_lerp_color(const PsbFile& psb, float t) {
    // Transition thresholds from PSB timing block
    float trans1 = psb.birth_delay;  // PSB+0x50 (color_midpoint_1)
    float trans2 = psb.life_min;     // PSB+0x54 (color_midpoint_2)
    if (trans1 <= 0) trans1 = 0.001f;
    if (trans2 <= trans1) trans2 = trans1 + 0.001f;

    PsbColor c;
    if (t < trans1) {
        float s = t / trans1;
        c.r = psb.start_color.r + (psb.middle_color.r - psb.start_color.r) * s;
        c.g = psb.start_color.g + (psb.middle_color.g - psb.start_color.g) * s;
        c.b = psb.start_color.b + (psb.middle_color.b - psb.start_color.b) * s;
        c.a = psb.start_color.a + (psb.middle_color.a - psb.start_color.a) * s;
    } else if (t < trans2) {
        float s = (t - trans1) / (trans2 - trans1);
        c.r = psb.middle_color.r + (psb.end_color.r - psb.middle_color.r) * s;
        c.g = psb.middle_color.g + (psb.end_color.g - psb.middle_color.g) * s;
        c.b = psb.middle_color.b + (psb.end_color.b - psb.middle_color.b) * s;
        c.a = psb.middle_color.a + (psb.end_color.a - psb.middle_color.a) * s;
    } else {
        float s = std::min((t - trans2) / (1.0f - trans2), 1.0f);
        c.r = psb.end_color.r + (psb.birth_color.r - psb.end_color.r) * s;
        c.g = psb.end_color.g + (psb.birth_color.g - psb.end_color.g) * s;
        c.b = psb.end_color.b + (psb.birth_color.b - psb.end_color.b) * s;
        c.a = psb.end_color.a + (psb.birth_color.a - psb.end_color.a) * s;
    }

    // Modulate by emitter tint
    c.r *= psb.color2.r; c.g *= psb.color2.g;
    c.b *= psb.color2.b; c.a *= psb.color2.a;
    return c;
}

float psb_lerp_size(const PsbFile& psb, float t) {
    float sz = psb.size_start * PSB_SIZE_SCALE
             + (psb.size_end * PSB_SIZE_SCALE - psb.size_start * PSB_SIZE_SCALE) * t;
    return std::max(sz, 0.001f);
}

int psb_texture_index(const PsbFile& psb, float age) {
    int numTex = static_cast<int>(psb.texture_uv_rects.size());
    if (numTex <= 1 || psb.emit_rate_final <= 0) return 0;
    return static_cast<int>(psb.emit_rate_final * age) % numTex;
}

} // namespace lu::assets
