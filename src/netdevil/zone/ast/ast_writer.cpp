#include "netdevil/zone/ast/ast_writer.h"

namespace lu::assets {

std::vector<uint8_t> ast_write(const AstFile& f) {
    return join_lines(f.lines);
}

} // namespace lu::assets
