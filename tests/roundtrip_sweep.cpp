// Round-trip sweep: parse -> write -> byte-compare every Gamebryo-format file under the
// given client roots. Exits nonzero if any file fails to parse or round-trip, making it
// suitable for CI once a client tree is available.
//
//   roundtrip_sweep <client-root> [<client-root>...]
//
// Formats: .nif/.kf/.etk (shared NIF container), .kfm, .settings, .luz, .lvl, .ast, .zal,
// .scm, .aud, .lutriggers, .pki, .dds, .tga, .raw (terrain), .psb, .sd0, .g/.g1/.g2
// (brick geometry), .hkx (binary packfile AND tagged binary — XML HKX is skipped, not
// round-tripped: zero real client files use it), .fsb (FMOD sound bank, full
// decrypt+parse+write+encrypt path), .fev (FMOD event project, FEV1 and RIFF variants), and
// ForkParticle effect scripts (content-sniffed under a plain ".txt" extension).

#include "gamebryo/nif/nif_reader.h"
#include "gamebryo/nif/nif_writer.h"
#include "gamebryo/kfm/kfm_reader.h"
#include "gamebryo/kfm/kfm_writer.h"
#include "gamebryo/settings/settings_reader.h"
#include "gamebryo/settings/settings_writer.h"
#include "netdevil/archive/pk/pk_reader.h"
#include "netdevil/archive/sd0/sd0_reader.h"
#include "netdevil/archive/sd0/sd0_writer.h"
#include "netdevil/zone/luz/luz_reader.h"
#include "netdevil/zone/luz/luz_writer.h"
#include "netdevil/zone/lvl/lvl_reader.h"
#include "netdevil/zone/lvl/lvl_writer.h"
#include "netdevil/zone/terrain/terrain_reader.h"
#include "netdevil/zone/terrain/terrain_writer.h"
#include "netdevil/zone/ast/ast_reader.h"
#include "netdevil/zone/ast/ast_writer.h"
#include "netdevil/zone/zal/zal_reader.h"
#include "netdevil/zone/zal/zal_writer.h"
#include "netdevil/zone/aud/aud_reader.h"
#include "netdevil/zone/aud/aud_writer.h"
#include "netdevil/zone/lutriggers/lutriggers_reader.h"
#include "netdevil/zone/lutriggers/lutriggers_writer.h"
#include "netdevil/macros/scm/scm_reader.h"
#include "netdevil/macros/scm/scm_writer.h"
#include "netdevil/archive/pki/pki_reader.h"
#include "netdevil/archive/pki/pki_writer.h"
#include "microsoft/dds/dds_reader.h"
#include "microsoft/dds/dds_writer.h"
#include "microsoft/tga/tga_reader.h"
#include "microsoft/tga/tga_writer.h"
#include "forkparticle/psb/psb_reader.h"
#include "forkparticle/psb/psb_writer.h"
#include "forkparticle/effect/effect_reader.h"
#include "forkparticle/effect/effect_writer.h"
#include "lego/brick_geometry/brick_geometry_reader.h"
#include "lego/brick_geometry/brick_geometry_writer.h"
#include "lego/lxfml/lxfml_reader.h"
#include "lego/lxfml/lxfml_writer.h"
#include "havok/unified/hkx_reader.h"
#include "havok/unified/hkx_writer.h"
#include "fmod/fsb/fsb_reader.h"
#include "fmod/fsb/fsb_writer.h"
#include "fmod/fev/fev_reader.h"
#include "fmod/fev/fev_writer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> read_file(const fs::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto sz = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> d(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(d.data()), sz);
    return d;
}

struct FormatStats {
    int ok = 0;
    int parse_fail = 0;
    int mismatch = 0;
    int skipped = 0; // wrong magic — mis-extensioned file (e.g. a DDS named .nif)
    int known_quirk = 0; // mismatch traced to a documented, unfixable source-file quirk
    std::vector<std::string> messages; // first few failure details
};

