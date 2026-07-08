#include "netdevil/zone/aud/aud_writer.h"

namespace lu::assets {

std::vector<uint8_t> aud_write(const AudFile& f) {
    return f.raw;
}

} // namespace lu::assets
