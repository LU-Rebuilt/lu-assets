#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <stdexcept>

namespace lu::assets {

// AST (Asset List) plain-text parser.
// Each line has an "A:" prefix followed by a file path (e.g. "A:res\audio\file.fsb").
// Lines starting with '#' are comments. Empty lines are skipped.
// The "A:" prefix is stripped and backslashes are normalized to forward slashes.

struct AstError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct AstFile {
    std::vector<std::string> asset_paths;
};
} // namespace lu::assets
