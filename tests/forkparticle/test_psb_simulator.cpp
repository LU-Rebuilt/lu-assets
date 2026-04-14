#include "forkparticle/psb/psb_simulator.h"
#include <gtest/gtest.h>
#include <cmath>

using namespace lu::assets;

static PsbFile make_test_psb() {
    PsbFile psb{};
    psb.life_max = 2.0f;
    psb.birth_rate = 10.0f;
    psb.emit_speed = 5.0f;
    psb.spread_angle = 30.0f;
    psb.gravity = 9.8f;
    psb.size_start = 1.0f;
    psb.size_end = 0.5f;
    psb.start_color = {1, 0, 0, 1};
    psb.middle_color = {0, 1, 0, 1};
    psb.end_color = {0, 0, 1, 1};
    psb.birth_color = {0, 0, 0, 0}; // death color
    psb.color2 = {1, 1, 1, 1};      // emitter tint (no tint)
    psb.birth_delay = 0.3f;          // color transition 1
    psb.life_min = 0.7f;             // color transition 2
    psb.playback_scale = 1.0f;
    return psb;
}

static const float IDENTITY[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

TEST(PsbSimulator, SpawnParticleHasLife) {
    auto psb = make_test_psb();
    std::mt19937 rng(42);
    auto p = psb_spawn_particle(psb, IDENTITY, rng);
    EXPECT_FLOAT_EQ(p.maxLife, 2.0f);
    EXPECT_FLOAT_EQ(p.life, 2.0f);
}

TEST(PsbSimulator, SpawnParticleAtOrigin) {
    auto psb = make_test_psb();
    std::mt19937 rng(42);
    auto p = psb_spawn_particle(psb, IDENTITY, rng);
    // With identity transform and zero designer offsets, position is at origin
    EXPECT_FLOAT_EQ(p.x, 0.0f);
    EXPECT_FLOAT_EQ(p.y, 0.0f);
    EXPECT_FLOAT_EQ(p.z, 0.0f);
}

TEST(PsbSimulator, SpawnParticleHasVelocity) {
    auto psb = make_test_psb();
    std::mt19937 rng(42);
    auto p = psb_spawn_particle(psb, IDENTITY, rng);
    float speed = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
    EXPECT_GT(speed, 0.0f); // should have nonzero velocity
}

TEST(PsbSimulator, TickReducesLife) {
    auto psb = make_test_psb();
    std::mt19937 rng(42);
    std::vector<PsbParticle> particles;
    particles.push_back(psb_spawn_particle(psb, IDENTITY, rng));
    float original_life = particles[0].life;
    psb_tick_particles(particles, psb, 0.1f);
    EXPECT_LT(particles[0].life, original_life);
}

TEST(PsbSimulator, TickRemovesDead) {
    auto psb = make_test_psb();
    std::mt19937 rng(42);
    std::vector<PsbParticle> particles;
    particles.push_back(psb_spawn_particle(psb, IDENTITY, rng));
    // Tick past lifetime
    int remaining = psb_tick_particles(particles, psb, 10.0f);
    EXPECT_EQ(remaining, 0);
    EXPECT_TRUE(particles.empty());
}

TEST(PsbSimulator, TickAppliesGravity) {
    auto psb = make_test_psb();
    std::mt19937 rng(42);
    std::vector<PsbParticle> particles;
    auto p = psb_spawn_particle(psb, IDENTITY, rng);
    p.vy = 0.0f; // start with no vertical velocity
    particles.push_back(p);
    psb_tick_particles(particles, psb, 1.0f);
    EXPECT_LT(particles[0].vy, 0.0f); // gravity pulls down
}

TEST(PsbSimulator, ColorLerpAtBirth) {
    auto psb = make_test_psb();
    auto c = psb_lerp_color(psb, 0.0f);
    EXPECT_NEAR(c.r, 1.0f, 0.01f); // start_color red
    EXPECT_NEAR(c.g, 0.0f, 0.01f);
}

TEST(PsbSimulator, ColorLerpAtDeath) {
    auto psb = make_test_psb();
    auto c = psb_lerp_color(psb, 1.0f);
    // At t=1.0, should be at death color (birth_color in PSB naming = {0,0,0,0})
    EXPECT_NEAR(c.a, 0.0f, 0.1f);
}

TEST(PsbSimulator, SizeLerpStartToEnd) {
    auto psb = make_test_psb();
    float s0 = psb_lerp_size(psb, 0.0f);
    float s1 = psb_lerp_size(psb, 1.0f);
    EXPECT_GT(s0, s1); // size_start > size_end means shrinking
    EXPECT_NEAR(s0, 0.5f, 0.01f); // size_start * SIZE_SCALE
    EXPECT_NEAR(s1, 0.25f, 0.01f); // size_end * SIZE_SCALE
}

TEST(PsbSimulator, TextureIndexSingle) {
    auto psb = make_test_psb();
    // No texture rects → always index 0
    EXPECT_EQ(psb_texture_index(psb, 0.0f), 0);
    EXPECT_EQ(psb_texture_index(psb, 5.0f), 0);
}
