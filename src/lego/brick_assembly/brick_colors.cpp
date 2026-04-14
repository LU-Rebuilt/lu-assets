#include "lego/brick_assembly/brick_colors.h"

#include <unordered_map>

namespace lu::assets {

namespace {

// LEGO material colors in linear color space, sourced from lu-toolbox.
// Format: material_id → {r, g, b, a}
const std::unordered_map<int, BrickColor>& color_table() {
    static const std::unordered_map<int, BrickColor> table = {
        // ── Opaque ──
        {1,   {0.904661f, 0.904661f, 0.904661f, 1.0f}},  // White
        {5,   {0.693872f, 0.491021f, 0.194618f, 1.0f}},  // Brick Yellow (tan)
        {18,  {0.508881f, 0.215861f, 0.051269f, 1.0f}},  // Nougat
        {21,  {0.730461f, 0.0f,      0.004025f, 1.0f}},   // Bright Red
        {23,  {0.0f,      0.141263f, 0.610496f, 1.0f}},   // Bright Blue
        {24,  {0.846873f, 0.632252f, 0.0f,      1.0f}},   // Bright Yellow
        {26,  {0.015996f, 0.015996f, 0.015996f, 1.0f}},   // Black
        {28,  {0.005605f, 0.141263f, 0.017642f, 1.0f}},   // Dark Green
        {29,  {0.016807f, 0.258183f, 0.016807f, 1.0f}},   // Medium Green
        {36,  {0.846873f, 0.508881f, 0.258183f, 1.0f}},   // Light Yellowish Orange
        {37,  {0.053705f, 0.215861f, 0.005605f, 1.0f}},   // Bright Green
        {38,  {0.318547f, 0.098899f, 0.014444f, 1.0f}},   // Dark Orange
        {39,  {0.341914f, 0.215861f, 0.610496f, 1.0f}},   // Light Violet
        {40,  {0.854993f, 0.854993f, 0.854993f, 1.0f}},   // Transparent (placeholder)
        {100, {0.846873f, 0.508881f, 0.428690f, 1.0f}},   // Light Red
        {101, {0.508881f, 0.141263f, 0.141263f, 1.0f}},   // Medium Red
        {102, {0.258183f, 0.318547f, 0.730461f, 1.0f}},   // Medium Blue
        {103, {0.467784f, 0.467784f, 0.467784f, 1.0f}},   // Light Grey
        {104, {0.258183f, 0.016807f, 0.341914f, 1.0f}},   // Bright Violet
        {105, {0.846873f, 0.428690f, 0.082283f, 1.0f}},   // Bright Yellowish Orange
        {106, {0.846873f, 0.341914f, 0.017642f, 1.0f}},   // Bright Orange
        {107, {0.0f,      0.318547f, 0.428690f, 1.0f}},   // Bright Bluish Green
        {110, {0.051269f, 0.098899f, 0.258183f, 1.0f}},   // Bright Bluish Violet
        {112, {0.215861f, 0.318547f, 0.730461f, 1.0f}},   // Medium Bluish Violet
        {115, {0.508881f, 0.693872f, 0.016807f, 1.0f}},   // Medium Yellowish Green
        {116, {0.0f,      0.508881f, 0.508881f, 1.0f}},   // Medium Bluish Green
        {118, {0.341914f, 0.693872f, 0.610496f, 1.0f}},   // Light Bluish Green
        {119, {0.428690f, 0.610496f, 0.016807f, 1.0f}},   // Bright Yellowish Green
        {120, {0.467784f, 0.730461f, 0.215861f, 1.0f}},   // Light Yellowish Green
        {124, {0.341914f, 0.014444f, 0.258183f, 1.0f}},   // Bright Reddish Violet
        {135, {0.258183f, 0.258183f, 0.258183f, 1.0f}},   // Sand Blue
        {138, {0.341914f, 0.258183f, 0.141263f, 1.0f}},   // Sand Yellow
        {140, {0.016807f, 0.033105f, 0.082283f, 1.0f}},   // Earth Blue
        {141, {0.016807f, 0.053705f, 0.005605f, 1.0f}},   // Earth Green
        {148, {0.141263f, 0.141263f, 0.141263f, 1.0f}},   // Dark Stone Grey
        {151, {0.258183f, 0.258183f, 0.258183f, 1.0f}},   // Medium Stone Grey
        {153, {0.341914f, 0.215861f, 0.141263f, 1.0f}},   // Sand Red
        {154, {0.318547f, 0.033105f, 0.016807f, 1.0f}},   // Dark Red
        {191, {0.846873f, 0.610496f, 0.082283f, 1.0f}},   // Flame Yellowish Orange
        {192, {0.258183f, 0.082283f, 0.014444f, 1.0f}},   // Reddish Brown
        {194, {0.428690f, 0.428690f, 0.428690f, 1.0f}},   // Medium Stone Grey (alt)
        {199, {0.141263f, 0.141263f, 0.141263f, 1.0f}},   // Dark Stone Grey (alt)
        {208, {0.730461f, 0.693872f, 0.610496f, 1.0f}},   // Light Stone Grey
        {212, {0.341914f, 0.508881f, 0.846873f, 1.0f}},   // Medium Royal Blue
        {221, {0.730461f, 0.141263f, 0.467784f, 1.0f}},   // Bright Purple
        {222, {0.904661f, 0.508881f, 0.610496f, 1.0f}},   // Light Purple
        {226, {0.904661f, 0.846873f, 0.258183f, 1.0f}},   // Cool Yellow
        {232, {0.215861f, 0.610496f, 0.846873f, 1.0f}},   // Dove Blue
        {268, {0.141263f, 0.082283f, 0.318547f, 1.0f}},   // Medium Lilac
        {283, {0.730461f, 0.508881f, 0.258183f, 1.0f}},   // Light Nougat
        {308, {0.033105f, 0.016807f, 0.005605f, 1.0f}},   // Dark Brown
        {312, {0.508881f, 0.341914f, 0.082283f, 1.0f}},   // Medium Nougat
        {321, {0.0f,      0.318547f, 0.508881f, 1.0f}},   // Dark Azur
        {322, {0.258183f, 0.610496f, 0.846873f, 1.0f}},   // Medium Azur
        {323, {0.610496f, 0.846873f, 0.904661f, 1.0f}},   // Aqua
        {324, {0.508881f, 0.141263f, 0.428690f, 1.0f}},   // Medium Lavender
        {325, {0.693872f, 0.428690f, 0.693872f, 1.0f}},   // Lavender
        {326, {0.730461f, 0.846873f, 0.082283f, 1.0f}},   // Spring Yellowish Green
        {330, {0.508881f, 0.341914f, 0.258183f, 1.0f}},   // Olive Green
        {484, {0.318547f, 0.082283f, 0.016807f, 1.0f}},   // Dark Orange (alt)

        // ── Transparent ──
        {40,  {0.854993f, 0.854993f, 0.854993f, 0.5f}},   // Transparent
        {41,  {0.730461f, 0.016807f, 0.004025f, 0.5f}},   // Tr. Red
        {42,  {0.341914f, 0.610496f, 0.846873f, 0.5f}},   // Tr. Light Blue
        {43,  {0.215861f, 0.318547f, 0.846873f, 0.5f}},   // Tr. Blue
        {44,  {0.846873f, 0.610496f, 0.016807f, 0.5f}},   // Tr. Yellow
        {47,  {0.846873f, 0.258183f, 0.016807f, 0.5f}},   // Tr. Fluorescent Reddish Orange
        {48,  {0.258183f, 0.508881f, 0.016807f, 0.5f}},   // Tr. Green
        {49,  {0.854993f, 0.854993f, 0.854993f, 0.5f}},   // Tr. Fluorescent Green
        {111, {0.467784f, 0.428690f, 0.428690f, 0.5f}},   // Tr. Brown
        {113, {0.730461f, 0.428690f, 0.610496f, 0.5f}},   // Tr. Medium Reddish Violet
        {126, {0.610496f, 0.341914f, 0.846873f, 0.5f}},   // Tr. Bright Bluish Violet
        {143, {0.341914f, 0.610496f, 0.846873f, 0.5f}},   // Tr. Fluorescent Blue
        {182, {0.846873f, 0.341914f, 0.082283f, 0.5f}},   // Tr. Bright Orange
        {311, {0.258183f, 0.508881f, 0.016807f, 0.5f}},   // Tr. Bright Green

        // ── Metallic ──
        {145, {0.508881f, 0.508881f, 0.508881f, 1.0f}},   // Sand Blue Metallic
        {147, {0.258183f, 0.215861f, 0.141263f, 1.0f}},   // Metallic Sand Yellow
        {148, {0.141263f, 0.141263f, 0.141263f, 1.0f}},   // Metallic Dark Grey
        {150, {0.318547f, 0.318547f, 0.318547f, 1.0f}},   // Metallic Light Grey
        {179, {0.610496f, 0.508881f, 0.341914f, 1.0f}},   // Silver flip/flop
        {183, {0.730461f, 0.693872f, 0.610496f, 1.0f}},   // Metallic White
        {186, {0.467784f, 0.318547f, 0.082283f, 1.0f}},   // Metallic Gold
        {187, {0.258183f, 0.215861f, 0.141263f, 1.0f}},   // Metallic Dark Grey (Earth Orange)
        {200, {0.428690f, 0.428690f, 0.428690f, 1.0f}},   // Lemon Metallic
        {297, {0.846873f, 0.610496f, 0.016807f, 1.0f}},   // Warm Gold
        {298, {0.258183f, 0.215861f, 0.215861f, 1.0f}},   // Cool Silver
        {315, {0.508881f, 0.508881f, 0.508881f, 1.0f}},   // Silver Metallic

        // ── Glow ──
        {294, {0.904661f, 0.846873f, 0.610496f, 1.0f}},   // Phosph. White
        {329, {0.904661f, 0.904661f, 0.508881f, 1.0f}},   // White Glow
    };
    return table;
}

} // anonymous namespace

BrickColor brick_color_lookup(int material_id) {
    const auto& table = color_table();
    auto it = table.find(material_id);
    if (it != table.end()) return it->second;
    // Fallback: black
    return {0.015996f, 0.015996f, 0.015996f, 1.0f};
}

} // namespace lu::assets
