#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace lu::assets {

// A single line of a text-format file, preserved verbatim: `text` is the line content
// without its terminator, `terminator` is exactly what followed it in the file ("\r\n",
// "\n", or "" for a final line with no trailing newline). join_lines(split_lines(x)) == x
// for any input, which is what gives text formats (.ast, .zal, .scm, ...) byte-identical
// round-trips while still letting parsers derive structured views from `text`.
struct TextLine {
    std::string text;
    std::string terminator;
};

inline std::vector<TextLine> split_lines(std::span<const uint8_t> data) {
    std::vector<TextLine> lines;
    size_t start = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        if (data[i] != '\n') continue;
        size_t text_end = i;
        const char* term = "\n";
        if (text_end > start && data[text_end - 1] == '\r') {
            text_end--;
            term = "\r\n";
        }
        lines.push_back({std::string(reinterpret_cast<const char*>(data.data()) + start,
                                     text_end - start),
                         term});
        start = i + 1;
    }
    if (start < data.size()) { // final line without a trailing newline
        lines.push_back({std::string(reinterpret_cast<const char*>(data.data()) + start,
                                     data.size() - start),
                         ""});
    }
    return lines;
}

inline std::vector<uint8_t> join_lines(const std::vector<TextLine>& lines) {
    std::vector<uint8_t> out;
    for (const TextLine& line : lines) {
        out.insert(out.end(), line.text.begin(), line.text.end());
        out.insert(out.end(), line.terminator.begin(), line.terminator.end());
    }
    return out;
}

} // namespace lu::assets
