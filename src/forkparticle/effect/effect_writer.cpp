#include "forkparticle/effect/effect_writer.h"

#include <sstream>
#include <iomanip>

namespace lu::assets {

namespace {

// Regenerate an emitter's text from its typed fields, in the field order and
// conditional-emission style seen across the majority of the real corpus. Used only when
// there's no verbatim source to replay: a freshly-constructed emitter (raw_lines empty,
// e.g. psb-editor's "Add Emitter"), or one whose typed fields were edited after parsing
// (psb-editor's property panel) — see EffectEmitter::matches_snapshot().
std::vector<std::string> regenerate_lines(const EffectEmitter& em) {
    std::vector<std::string> lines;
    auto push = [&](std::ostringstream& ss) { lines.push_back(ss.str()); ss.str({}); };

    std::ostringstream ss;
    ss << "EMITTERNAME: " << em.name; push(ss);
    ss << "TRANSFORM:";
    for (int i = 0; i < 16; ++i) ss << " " << std::fixed << std::setprecision(6) << em.transform[i];
    push(ss);
    ss << "FACING: " << em.facing; push(ss);
    ss << "ROT: " << em.rot; push(ss);
    ss << "TRAIL: " << em.trail; push(ss);
    if (em.time != 0.0f) { ss << "TIME: " << std::fixed << std::setprecision(6) << em.time; push(ss); }
    ss << "DS: " << em.ds; push(ss);
    ss << "SE: " << em.se; push(ss);
    if (em.mt != 0) { ss << "MT: " << em.mt; push(ss); }
    if (em.dist != 0.0f) { ss << "DIST: " << static_cast<int64_t>(em.dist); push(ss); }
    if (em.dmin != 0.0f) { ss << "DMIN: " << static_cast<int64_t>(em.dmin); push(ss); }
    if (em.prio != 0) { ss << "PRIO: " << em.prio; push(ss); }
    if (em.loop != 0) { ss << "LOOP: " << em.loop; push(ss); }
    if (em.rsx != 0) { ss << "RSX: " << em.rsx; push(ss); }
    if (em.has_cr) { ss << "CR: " << static_cast<int64_t>(em.cr); push(ss); }
    return lines;
}

} // namespace

// Byte-perfect round-trip for the unmodified case: no writer tool for this format exists
// in any client-side binary (confirmed via Ghidra — legouniverse.exe only ever reads it),
// and the real corpus has no discoverable formatting rule (field order varies per-emitter,
// values are conditionally emitted, DIST/DMIN/LOOP/PRIO/CR are usually-but-not-always bare
// integers, and a handful of files carry authoring typos like "lEMITTERNAME"/"DSl" that
// the client's tag matching evidently tolerates). So this re-emits each parsed line
// verbatim instead of reformatting from the typed EffectEmitter fields — except for an
// emitter a consumer has actually edited (see EffectEmitter::matches_snapshot), where raw
// replay would silently discard the edit; those are regenerated from the live fields.
std::string effect_write(const EffectFile& effect) {
    std::vector<std::string> lines = effect.prefix_lines;
    for (const auto& em : effect.emitters) {
        if (!em.raw_lines.empty() && em.matches_snapshot()) {
            lines.insert(lines.end(), em.raw_lines.begin(), em.raw_lines.end());
        } else {
            auto regen = regenerate_lines(em);
            lines.insert(lines.end(), regen.begin(), regen.end());
        }
    }

    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size()) out += effect.line_ending;
    }
    if (!lines.empty() && effect.trailing_newline) out += effect.line_ending;
    return out;
}

} // namespace lu::assets