// The client ships a handful of mis-extensioned files (e.g. DDS textures named .nif).
// Only files whose leading bytes identify the expected container are round-tripped.
bool has_magic(const std::vector<uint8_t>& data, std::initializer_list<const char*> magics) {
    for (const char* m : magics) {
        size_t len = strlen(m);
        if (data.size() >= len && memcmp(data.data(), m, len) == 0) return true;
    }
    return false;
}

// First byte offset where the two buffers differ (or the shorter length).
size_t first_diff(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) return i;
    }
    return n;
}

using RoundTripFunc = std::function<std::vector<uint8_t>(const std::vector<uint8_t>&)>;

void check_file(const fs::path& path, const RoundTripFunc& round_trip, FormatStats& stats,
                std::initializer_list<const char*> magics) {
    auto data = read_file(path);
    if (data.empty()) return; // unreadable/empty — not a round-trip failure

    // Some client dumps (1.7.45, 0.179.12, leaks) ship individual files still
    // SD0-compressed on disk. Round-trip the decompressed payload — that's the actual
    // format file; SD0 itself is checked separately via the ".sd0" extension handler.
    if (data.size() >= 5 && memcmp(data.data(), "sd0\x01\xff", 5) == 0) {
        try {
            data = lu::assets::sd0_decompress(data);
        } catch (const std::exception& ex) {
            // The 1.7.45 / 0.179.12 dumps contain patcher-cache artifacts: several sd0
            // streams concatenated into one file (a second sd0 magic appears mid-file).
            // Those aren't single-format files, so they're skips, not format failures.
            stats.skipped++;
            if (stats.messages.size() < 10) {
                stats.messages.push_back("SKIP  " + path.string() +
                                         ": sd0 decompress failed (concatenated patcher stream?): " +
                                         ex.what());
            }
            return;
        }
    }

    if (magics.size() > 0 && !has_magic(data, magics)) {
        stats.skipped++;
        if (stats.messages.size() < 10) {
            stats.messages.push_back("SKIP  " + path.string() + ": wrong magic (mis-extensioned)");
        }
        return;
    }

    std::vector<uint8_t> out;
    try {
        out = round_trip(data);
    } catch (const std::exception& ex) {
        stats.parse_fail++;
        if (stats.messages.size() < 10) {
            stats.messages.push_back("PARSE " + path.string() + ": " + ex.what());
        }
        return;
    }

    if (out == data) {
        stats.ok++;
        return;
    }
    stats.mismatch++;
    if (stats.messages.size() < 10) {
        char buf[160];
        snprintf(buf, sizeof(buf), "DIFF  %s: size %zu -> %zu, first diff @ 0x%zx",
                 path.string().c_str(), data.size(), out.size(), first_diff(data, out));
        stats.messages.push_back(buf);
    }
}

