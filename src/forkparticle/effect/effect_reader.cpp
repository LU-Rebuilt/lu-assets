#include "forkparticle/effect/effect_reader.h"

#include <sstream>
#include <string>
#include <algorithm>

namespace lu::assets {

EffectFile effect_parse(const std::string& text) {
    EffectFile ef;
    EffectEmitter* current = nullptr;

    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
            line.pop_back();
        if (line.empty()) continue;

        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        while (!val.empty() && val[0] == ' ') val.erase(val.begin());

        if (key == "EMITTERNAME") {
            ef.emitters.push_back({});
            current = &ef.emitters.back();
            current->name = val;
        } else if (current) {
            if (key == "TRANSFORM") {
                std::istringstream ss(val);
                for (int i = 0; i < 16; ++i) ss >> current->transform[i];
            } else if (key == "FACING") { current->facing = std::stoi(val);
            } else if (key == "ROT") { current->rot = std::stoi(val);
            } else if (key == "TRAIL") { current->trail = std::stoi(val);
            } else if (key == "TIME") { current->time = std::stof(val);
            } else if (key == "DS") { current->ds = std::stoi(val);
            } else if (key == "SE") { current->se = std::stoi(val);
            } else if (key == "MT") { current->mt = std::stoi(val);
            } else if (key == "DIST") { current->dist = std::stof(val);
            } else if (key == "DMIN") { current->dmin = std::stof(val);
            } else if (key == "PRIO") { current->prio = std::stoi(val);
            } else if (key == "LOOP") { current->loop = std::stoi(val);
            }
        }
    }

    return ef;
}

} // namespace lu::assets
