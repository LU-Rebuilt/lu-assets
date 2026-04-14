#include "netdevil/zone/zal/zal_reader.h"

#include <algorithm>
#include <sstream>

namespace lu::assets {

ZalFile zal_parse(std::span<const uint8_t> data) {
    ZalFile zal;

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

        // Normalize backslashes to forward slashes
        std::replace(line.begin(), line.end(), '\\', '/');

        zal.asset_paths.push_back(std::move(line));
    }

    return zal;
}

} // namespace lu::assets
