#include "netdevil/macros/scm/scm_writer.h"

namespace lu::assets {

std::vector<uint8_t> scm_write(const ScmFile& f) {
    return join_lines(f.lines);
}

} // namespace lu::assets
