#include "forkparticle/effect/effect_reader.h"

#include <sstream>
#include <string>
#include <algorithm>

namespace lu::assets {

namespace {

// Trim trailing spaces, for tag/value extraction only.
std::string trim_for_parse(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

} // namespace

EffectFile effect_parse(const std::string& text) {
    EffectFile ef;
    ef.line_ending = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    ef.trailing_newline = !text.empty() &&
        (text.back() == '\n' || text.back() == '\r');

    EffectEmitter* current = nullptr;

    // Split on '\n' only, then drop a trailing '\r' from the piece (CRLF files) — the
    // stored raw_lines/prefix_lines entry must NOT carry its own terminator, since
    // effect_write() re-adds exactly one file-wide line_ending between entries; keeping
    // the '\r' here would double it up on every CRLF file. The final element after the
    // last '\n' is the (usually empty) tail after the file's closing line terminator —
    // skipped via last_iteration below so it doesn't turn into a phantom trailing blank
    // line.
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        bool last_iteration = (nl == std::string::npos);
        std::string line = last_iteration ? text.substr(pos) : text.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (!last_iteration || !line.empty()) {
            std::string trimmed = trim_for_parse(line);
            std::string tag, val;
            auto colon = trimmed.find(':');
            if (colon != std::string::npos) {
                tag = trimmed.substr(0, colon);
                val = trimmed.substr(colon + 1);
                while (!val.empty() && val.front() == ' ') val.erase(val.begin());
            }

            if (tag == "EMITTERNAME") {
                ef.emitters.push_back({});
                current = &ef.emitters.back();
                current->name = val;
            } else if (current) {
                try {
                    if (tag == "TRANSFORM") {
                        std::istringstream ss(val);
                        for (int i = 0; i < 16; ++i) ss >> current->transform[i];
                    } else if (tag == "FACING") { current->facing = std::stoi(val);
                    } else if (tag == "ROT") { current->rot = std::stoi(val);
                    } else if (tag == "TRAIL") { current->trail = std::stoi(val);
                    } else if (tag == "TIME") { current->time = std::stof(val);
                    } else if (tag == "DS") { current->ds = std::stoi(val);
                    } else if (tag == "SE") { current->se = std::stoi(val);
                    } else if (tag == "MT") { current->mt = std::stoi(val);
                    } else if (tag == "DIST") { current->dist = std::stof(val);
                    } else if (tag == "DMIN") { current->dmin = std::stof(val);
                    } else if (tag == "PRIO") {
                        // May be "N" or a per-client comma list "id:val,id:val" — only
                        // the plain-integer form is reflected in the typed field.
                        if (val.find(',') == std::string::npos &&
                            val.find(':') == std::string::npos) {
                            current->prio = std::stoi(val);
                        }
                    } else if (tag == "LOOP") { current->loop = std::stoi(val);
                    } else if (tag == "RSX") { current->rsx = std::stoi(val);
                    } else if (tag == "CR") {
                        current->cr = std::stof(val);
                        current->has_cr = true;
                    }
                    // Unrecognised tags (including authoring typos like a stray leading
                    // "l" on "lEMITTERNAME" continuation lines, or "DSl") fall through
                    // silently — the line is still preserved verbatim below.
                } catch (const std::exception&) {
                    // Malformed numeric value on an otherwise-recognised tag — the typed
                    // field stays at its default; raw_lines still carries the real text.
                }
            }

            // Preserve every line verbatim (blank lines included — real files do contain
            // stray blank lines, mostly before the first EMITTERNAME but occasionally
            // mid-file), attached to whichever emitter is currently open, or to the
            // file-level prefix if none has started yet.
            if (current) current->raw_lines.push_back(line);
            else ef.prefix_lines.push_back(line);
        }

        if (last_iteration) break;
        pos = nl + 1;
    }

    for (auto& em : ef.emitters) {
        auto& snap = em.parse_snapshot;
        for (int i = 0; i < 16; ++i) snap.transform[i] = em.transform[i];
        snap.facing = em.facing; snap.prio = em.prio;
        snap.rot = em.rot; snap.trail = em.trail; snap.ds = em.ds; snap.se = em.se;
        snap.mt = em.mt; snap.loop = em.loop; snap.rsx = em.rsx;
        snap.time = em.time; snap.dist = em.dist; snap.dmin = em.dmin; snap.cr = em.cr;
        snap.has_cr = em.has_cr;
    }

    return ef;
}

} // namespace lu::assets
