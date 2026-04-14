#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <stdexcept>

namespace lu::assets {

// AUD zone audio configuration XML parser.
// Each .aud file defines audio parameters for a scene/area, including the
// FMOD music cue, 2D/3D sound GUIDs, group/program names, and boredom timing.
//
// Format:
//   <SceneAudioAttributes musicCue="NT_Hallway" musicParamName=""
//     guid2D="{...}" guid3D="" groupName="" programName=""
//     musicParamValue="0" boredomTime="-1" />

struct AudError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct AudFile {
    std::string music_cue;
    std::string music_param_name;
    std::string guid_2d;
    std::string guid_3d;
    std::string group_name;
    std::string program_name;
    float music_param_value = 0.0f;
    float boredom_time = -1.0f;
};
} // namespace lu::assets
