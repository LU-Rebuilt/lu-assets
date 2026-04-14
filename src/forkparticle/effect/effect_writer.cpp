#include "forkparticle/effect/effect_writer.h"

#include <sstream>
#include <iomanip>

namespace lu::assets {

std::string effect_write(const EffectFile& effect) {
    std::ostringstream out;
    for (const auto& em : effect.emitters) {
        out << "EMITTERNAME: " << em.name << "\n";
        out << "TRANSFORM:";
        for (int i = 0; i < 16; ++i)
            out << " " << std::fixed << std::setprecision(6) << em.transform[i];
        out << "\n";
        out << "FACING: " << em.facing << "\n";
        out << "ROT: " << em.rot << "\n";
        out << "TRAIL: " << em.trail << "\n";
        if (em.time != 0.0f) out << "TIME: " << std::fixed << std::setprecision(6) << em.time << "\n";
        out << "DS: " << em.ds << "\n";
        out << "SE: " << em.se << "\n";
        if (em.mt != 0) out << "MT: " << em.mt << "\n";
        if (em.dist != 0.0f) out << "DIST: " << static_cast<int>(em.dist) << "\n";
        if (em.dmin != 0.0f) out << "DMIN: " << static_cast<int>(em.dmin) << "\n";
        if (em.prio != 0) out << "PRIO: " << em.prio << "\n";
        if (em.loop != 0) out << "LOOP: " << em.loop << "\n";
    }
    return out.str();
}

} // namespace lu::assets
