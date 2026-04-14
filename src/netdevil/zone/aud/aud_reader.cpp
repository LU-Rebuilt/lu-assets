#include "netdevil/zone/aud/aud_reader.h"

#include <pugixml.hpp>

namespace lu::assets {

AudFile aud_parse(std::span<const uint8_t> data) {
    pugi::xml_document doc;
    auto result = doc.load_buffer(data.data(), data.size());
    if (!result) {
        throw AudError("AUD: XML parse error: " + std::string(result.description()));
    }

    auto root = doc.child("SceneAudioAttributes");
    if (!root) {
        throw AudError("AUD: missing root <SceneAudioAttributes> element");
    }

    AudFile aud;
    aud.music_cue = root.attribute("musicCue").as_string();
    aud.music_param_name = root.attribute("musicParamName").as_string();
    aud.guid_2d = root.attribute("guid2D").as_string();
    aud.guid_3d = root.attribute("guid3D").as_string();
    aud.group_name = root.attribute("groupName").as_string();
    aud.program_name = root.attribute("programName").as_string();
    aud.music_param_value = root.attribute("musicParamValue").as_float();
    aud.boredom_time = root.attribute("boredomTime").as_float(-1.0f);

    return aud;
}

} // namespace lu::assets
