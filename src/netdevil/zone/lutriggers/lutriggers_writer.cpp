#include "netdevil/zone/lutriggers/lutriggers_writer.h"

namespace lu::assets {

std::vector<uint8_t> lutriggers_write(const LuTriggersFile& f) {
    return f.raw;
}

} // namespace lu::assets