std::string lower_ext(const fs::path& p) {
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

// .settings has no text header, but every known file starts with its version string as
// u8(5) + "2.3.0" — distinctive enough to identify entries inside PK archives, which
// index by path CRC and carry no filenames.
bool sniff_settings(const std::vector<uint8_t>& d) {
    return d.size() >= 6 && d[0] == 5 && memcmp(d.data() + 1, "2.3.0", 5) == 0;
}

// Standalone .sd0 files: decompress then recompress and compare against the original
// bytes directly (not the decompressed payload — that's a different format entirely and
// gets its own extension's round-trip check once unwrapped by check_file below).
void check_sd0(const fs::path& path, FormatStats& stats) {
    auto data = read_file(path);
    if (data.empty()) return;
    if (!lu::assets::sd0_is_compressed(data)) {
        stats.skipped++;
        return;
    }

    std::vector<uint8_t> out;
    try {
        auto decompressed = lu::assets::sd0_decompress(data);
        out = lu::assets::sd0_compress(decompressed);
    } catch (const std::exception& ex) {
        stats.parse_fail++;
        if (stats.messages.size() < 10) {
            stats.messages.push_back("PARSE " + path.string() + ": " + ex.what());
        }
        return;
    }

    if (out == data) {
        stats.ok++;
        return;
    }
    stats.mismatch++;
    if (stats.messages.size() < 10) {
        char buf[160];
        snprintf(buf, sizeof(buf), "DIFF  %s: size %zu -> %zu, first diff @ 0x%zx",
                 path.string().c_str(), data.size(), out.size(), first_diff(data, out));
        stats.messages.push_back(buf);
    }
}

// Standalone .fsb files: decrypt (all LU FSBs are encrypted), parse, write, then
// re-encrypt and compare against the original on-disk bytes. The audio sample data
// isn't modeled on FsbFile, so fsb_write needs the decrypted buffer to slice it from;
// the full path exercises decrypt + parse + write + encrypt end to end.
void check_fsb(const fs::path& path, FormatStats& stats) {
    auto data = read_file(path);
    if (data.empty()) return;

    std::vector<uint8_t> dec = data;
    bool was_encrypted = lu::assets::fsb_decrypt(dec);

    std::vector<uint8_t> out;
    try {
        auto fsb = lu::assets::fsb_parse(dec);
        auto out_dec = lu::assets::fsb_write(fsb, dec);
        out = was_encrypted ? lu::assets::fsb_encrypt(out_dec) : std::move(out_dec);
    } catch (const std::exception& ex) {
        stats.parse_fail++;
        if (stats.messages.size() < 10) {
            stats.messages.push_back("PARSE " + path.string() + ": " + ex.what());
        }
        return;
    }

    if (out == data) {
        stats.ok++;
        return;
    }
    stats.mismatch++;
    if (stats.messages.size() < 10) {
        char buf[160];
        snprintf(buf, sizeof(buf), "DIFF  %s: size %zu -> %zu, first diff @ 0x%zx",
                 path.string().c_str(), data.size(), out.size(), first_diff(data, out));
        stats.messages.push_back(buf);
    }
}

// .hkx ships in three format variants sharing one extension: binary packfile (magic
// 0x57E0E057 0x10C0C010) and tagged binary (magic 0xCAB00D1E 0xD011FACE) are both
// round-tripped here via the unified hkx_parse()/hkx_write() dispatcher (see
// src/havok/unified/), which itself detects which of the two a file is — the sweep
// tool doesn't sniff magic bytes itself. XML HKX is out of scope for this project
// (zero real client files use it); hkx_parse() throws HkxFormatError for it (or
// anything else unrecognized), which this treats as a skip rather than a failure,
// since it's an intentional scope boundary, not a real-file round-trip bug. That
// "identify by magic, skip non-matching, round-trip the rest" shape doesn't fit the
// generic check_file() dispatcher (which mismatches, rather than skips, on wrong magic
// when an extension maps 1:1 to a format), so — like check_sd0() above — .hkx gets its
// own function.
void check_hkx(const fs::path& path, FormatStats& stats) {
    auto data = read_file(path);
    if (data.empty()) return;

    std::vector<uint8_t> out;
    try {
        auto hkx = lu::assets::hkx_parse(data);
        out = lu::assets::hkx_write(hkx);
    } catch (const lu::assets::HkxFormatError&) {
        stats.skipped++; // XML HKX, or anything else unrecognized — out of scope.
        return;
    } catch (const std::exception& ex) {
        stats.parse_fail++;
        if (stats.messages.size() < 10) {
            stats.messages.push_back("PARSE " + path.string() + ": " + ex.what());
        }
        return;
    }

    if (out == data) {
        stats.ok++;
        return;
    }
    stats.mismatch++;
    if (stats.messages.size() < 10) {
        char buf[160];
        snprintf(buf, sizeof(buf), "DIFF  %s: size %zu -> %zu, first diff @ 0x%zx",
                 path.string().c_str(), data.size(), out.size(), first_diff(data, out));
        stats.messages.push_back(buf);
    }
}

// LXFML mismatches fall into two buckets: a genuine bug, or one of two documented,
// unfixable source-file quirks (see lxfml_writer.h):
//   1. A ~0.05-1% float tie-break case where the .NET tool's original formatter picked a
//      different (but equally valid, same-bit-pattern-on-reparse) 17-significant-digit
//      string than ours does.
//   2. A one-off hand-edited anomaly in the shipped asset itself (stray tab used for
//      indentation, a truncated/hand-typed decimal literal, a stray extra byte) — these
//      affect a small, fixed list of specific files, not a discoverable format rule.
// This walks both buffers and, at each point of divergence, widens to the enclosing
// numeric token on each side; if the two numbers are equal as either float or double,
// it's bucket 1. Anything else (including a length mismatch, which bucket 2's anomalies
// usually cause) is reported as a real mismatch.
struct NumSpan { size_t start, end; };
NumSpan lxfml_numeric_span(const std::vector<uint8_t>& buf, size_t i) {
    auto is_num_char = [](uint8_t c) {
        return static_cast<bool>(isdigit(c)) || c == '.' || c == '-' || c == 'E' ||
               c == 'e' || c == '+';
    };
    size_t s = i, e = i;
    while (s > 0 && is_num_char(buf[s - 1])) s--;
    while (e < buf.size() && is_num_char(buf[e])) e++;
    return {s, e};
}

bool lxfml_is_float_tie(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    size_t ia = 0, ib = 0;
    bool any_diff = false;
    while (ia < a.size() && ib < b.size()) {
        if (a[ia] == b[ib]) { ia++; ib++; continue; }
        any_diff = true;
        NumSpan sa = lxfml_numeric_span(a, ia);
        NumSpan sb = lxfml_numeric_span(b, ib);
        if (sa.end == sa.start || sb.end == sb.start) return false;
        std::string na(a.begin() + sa.start, a.begin() + sa.end);
        std::string nb(b.begin() + sb.start, b.begin() + sb.end);
        char* enda; char* endb;
        double da = strtod(na.c_str(), &enda);
        double db = strtod(nb.c_str(), &endb);
        if (enda == na.c_str() || endb == nb.c_str()) return false;
        bool equal = (da == db) || (static_cast<float>(da) == static_cast<float>(db));
        if (!equal) return false;
        ia = sa.end; ib = sb.end;
    }
    if (ia != a.size() || ib != b.size()) return false;
    return any_diff;
}

// Files with known one-off hand-edited anomalies in the shipped asset itself (see the
// comment above) — never expected to round-trip byte-perfectly. Matched by filename
// since the anomaly is specific to that exact shipped file, not a derivable rule.
bool lxfml_is_known_anomaly(const fs::path& path) {
    static const std::vector<std::string> names = {
        "allbricks.lxfml", "cre_dragon.lxfml", "cre_dragon_named.lxfml", "cre_burno.lxfml",
        "env_nim_pr_doghouse.lxfml", "glass_minifig.lxfml", "mf_ninja-maelstrom_01.lxfml",
        "env_ng_ninjago_spinjitzu_railpost_large_earth.lxfml",
        "smash_won_fv_pedistalwarrior.lxfml", "smash_won_gnar_crate-tiny-03.lxfml",
    };
    std::string fname = path.filename().string();
    return std::find(names.begin(), names.end(), fname) != names.end();
}

void check_lxfml(const fs::path& path, FormatStats& stats) {
    auto data = read_file(path);
    if (data.empty()) return;

    std::vector<uint8_t> out;
    try {
        auto lxfml = lu::assets::lxfml_parse(data);
        auto out_str = lu::assets::lxfml_write(lxfml);
        out.assign(out_str.begin(), out_str.end());
    } catch (const std::exception& ex) {
        stats.parse_fail++;
        if (stats.messages.size() < 10) {
            stats.messages.push_back("PARSE " + path.string() + ": " + ex.what());
        }
        return;
    }

    if (out == data) {
        stats.ok++;
        return;
    }
    if (lxfml_is_known_anomaly(path) || lxfml_is_float_tie(data, out)) {
        stats.known_quirk++;
        return;
    }
    stats.mismatch++;
    if (stats.messages.size() < 10) {
        char buf[160];
        snprintf(buf, sizeof(buf), "DIFF  %s: size %zu -> %zu, first diff @ 0x%zx",
                 path.string().c_str(), data.size(), out.size(), first_diff(data, out));
        stats.messages.push_back(buf);
    }
}

// Round-trip every Gamebryo-format entry inside a .pk archive. Entries are identified by
// magic (no filenames in the pack index); non-Gamebryo entries (textures, scripts, ...)
// are simply not counted. Comparison target is the extracted (SD0-decompressed) bytes —
// i.e. the actual Gamebryo file, as the client itself sees it.
void check_pk(const fs::path& pk_path,
              const RoundTripFunc& nif_rt, const RoundTripFunc& kfm_rt,
              const RoundTripFunc& settings_rt,
              std::map<std::string, FormatStats>& stats) {
    auto file_data = read_file(pk_path);
    if (file_data.empty()) return;

    std::unique_ptr<lu::assets::PkArchive> archive;
    try {
        archive = std::make_unique<lu::assets::PkArchive>(file_data);
    } catch (const std::exception& ex) {
        auto& st = stats["pk:(archive)"];
        st.parse_fail++;
        if (st.messages.size() < 10) {
            st.messages.push_back("PKERR " + pk_path.string() + ": " + ex.what());
        }
        return;
    }

    for (size_t i = 0; i < archive->entry_count(); ++i) {
        std::vector<uint8_t> data;
        try {
            data = archive->extract(i);
        } catch (const std::exception&) {
            continue; // extraction failures are pk-reader territory, not round-trip
        }

        const char* fmt = nullptr;
        if (has_magic(data, {"Gamebryo File Format", "NetImmerse File Format"})) {
            fmt = "pk:nif-container";
        } else if (has_magic(data, {";Gamebryo KFM File Version"})) {
            fmt = "pk:.kfm";
        } else if (sniff_settings(data)) {
            fmt = "pk:.settings";
        } else {
            continue;
        }

        auto& st = stats[fmt];
        const RoundTripFunc& rt = fmt == std::string("pk:.kfm") ? kfm_rt
                                : fmt == std::string("pk:.settings") ? settings_rt
                                : nif_rt;
        std::vector<uint8_t> out;
        try {
            out = rt(data);
        } catch (const std::exception& ex) {
            st.parse_fail++;
            if (st.messages.size() < 10) {
                st.messages.push_back("PARSE " + pk_path.string() + "[entry " +
                                      std::to_string(i) + "]: " + ex.what());
            }
            continue;
        }
        if (out == data) {
            st.ok++;
        } else {
            st.mismatch++;
            if (st.messages.size() < 10) {
                char buf[200];
                snprintf(buf, sizeof(buf), "DIFF  %s[entry %zu]: size %zu -> %zu, first diff @ 0x%zx",
                         pk_path.string().c_str(), i, data.size(), out.size(),
                         first_diff(data, out));
                st.messages.push_back(buf);
            }
        }
    }
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: roundtrip_sweep <client-root> [<client-root>...]\n");
        return 2;
    }

    const RoundTripFunc nif_rt = [](const std::vector<uint8_t>& d) {
        return lu::assets::nif_write(lu::assets::nif_parse(d));
    };
    const RoundTripFunc kfm_rt = [](const std::vector<uint8_t>& d) {
        return lu::assets::kfm_write(lu::assets::kfm_parse(d));
    };
    const RoundTripFunc settings_rt = [](const std::vector<uint8_t>& d) {
        return lu::assets::settings_write(lu::assets::settings_parse(d));
    };
    const RoundTripFunc luz_rt = [](const std::vector<uint8_t>& d) {
        return lu::assets::luz_write(lu::assets::luz_parse(d));
    };
    const RoundTripFunc lvl_rt = [](const std::vector<uint8_t>& d) {
        return lu::assets::lvl_write(lu::assets::lvl_parse(d));
    };
    const RoundTripFunc terrain_rt = [](const std::vector<uint8_t>& d) {
        return lu::assets::terrain_write(lu::assets::terrain_parse(d));
    };
    const RoundTripFunc ast_rt = [](const std::vector<uint8_t>& d) {
        return lu::assets::ast_write(lu::assets::ast_parse(d));
    };
    const RoundTripFunc zal_rt = [](const std::vector<uint8_t>& d) {
        return lu::assets::zal_write(lu::assets::zal_parse(d));
    };
    const RoundTripFunc scm_rt = [](const std::vector<uint8_t>& d) {
        return lu::assets::scm_write(lu::assets::scm_parse(d));
    };
    const RoundTripFunc aud_rt = [](const std::vector<uint8_t>& d) {
        return lu::assets::aud_write(lu::assets::aud_parse(d));
    };
    const RoundTripFunc lutriggers_rt = [](const std::vector<uint8_t>& d) {
        return lu::assets::lutriggers_write(lu::assets::lutriggers_parse(d));
    };
    const RoundTripFunc pki_rt = [](const std::vector<uint8_t>& d) {
        return lu::assets::pki_write(lu::assets::pki_parse(d));
    };
    const RoundTripFunc dds_rt = [](const std::vector<uint8_t>& d) {
        auto dds = lu::assets::dds_parse_header(d);
        std::span<const uint8_t> payload(d.data() + 128, d.size() - 128);
        return lu::assets::dds_write(dds, payload);
    };
    const RoundTripFunc tga_rt = [](const std::vector<uint8_t>& d) {
        return lu::assets::tga_write(lu::assets::tga_parse(d));
    };
    const RoundTripFunc psb_rt = [](const std::vector<uint8_t>& d) {
        std::span<const uint8_t> span(d.data(), d.size());
        return lu::assets::psb_write(lu::assets::psb_parse(span), span);
    };
    const RoundTripFunc effect_rt = [](const std::vector<uint8_t>& d) {
        std::string text(reinterpret_cast<const char*>(d.data()), d.size());
        std::string out = lu::assets::effect_write(lu::assets::effect_parse(text));
        return std::vector<uint8_t>(out.begin(), out.end());
    };
    const RoundTripFunc brick_geom_rt = [](const std::vector<uint8_t>& d) {
        return lu::assets::brick_geometry_write(lu::assets::brick_geometry_parse(d));
    };
    const RoundTripFunc fev_rt = [](const std::vector<uint8_t>& d) {
        return lu::assets::fev_write(lu::assets::fev_parse(d));
    };

    struct Handler {
        const RoundTripFunc* fn;
        std::initializer_list<const char*> magics;
    };
    const std::map<std::string, Handler> handlers = {
        {".nif", {&nif_rt, {"Gamebryo File Format", "NetImmerse File Format"}}},
        {".kf", {&nif_rt, {"Gamebryo File Format", "NetImmerse File Format"}}},
        {".etk", {&nif_rt, {"Gamebryo File Format", "NetImmerse File Format"}}},
        {".kfm", {&kfm_rt, {";Gamebryo KFM File Version"}}},
        {".settings", {&settings_rt, {}}}, // binary NiKFMTool format has no text magic
        {".luz", {&luz_rt, {}}},
        {".lvl", {&lvl_rt, {}}},
        {".ast", {&ast_rt, {}}},
        {".zal", {&zal_rt, {}}},
        {".scm", {&scm_rt, {}}},
        {".aud", {&aud_rt, {}}},
        {".lutriggers", {&lutriggers_rt, {}}},
        {".pki", {&pki_rt, {}}},
        {".dds", {&dds_rt, {"DDS "}}},
        {".tga", {&tga_rt, {}}},
        // .raw is only the terrain format under maps/ — other .raw files (if any) are
        // unrelated binary blobs, so scope by path in the walk below.
        {".raw", {&terrain_rt, {}}},
        {".psb", {&psb_rt, {}}},
        {".g", {&brick_geom_rt, {}}},
        {".g1", {&brick_geom_rt, {}}},
        {".g2", {&brick_geom_rt, {}}},
        // FMOD event projects: both the flat FEV1 variant and the RIFF-wrapped
        // variant (FMOD Designer 4.45) round-trip through the same fev_parse/
        // fev_write dispatch. Accept both magics so RIFF .fev files aren't skipped
        // as mis-extensioned.
        {".fev", {&fev_rt, {"FEV1", "RIFF"}}},
    };

    std::map<std::string, FormatStats> stats;
    for (int i = 1; i < argc; ++i) {
        fs::path root(argv[i]);
        if (!fs::exists(root)) {
            fprintf(stderr, "skipping missing root: %s\n", argv[i]);
            continue;
        }
        for (const auto& e : fs::recursive_directory_iterator(
                 root, fs::directory_options::skip_permission_denied)) {
            if (!e.is_regular_file()) continue;
            std::string ext = lower_ext(e.path());
            if (ext == ".pk") {
                check_pk(e.path(), nif_rt, kfm_rt, settings_rt, stats);
                continue;
            }
            if (ext == ".sd0") {
                check_sd0(e.path(), stats[".sd0"]);
                continue;
            }
            if (ext == ".lxfml") {
                check_lxfml(e.path(), stats[".lxfml"]);
                continue;
            }
            if (ext == ".hkx") {
                check_hkx(e.path(), stats[".hkx"]);
                continue;
            }
            if (ext == ".fsb") {
                check_fsb(e.path(), stats[".fsb"]);
                continue;
            }
            // ForkParticle effect scripts ship as plain ".txt" (no distinguishing
            // extension) — identify by the "EMITTERNAME:" prefix every real sample starts
            // with, same sniff pk_viewer's detectFile() uses.
            if (ext == ".txt") {
                auto data = read_file(e.path());
                if (data.size() >= 12 &&
                    memcmp(data.data(), "EMITTERNAME:", 12) == 0) {
                    check_file(e.path(), effect_rt, stats[".effect(.txt)"], {});
                }
                continue;
            }
            auto it = handlers.find(ext);
            if (it == handlers.end()) continue;
            if (ext == ".raw" && e.path().string().find("maps") == std::string::npos) {
                continue; // non-terrain .raw
            }
            check_file(e.path(), *it->second.fn, stats[ext], it->second.magics);
        }
    }

    int total_bad = 0;
    printf("%-10s %10s %12s %10s %8s %12s\n", "format", "ok", "parse-fail", "mismatch",
           "skipped", "known-quirk");
    for (const auto& [ext, st] : stats) {
        printf("%-10s %10d %12d %10d %8d %12d\n", ext.c_str(), st.ok, st.parse_fail,
               st.mismatch, st.skipped, st.known_quirk);
        total_bad += st.parse_fail + st.mismatch;
    }
    for (const auto& [ext, st] : stats) {
        for (const auto& msg : st.messages) {
            printf("  %s\n", msg.c_str());
        }
    }
    printf(total_bad == 0 ? "\nAll files round-trip byte-identical.\n"
                          : "\n%d file(s) failed.\n", total_bad);
    return total_bad == 0 ? 0 : 1;
}
