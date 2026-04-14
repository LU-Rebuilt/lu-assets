#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <stdexcept>

namespace lu::assets {

// ZAL (Zone Asset List) plain-text parser.
// One file path per line, listing all assets required by a zone.
// Backslashes are normalized to forward slashes.

struct ZalError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ZalFile {
    std::vector<std::string> asset_paths;
};
} // namespace lu::assets
