#include "netdevil/zone/ast/ast_reader.h"

#include <algorithm>
#include <sstream>

namespace lu::assets {

AstFile ast_parse(std::span<const uint8_t> data) {
    AstFile ast;

    std::string text(reinterpret_cast<const char*>(data.data()), data.size());
    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line)) {
        // Strip trailing \r if present (Windows line endings)
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Strip "A:" prefix
        if (line.size() >= 2 && line[0] == 'A' && line[1] == ':') {
            line = line.substr(2);
        }

        // Normalize backslashes to forward slashes
        std::replace(line.begin(), line.end(), '\\', '/');

        ast.asset_paths.push_back(std::move(line));
    }

    return ast;
}

} // namespace lu::assets
