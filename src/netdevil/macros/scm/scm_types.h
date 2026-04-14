#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <stdexcept>

namespace lu::assets {

// SCM (Script Macro) plain-text parser.
// One slash command per line (e.g. "/gmadditem 3").
// Used for GM macros and test scripts.

struct ScmError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ScmFile {
    std::vector<std::string> commands;
};
} // namespace lu::assets
