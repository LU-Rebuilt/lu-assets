#include "netdevil/zone/zal/zal_reader.h"

#include <algorithm>

namespace lu::assets {

ZalFile zal_parse(std::span<const uint8_t> data) {
    ZalFile zal;
    zal.lines = split_lines(data);

    for (const TextLine& line : zal.lines) {
        if (line.text.empty()) {
            continue;
        }

        // Normalize backslashes to forward slashes
        std::string path = line.text;
        std::replace(path.begin(), path.end(), '\\', '/');

        zal.asset_paths.push_back(std::move(path));
    }

    return zal;
}

} // namespace lu::assets
