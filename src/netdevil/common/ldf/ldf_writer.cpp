#include "netdevil/common/ldf/ldf_writer.h"

#include <string>

namespace lu::assets {

std::string ldf_write_text(const std::vector<LdfEntry>& entries) {
    std::string text;
    for (auto& e : entries) {
        text += e.key;
        text += '=';
        text += std::to_string(static_cast<int>(e.type));
        text += ':';
        text += e.raw_value;
        text += '\n';
    }
    return text;
}

void ldf_write_binary(BinaryWriter& w, const LdfConfig& config) {
    w.write_u32(static_cast<uint32_t>(config.size()));
    for (auto& [key, val] : config) {
        w.write_wstr8(key);
        w.write_wstr8(val);
    }
}

} // namespace lu::assets
