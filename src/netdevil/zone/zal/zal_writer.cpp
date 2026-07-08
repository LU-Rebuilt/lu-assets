#include "netdevil/zone/zal/zal_writer.h"

namespace lu::assets {

std::vector<uint8_t> zal_write(const ZalFile& f) {
    return join_lines(f.lines);
}

} // namespace lu::assets
