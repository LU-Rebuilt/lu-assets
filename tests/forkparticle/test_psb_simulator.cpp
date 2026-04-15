#include "forkparticle/psb/psb_simulator.h"
#include <gtest/gtest.h>
#include <cmath>

using namespace lu::assets;

static PsbFile make_test_psb() {
    PsbFile psb{};
    psb.life_min = 2.0f;             // *PLIFEMIN: minimum lifetime
    psb.life_var = 2.0f;             // *PLIFEVAR: same as min for deterministic tests
    psb.emit_rate = 10.0f;           // *ERATE: emission rate
    psb.vel_min = 5.0f;              // *PVELMIN: minimum velocity
    psb.vel_var = 5.0f;              // *PVELVAR: same as min for deterministic tests
    psb.cone_radius = 30.0f;         // *ECONERAD: emission cone radius
    psb.gravity = -9.8f;             // *EGRAVITY: negative = downward (Y-up)
    psb.initial_scale = 1.0f;        // *PISCALE: scale at birth (max)
    psb.trans_scale = 0.75f;         // *PTSCALE: scale at midlife (max)
    psb.final_scale = 0.5f;          // *PFSCALE: scale at death (max)
    psb.iscale_var = 1.0f;           // *ISCALEMIN: scale at birth (min)
    psb.tscale_var = 0.75f;          // *TSCALEMIN: scale at midlife (min)
    psb.fscale_var = 0.5f;           // *FSCALEMIN: scale at death (min)
    psb.scale_ratio = 0.5f;          // *PSCALERATIO: midlife transition point
    psb.initial_color = {1, 0, 0, 1};   // *PICOLOR
    psb.trans_color_1 = {0, 1, 0, 1};   // *PTCOLOR1
    psb.trans_color_2 = {0, 0, 1, 1};   // *PTCOLOR2
    psb.final_color = {0, 0, 0, 0};     // *PFCOLOR
    psb.tint = {1, 1, 1, 1};            // *TINT: no tint
    psb.color_ratio_1 = 0.3f;        // *PCOLORRATIO
    psb.color_ratio_2 = 0.7f;        // *PCOLORRATIO2
    psb.time_delta_mult = 1.0f;      // *TDELTAMULT
    psb.max_particles = 100.0f;      // *EMAXPARTICLE
    return psb;
}

static const float IDENTITY[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

TEST(PsbSimulator, SpawnParticleHasLife) {
    auto psb = make_test_psb();
    std::mt19937 rng(42);
    auto p = psb_spawn_particle(psb, IDENTITY, rng);
    EXPECT_FLOAT_EQ(p.maxLife, 2.0f); // life_min == life_var so always 2.0
    EXPECT_FLOAT_EQ(p.age, 0.0f);     // age starts at 0
}

TEST(PsbSimulator, SpawnParticleAtOrigin) {
    auto psb = make_test_psb();
    std::mt19937 rng(42);
    auto p = psb_spawn_particle(psb, IDENTITY, rng);
    EXPECT_FLOAT_EQ(p.x, 0.0f);
    EXPECT_FLOAT_EQ(p.y, 0.0f);
    EXPECT_FLOAT_EQ(p.z, 0.0f);
}

TEST(PsbSimulator, SpawnParticleHasVelocity) {
    auto psb = make_test_psb();
    std::mt19937 rng(42);
    auto p = psb_spawn_particle(psb, IDENTITY, rng);
    float speed = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
    EXPECT_GT(speed, 0.0f);
}

TEST(PsbSimulator, SpawnParticleHasPerParticleSize) {
    auto psb = make_test_psb();
    psb.iscale_var = 0.5f;   // min for birth
    psb.initial_scale = 2.0f; // max for birth
    std::mt19937 rng(42);
    auto p = psb_spawn_particle(psb, IDENTITY, rng);
    EXPECT_GE(p.sizeStart, 0.5f);
    EXPECT_LE(p.sizeStart, 2.0f);
}

TEST(PsbSimulator, TickAdvancesAge) {
    auto psb = make_test_psb();
    std::mt19937 rng(42);
    std::vector<PsbParticle> particles;
    particles.push_back(psb_spawn_particle(psb, IDENTITY, rng));
    EXPECT_FLOAT_EQ(particles[0].age, 0.0f);
    psb_tick_particles(particles, psb, 0.1f);
    EXPECT_NEAR(particles[0].age, 0.1f, 0.001f);
}

TEST(PsbSimulator, TickRemovesDead) {
    auto psb = make_test_psb();
    std::mt19937 rng(42);
    std::vector<PsbParticle> particles;
    particles.push_back(psb_spawn_particle(psb, IDENTITY, rng));
    int remaining = psb_tick_particles(particles, psb, 10.0f);
    EXPECT_EQ(remaining, 0);
    EXPECT_TRUE(particles.empty());
}

TEST(PsbSimulator, TickAppliesGravity) {
    auto psb = make_test_psb();
    psb.gravity = -10.0f; // negative = downward
    std::mt19937 rng(42);
    std::vector<PsbParticle> particles;
    auto p = psb_spawn_particle(psb, IDENTITY, rng);
    p.vy = 0.0f;
    particles.push_back(p);
    psb_tick_particles(particles, psb, 1.0f);
    EXPECT_LT(particles[0].vy, 0.0f); // gravity pulls down
}

TEST(PsbSimulator, TickAppliesDrag) {
    auto psb = make_test_psb();
    psb.drag = -1.0f; // negative drag = deceleration
    std::mt19937 rng(42);
    std::vector<PsbParticle> particles;
    auto p = psb_spawn_particle(psb, IDENTITY, rng);
    p.vx = 10.0f; p.vy = 0.0f; p.vz = 0.0f;
    particles.push_back(p);
    psb_tick_particles(particles, psb, 0.1f);
    EXPECT_LT(std::abs(particles[0].vx), 10.0f); // velocity decreased
}

TEST(PsbSimulator, ColorLerpAtBirth) {
    auto psb = make_test_psb();
    auto c = psb_lerp_color(psb, 0.0f);
    EXPECT_NEAR(c.r, 1.0f, 0.01f);
    EXPECT_NEAR(c.g, 0.0f, 0.01f);
}

TEST(PsbSimulator, ColorLerpAtDeath) {
    auto psb = make_test_psb();
    auto c = psb_lerp_color(psb, 1.0f);
    EXPECT_NEAR(c.a, 0.0f, 0.1f);
}

TEST(PsbSimulator, SizeLerpWithPerParticleValues) {
    auto psb = make_test_psb();
    PsbParticle p;
    p.sizeStart = 1.0f;
    p.sizeMid = 0.75f;
    p.sizeEnd = 0.5f;
    float s0 = psb_lerp_size(p, psb, 0.0f);
    float s1 = psb_lerp_size(p, psb, 1.0f);
    EXPECT_GT(s0, s1); // shrinking particle
    EXPECT_NEAR(s0, 0.5f, 0.01f); // sizeStart * SIZE_SCALE
    EXPECT_NEAR(s1, 0.25f, 0.01f); // sizeEnd * SIZE_SCALE
}

TEST(PsbSimulator, TextureIndexSingle) {
    auto psb = make_test_psb();
    PsbParticle p;
    p.age = 0.0f;
    p.texStartIdx = 0;
    EXPECT_EQ(psb_texture_index(psb, p), 0);
}
