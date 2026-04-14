#pragma once
#include "forkparticle/effect/effect_types.h"

namespace lu::assets {

// Parse a ForkParticle effect .txt file.
EffectFile effect_parse(const std::string& text);

} // namespace lu::assets
