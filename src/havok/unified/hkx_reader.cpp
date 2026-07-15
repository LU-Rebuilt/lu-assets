#include "havok/unified/hkx_reader.h"
#include "havok/packfile/hkx_packfile_reader.h"
#include "havok/tagged/hkx_tagged_binary_reader.h"

#include <cstring>

namespace lu::assets {

HkxAny hkx_parse(std::span<const uint8_t> data) {
    if (data.size() < 8) {
        throw HkxFormatError("HKX: file smaller than the 8-byte magic");
    }

    uint32_t magic0, magic1;
    std::memcpy(&magic0, data.data(), 4);
    std::memcpy(&magic1, data.data() + 4, 4);

    // Default member initializers on HkxPackfileHeader/HkxTaggedBinary already spell
    // out these values (0x57E0E057/0x10C0C010 and 0xCAB00D1E/0xD011FACE respectively)
    // -- comparing against a default-constructed instance keeps the magic numbers
    // defined in exactly one place per format instead of a third copy here.
    if (magic0 == HkxPackfileHeader{}.magic0 && magic1 == HkxPackfileHeader{}.magic1) {
        return hkx_packfile_parse(data);
    }
    if (magic0 == HkxTaggedBinary{}.magic0 && magic1 == HkxTaggedBinary{}.magic1) {
        return hkx_tagged_binary_parse(data);
    }

    throw HkxFormatError(
        "HKX: not a supported container format (expected binary packfile or "
        "tagged binary magic; XML HKX is not round-trip supported)");
}

} // namespace lu::assets
