#include "scaleform/gfx/gfx_reader.h"

#include <zlib.h>
#include <string>
#include <cstring>
#include <stdexcept>

namespace lu::assets {

// ---------------------------------------------------------------------------
// Bit reader for the RECT field (SWF uses bit-aligned records here)
// ---------------------------------------------------------------------------

struct BitReader {
    const uint8_t* data;
    size_t         size;
    size_t         byte_pos = 0;
    int            bit_pos  = 7; // next bit to read within data[byte_pos]

    BitReader(const uint8_t* d, size_t s, size_t start)
        : data(d), size(s), byte_pos(start) {}

    int read_bit() {
        if (byte_pos >= size) throw GfxError("GFX: RECT bit-read past end of data");
        int b = (data[byte_pos] >> bit_pos) & 1;
        if (--bit_pos < 0) { bit_pos = 7; ++byte_pos; }
        return b;
    }

    uint32_t read_ubits(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) v = (v << 1) | read_bit();
        return v;
    }

    int32_t read_sbits(int n) {
        if (n == 0) return 0;
        uint32_t raw = read_ubits(n);
        // Sign-extend
        if (raw & (1u << (n - 1)))
            raw |= ~((1u << n) - 1u);
        return static_cast<int32_t>(raw);
    }

    // Byte position after consuming current byte (aligned up)
    size_t aligned_byte_pos() const {
        return (bit_pos == 7) ? byte_pos : byte_pos + 1;
    }
};

// ---------------------------------------------------------------------------
// zlib decompression helper
// ---------------------------------------------------------------------------

static std::vector<uint8_t> inflate_stream(const uint8_t* src, size_t src_len,
                                           size_t hint_size) {
    std::vector<uint8_t> out;
    out.reserve(hint_size > 0 ? hint_size : 65536);

    z_stream strm = {};
    strm.next_in  = const_cast<uint8_t*>(src);
    strm.avail_in = static_cast<uInt>(src_len);

    if (inflateInit(&strm) != Z_OK)
        throw GfxError("GFX: inflateInit failed");

    uint8_t buf[65536];
    int ret;
    do {
        strm.next_out  = buf;
        strm.avail_out = sizeof(buf);
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            inflateEnd(&strm);
            throw GfxError("GFX: inflate error " + std::to_string(ret));
        }
        size_t have = sizeof(buf) - strm.avail_out;
        out.insert(out.end(), buf, buf + have);
    } while (ret != Z_STREAM_END && strm.avail_in > 0);

    inflateEnd(&strm);
    return out;
}

// ---------------------------------------------------------------------------
// Tag record parser
// ---------------------------------------------------------------------------

static std::vector<GfxTag> parse_tags(const uint8_t* swf, size_t swf_size,
                                      size_t tags_start) {
    std::vector<GfxTag> tags;
    size_t pos = tags_start;

    while (pos + 2 <= swf_size) {
        uint16_t record_hdr;
        std::memcpy(&record_hdr, swf + pos, 2);
        pos += 2;

        uint16_t tag_type = record_hdr >> 6;
        uint32_t tag_len  = record_hdr & 0x3F;

        if (tag_len == 0x3F) {
            // Long-form record: u32 length follows
            if (pos + 4 > swf_size)
                throw GfxError("GFX: truncated long-form tag length");
            std::memcpy(&tag_len, swf + pos, 4);
            pos += 4;
        }

        if (pos + tag_len > swf_size)
            throw GfxError("GFX: tag data extends past end of file (type=" +
                           std::to_string(tag_type) + " len=" + std::to_string(tag_len) + ")");

        GfxTag t;
        t.type = tag_type;
        t.data.assign(swf + pos, swf + pos + tag_len);
        tags.push_back(std::move(t));

        pos += tag_len;

        if (tag_type == 0) break; // End tag
    }

    return tags;
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

GfxFile gfx_parse(std::span<const uint8_t> data) {
    if (data.size() < 8)
        throw GfxError("GFX: file too small for header (" +
                        std::to_string(data.size()) + " bytes)");

    GfxFile gfx;
    const uint8_t* raw = data.data();

    // --- Header ---
    // Magic: GFX/CFX (Scaleform) or FWS/CWS (standard SWF)
    uint8_t m0 = raw[0], m1 = raw[1], m2 = raw[2];
    bool is_gfx = (m1 == 'F' && m2 == 'X');
    bool is_swf = (m1 == 'W' && m2 == 'S');
    if (!is_gfx && !is_swf)
        throw GfxError(std::string("GFX: invalid magic '") +
                       char(m0) + char(m1) + char(m2) + "'");

    gfx.is_compressed = (m0 == 'C');
    gfx.swf_version   = raw[3];
    std::memcpy(&gfx.file_length_field, raw + 4, 4);

    // --- Decompress if needed ---
    if (!gfx.is_compressed) {
        gfx.swf_data.assign(data.begin(), data.end());
    } else {
        // Compressed: header (8 bytes) is uncompressed; rest is deflate stream.
        gfx.swf_data.resize(8);
        std::memcpy(gfx.swf_data.data(), raw, 8);
        // Rewrite magic to uncompressed form (CFX→GFX, CWS→FWS)
        gfx.swf_data[0] = (m1 == 'F') ? 'G' : 'F';

        size_t hint = gfx.file_length_field > 8 ? gfx.file_length_field - 8 : 0;
        auto payload = inflate_stream(raw + 8, data.size() - 8, hint);
        gfx.swf_data.insert(gfx.swf_data.end(), payload.begin(), payload.end());
    }

    const uint8_t* swf = gfx.swf_data.data();
    const size_t   swf_size = gfx.swf_data.size();

    if (swf_size < 9)
        throw GfxError("GFX: decompressed body too small");

    // --- RECT (frame_size) ---
    // Bit-aligned: [5-bit Nbits][4 × Nbits signed twip values]
    BitReader br(swf, swf_size, 8);
    int nbits = static_cast<int>(br.read_ubits(5));
    gfx.frame_size.x_min = br.read_sbits(nbits);
    gfx.frame_size.x_max = br.read_sbits(nbits);
    gfx.frame_size.y_min = br.read_sbits(nbits);
    gfx.frame_size.y_max = br.read_sbits(nbits);
    size_t after_rect = br.aligned_byte_pos();

    // --- FrameRate + FrameCount ---
    if (after_rect + 4 > swf_size)
        throw GfxError("GFX: truncated after RECT");
    std::memcpy(&gfx.frame_rate,  swf + after_rect,     2);
    std::memcpy(&gfx.frame_count, swf + after_rect + 2, 2);

    // --- Tags ---
    gfx.tags = parse_tags(swf, swf_size, after_rect + 4);

    return gfx;
}

} // namespace lu::assets
