#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace lu::assets {

class BinaryWriter {
public:
    void write_u8(uint8_t v)   { buf_.push_back(v); }
    void write_u16(uint16_t v) { append(&v, 2); }
    void write_s16(int16_t v)  { append(&v, 2); }
    void write_u32(uint32_t v) { append(&v, 4); }
    void write_s32(int32_t v)  { append(&v, 4); }
    void write_u64(uint64_t v) { append(&v, 8); }
    void write_f32(float v)    { append(&v, 4); }
    void write_bool(bool v)    { write_u8(v ? 1 : 0); }

    void write_string8(std::string_view s) {
        write_u8(static_cast<uint8_t>(s.size()));
        buf_.insert(buf_.end(), s.begin(), s.end());
    }

    void write_string32(std::string_view s) {
        write_u32(static_cast<uint32_t>(s.size()));
        buf_.insert(buf_.end(), s.begin(), s.end());
    }

    void write_wstr8(std::string_view s) {
        write_u8(static_cast<uint8_t>(s.size()));
        for (char c : s) write_u16(static_cast<uint16_t>(static_cast<uint8_t>(c)));
    }

    void write_wstr32(std::string_view s) {
        write_u32(static_cast<uint32_t>(s.size()));
        for (char c : s) write_u16(static_cast<uint16_t>(static_cast<uint8_t>(c)));
    }

    void write_u4_str(std::string_view s) {
        write_u32(static_cast<uint32_t>(s.size()));
        buf_.insert(buf_.end(), s.begin(), s.end());
    }

    void write_fixed_str(std::string_view s, size_t field_size) {
        size_t copy = (s.size() < field_size) ? s.size() : field_size;
        buf_.insert(buf_.end(), s.begin(), s.begin() + copy);
        for (size_t i = copy; i < field_size; ++i) buf_.push_back(0);
    }

    void write_bytes(const uint8_t* data, size_t n) {
        buf_.insert(buf_.end(), data, data + n);
    }

    void write_zeros(size_t n) {
        buf_.insert(buf_.end(), n, 0);
    }

    size_t pos() const { return buf_.size(); }

    void patch_u32(size_t offset, uint32_t v) {
        std::memcpy(buf_.data() + offset, &v, 4);
    }

    const std::vector<uint8_t>& data() const { return buf_; }
    std::vector<uint8_t>&       data()       { return buf_; }

private:
    void append(const void* v, size_t n) {
        const auto* p = static_cast<const uint8_t*>(v);
        buf_.insert(buf_.end(), p, p + n);
    }

    std::vector<uint8_t> buf_;
};

} // namespace lu::assets
