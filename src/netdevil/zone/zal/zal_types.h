#pragma once

#include "common/text_lines/text_lines.h"

#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace lu::assets {

struct ZalError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ZalFile {
    // Every line of the file verbatim (comments, blank lines, original separators,
    // per-line terminators) — what zal_write emits for byte-identical round-trips.
    std::vector<TextLine> lines;

    // Parsed view derived from `lines` at parse time (blank lines dropped, backslashes normalized). Rebuilt by
    // zal_parse; edits meant to reach disk belong in `lines`.
    std::vector<std::string> asset_paths;
};
} // namespace lu::assets
