#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <stdexcept>

namespace lu::assets {

// Lightweight binary reader for little-endian data with bounds checking.
// Does not own the data - caller must ensure the span outlives the reader.
class BinaryReader {
public:
    explicit BinaryReader(std::span<const uint8_t> data)
        : data_(data), pos_(0) {}

    size_t pos() const { return pos_; }
    size_t remaining() const { return data_.size() - pos_; }
    size_t size() const { return data_.size(); }
    bool eof() const { return pos_ >= data_.size(); }

    void seek(size_t pos) {
        if (pos > data_.size()) {
            throw std::out_of_range("BinaryReader::seek past end: " + std::to_string(pos));
        }
        pos_ = pos;
    }

    void skip(size_t n) {
        seek(pos_ + n);
    }

    void check_remaining(size_t n) const {
        if (pos_ + n > data_.size()) {
            throw std::out_of_range(
                "BinaryReader: need " + std::to_string(n) + " bytes at offset " +
                std::to_string(pos_) + ", but only " + std::to_string(remaining()) + " remain");
        }
    }

    uint8_t read_u8() {
        check_remaining(1);
        return data_[pos_++];
    }

    uint16_t read_u16() {
        check_remaining(2);
        uint16_t val;
        std::memcpy(&val, data_.data() + pos_, 2);
        pos_ += 2;
        return val;
    }

    int16_t read_s16() {
        check_remaining(2);
        int16_t val;
        std::memcpy(&val, data_.data() + pos_, 2);
        pos_ += 2;
        return val;
    }

    int32_t read_s32() {
        check_remaining(4);
        int32_t val;
        std::memcpy(&val, data_.data() + pos_, 4);
        pos_ += 4;
        return val;
    }

    uint32_t read_u32() {
        check_remaining(4);
        uint32_t val;
        std::memcpy(&val, data_.data() + pos_, 4);
        pos_ += 4;
        return val;
    }

    uint64_t read_u64() {
        check_remaining(8);
        uint64_t val;
        std::memcpy(&val, data_.data() + pos_, 8);
        pos_ += 8;
        return val;
    }

    float read_f32() {
        check_remaining(4);
        float val;
        std::memcpy(&val, data_.data() + pos_, 4);
        pos_ += 4;
        return val;
    }

    bool read_bool() {
        return read_u8() != 0;
    }

    // Read length-prefixed string (u8 length prefix)
    std::string read_string8() {
        uint8_t len = read_u8();
        check_remaining(len);
        std::string result(reinterpret_cast<const char*>(data_.data() + pos_), len);
        pos_ += len;
        return result;
    }

    // Read length-prefixed string (u32 length prefix)
    std::string read_string32() {
        uint32_t len = read_u32();
        if (len > 10000) return {}; // Sanity bound
        check_remaining(len);
        std::string result(reinterpret_cast<const char*>(data_.data() + pos_), len);
        pos_ += len;
        return result;
    }

    // Read length-prefixed wide string (u32 char count, UTF-16LE), convert to ASCII
    std::string read_wstring() {
        uint32_t char_count = read_u32();
        if (char_count > 10000) return {};
        check_remaining(char_count * 2);
        std::string result;
        result.reserve(char_count);
        for (uint32_t i = 0; i < char_count; ++i) {
            uint16_t wc;
            std::memcpy(&wc, data_.data() + pos_ + i * 2, 2);
            result += static_cast<char>(wc < 128 ? wc : '?');
        }
        pos_ += char_count * 2;
        return result;
    }

    // Read raw bytes
    std::span<const uint8_t> read_bytes(size_t n) {
        check_remaining(n);
        auto result = data_.subspan(pos_, n);
        pos_ += n;
        return result;
    }

    // Read at a specific absolute position without advancing pos
    uint32_t peek_u32_at(size_t offset) const {
        if (offset + 4 > data_.size()) {
            throw std::out_of_range("BinaryReader::peek_u32_at out of bounds");
        }
        uint32_t val;
        std::memcpy(&val, data_.data() + offset, 4);
        return val;
    }

    // Peek at bytes from the current position without advancing.
    // Returns a span of up to n bytes starting at the current position.
    std::span<const uint8_t> peek_bytes(size_t n) const {
        size_t avail = (pos_ < data_.size()) ? data_.size() - pos_ : 0;
        size_t count = (n < avail) ? n : avail;
        return data_.subspan(pos_, count);
    }

    // Get the underlying data
    std::span<const uint8_t> data() const { return data_; }

private:
    std::span<const uint8_t> data_;
    size_t pos_;
};

} // namespace lu::assets
