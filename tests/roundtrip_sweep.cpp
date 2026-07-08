// Round-trip sweep: parse -> write -> byte-compare every Gamebryo-format file under the
// given client roots. Exits nonzero if any file fails to parse or round-trip, making it
// suitable for CI once a client tree is available.
//
//   roundtrip_sweep <client-root> [<client-root>...]
//
// Formats: .nif/.kf/.etk (shared NIF container), .kfm, .settings.

#include "gamebryo/nif/nif_reader.h"
#include "gamebryo/nif/nif_writer.h"
#include "gamebryo/kfm/kfm_reader.h"
#include "gamebryo/kfm/kfm_writer.h"
#include "gamebryo/settings/settings_reader.h"
#include "gamebryo/settings/settings_writer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
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
            auto it = handlers.find(ext);
            if (it == handlers.end()) continue;
            check_file(e.path(), *it->second.fn, stats[ext], it->second.magics);
        }
    }

    int total_bad = 0;
    printf("%-10s %10s %12s %10s %8s\n", "format", "ok", "parse-fail", "mismatch", "skipped");
    for (const auto& [ext, st] : stats) {
        printf("%-10s %10d %12d %10d %8d\n", ext.c_str(), st.ok, st.parse_fail, st.mismatch,
               st.skipped);
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
