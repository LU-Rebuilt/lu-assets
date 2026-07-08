#include "netdevil/zone/ast/ast_reader.h"

#include <algorithm>

namespace lu::assets {

AstFile ast_parse(std::span<const uint8_t> data) {
    AstFile ast;
    ast.lines = split_lines(data);

    for (const TextLine& line : ast.lines) {
        // Skip empty lines and comments
        if (line.text.empty() || line.text[0] == '#') {
            continue;
        }

        std::string path = line.text;

        // Strip "A:" prefix
        if (path.size() >= 2 && path[0] == 'A' && path[1] == ':') {
            path = path.substr(2);
        }

        // Normalize backslashes to forward slashes
        std::replace(path.begin(), path.end(), '\\', '/');

        ast.asset_paths.push_back(std::move(path));
    }

    return ast;
}

} // namespace lu::assets
