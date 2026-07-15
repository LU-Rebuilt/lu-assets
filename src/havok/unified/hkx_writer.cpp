#include "havok/unified/hkx_writer.h"
#include "havok/packfile/hkx_packfile_writer.h"
#include "havok/tagged/hkx_tagged_binary_writer.h"

#include <variant>

namespace lu::assets {

std::vector<uint8_t> hkx_write(const HkxAny& file) {
    return std::visit(
        [](const auto& f) -> std::vector<uint8_t> {
            using T = std::decay_t<decltype(f)>;
            if constexpr (std::is_same_v<T, HkxPackfile>) {
                return hkx_packfile_write(f);
            } else {
                static_assert(std::is_same_v<T, HkxTaggedBinary>);
                return hkx_tagged_binary_write(f);
            }
        },
        file);
}

} // namespace lu::assets
