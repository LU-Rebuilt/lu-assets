#include "netdevil/macros/scm/scm_reader.h"

namespace lu::assets {

ScmFile scm_parse(std::span<const uint8_t> data) {
    ScmFile scm;
    scm.lines = split_lines(data);

    for (const TextLine& line : scm.lines) {
        if (line.text.empty()) {
            continue;
        }
        scm.commands.push_back(line.text);
    }

    return scm;
}

} // namespace lu::assets
