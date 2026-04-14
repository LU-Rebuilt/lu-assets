#include "netdevil/macros/scm/scm_reader.h"

#include <sstream>

namespace lu::assets {

ScmFile scm_parse(std::span<const uint8_t> data) {
    ScmFile scm;

    std::string text(reinterpret_cast<const char*>(data.data()), data.size());
    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line)) {
        // Strip trailing \r if present (Windows line endings)
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            continue;
        }

        scm.commands.push_back(std::move(line));
    }

    return scm;
}

} // namespace lu::assets
