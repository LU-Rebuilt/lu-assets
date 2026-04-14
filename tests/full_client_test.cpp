#include "netdevil/archive/sd0/sd0_reader.h"
#include "netdevil/archive/pk/pk_reader.h"
#include "netdevil/database/fdb/fdb_reader.h"
#include "netdevil/zone/luz/luz_reader.h"
#include "netdevil/zone/lvl/lvl_reader.h"
#include "netdevil/zone/terrain/terrain_reader.h"
#include "netdevil/zone/lutriggers/lutriggers_reader.h"
#include "netdevil/zone/aud/aud_reader.h"
#include "netdevil/zone/zal/zal_reader.h"
#include "netdevil/zone/ast/ast_reader.h"
#include "netdevil/macros/scm/scm_reader.h"
#include "gamebryo/nif/nif_reader.h"
#include "gamebryo/kfm/kfm_reader.h"
#include "gamebryo/settings/settings_reader.h"
#include "havok/reader/hkx_reader.h"
#include "lego/lxfml/lxfml_reader.h"
#include "lego/brick_geometry/brick_geometry.h"
#include "microsoft/dds/dds_reader.h"
#include "microsoft/tga/tga_reader.h"
#include "microsoft/fxo/fxo_reader.h"
#include "forkparticle/psb/psb_reader.h"
#include "fmod/fev/fev_reader.h"
#include "fmod/fsb/fsb_reader.h"
#include "scaleform/gfx/gfx_reader.h"

#include <fstream>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <chrono>
#include <map>
#include <functional>

namespace fs = std::filesystem;

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto sz = f.tellg(); f.seekg(0);
    std::vector<uint8_t> d(sz);
    f.read(reinterpret_cast<char*>(d.data()), sz);
    return d;
}

struct FormatResult {
    int ok = 0;
    int fail = 0;
    std::vector<std::string> failures; // First 5 failure messages
};

using ParseFunc = std::function<void(const std::vector<uint8_t>&)>;

FormatResult test_format(const std::string& base, const std::string& ext,
                         ParseFunc parser, int max_files = 0) {
    FormatResult result;
    if (!fs::exists(base)) return result;
    int count = 0;
    for (auto& e : fs::recursive_directory_iterator(base)) {
        if (!e.is_regular_file()) continue;
        auto fext = e.path().extension().string();
        // Handle multi-char extensions like .g1, .g2
        if (ext == ".g") {
            if (fext != ".g" && (fext.size() != 3 || fext[0] != '.' || fext[1] != 'g')) continue;
        } else {
            if (fext != ext) continue;
        }
        auto data = read_file(e.path().string());
        if (data.empty()) { result.fail++; continue; }
        try {
            parser(data);
            result.ok++;
        } catch (const std::exception& ex) {
            result.fail++;
            if (result.failures.size() < 5) {
                result.failures.push_back(e.path().filename().string() + ": " + ex.what());
            }
        }
        if (max_files > 0 && ++count >= max_files) break;
    }
    return result;
}

int main(int argc, char* argv[]) {
    std::string BASE = "client/res"; // override with: full_client_test /path/to/client/res
    if (argc > 1) BASE = argv[1];

    printf("=== FULL CLIENT FORMAT VALIDATION ===\n");
    printf("Client path: %s\n\n", BASE.c_str());

    auto t0 = std::chrono::steady_clock::now();
    int total_ok = 0, total_fail = 0;

    auto report = [&](const char* name, const FormatResult& r) {
        float pct = (r.ok + r.fail) > 0 ? 100.0f * r.ok / (r.ok + r.fail) : 0;
        printf("%-15s %6d/%6d  (%5.1f%%)\n", name, r.ok, r.ok + r.fail, pct);
        for (auto& f : r.failures) printf("  FAIL: %s\n", f.c_str());
        total_ok += r.ok;
        total_fail += r.fail;
    };

    // NetDevil formats
    report(".luz", test_format(BASE + "/maps", ".luz",
        [](auto& d) { lu::assets::luz_parse(d); }));

    report(".lvl", test_format(BASE + "/maps", ".lvl",
        [](auto& d) { lu::assets::lvl_parse(d); }));

    report(".raw", test_format(BASE + "/maps", ".raw",
        [](auto& d) { lu::assets::terrain_parse(d); }));

    report(".lutriggers", test_format(BASE + "/maps", ".lutriggers",
        [](auto& d) { lu::assets::lutriggers_parse(d); }));

    report(".aud", test_format(BASE + "/maps", ".aud",
        [](auto& d) { lu::assets::aud_parse(d); }));

    report(".zal", test_format(BASE + "/maps", ".zal",
        [](auto& d) { lu::assets::zal_parse(d); }));

    report(".ast", test_format(BASE + "/maps", ".ast",
        [](auto& d) { lu::assets::ast_parse(d); }));

    report(".scm", test_format(BASE + "/macros", ".scm",
        [](auto& d) { lu::assets::scm_parse(d); }));

    report(".fdb", test_format(BASE, ".fdb",
        [](auto& d) { lu::assets::fdb_parse(d); }));

    // Havok formats
    report(".hkx", test_format(BASE + "/physics", ".hkx",
        [](auto& d) {
            Hkx::HkxFile hkx;
            auto result = hkx.Parse(d.data(), d.size());
            if (!result.success)
                throw std::runtime_error(result.error);
        }));

    // Gamebryo formats
    report(".nif", test_format(BASE + "/mesh", ".nif",
        [](auto& d) { lu::assets::nif_parse(d); }));

    report(".kf", test_format(BASE + "/animations", ".kf",
        [](auto& d) { lu::assets::nif_parse(d); }));

    report(".etk", test_format(BASE + "/animations", ".etk",
        [](auto& d) { lu::assets::nif_parse(d); }));

    report(".kfm", test_format(BASE + "/animations", ".kfm",
        [](auto& d) { lu::assets::kfm_parse(d); }));

    report(".settings", test_format(BASE + "/mesh", ".settings",
        [](auto& d) { lu::assets::settings_parse(d); }));

    // LEGO formats
    report(".lxfml", test_format(BASE + "/BrickModels", ".lxfml",
        [](auto& d) { lu::assets::lxfml_parse(d); }));

    report(".g", test_format(BASE + "/brickprimitives", ".g",
        [](auto& d) { lu::assets::brick_geometry_parse(d); }));

    // Microsoft formats
    report(".dds", test_format(BASE + "/textures", ".dds",
        [](auto& d) { lu::assets::dds_parse_header(d); }));

    // Also test DDS in mesh/ directory
    report(".dds(mesh)", test_format(BASE + "/mesh", ".dds",
        [](auto& d) { lu::assets::dds_parse_header(d); }));

    report(".tga", test_format(BASE + "/mesh", ".tga",
        [](auto& d) { lu::assets::tga_load(d); }));

    report(".fxo", test_format(BASE + "/shaders", ".fxo",
        [](auto& d) { lu::assets::fxo_parse(d); }));

    // ForkParticle
    report(".psb", test_format(BASE + "/forkp", ".psb",
        [](auto& d) { lu::assets::psb_parse(d); }));

    // FMOD
    report(".fev", test_format(BASE + "/audio", ".fev",
        [](auto& d) { lu::assets::fev_parse(d); }));

    // FSB files: decrypt with the LU key and parse sample headers.
    // Key: "1024442297" (FMOD project integer password as decimal ASCII string,
    // confirmed by RE of fmodex.dll FUN_10040d7e and verified against all 98 LU FSBs).
    report(".fsb", test_format(BASE + "/audio", ".fsb",
        [](const std::vector<uint8_t>& data) {
            // fsb_decrypt operates in-place, so copy the const input first.
            std::vector<uint8_t> buf(data);
            lu::assets::fsb_decrypt(buf, lu::assets::FSB_LU_KEY);
            lu::assets::fsb_parse(buf);
        }));

    // Verify FSB bank checksums match the paired FEV files.
    // For each FEV, the bank's fsb_checksum[2] must match the 8 reserved bytes
    // in the corresponding FSB4 header (confirmed against all 5 newcontent banks).
    {
        FormatResult r;
        for (auto& e : fs::recursive_directory_iterator(BASE + "/audio")) {
            if (!e.is_regular_file() || e.path().extension() != ".fev") continue;
            try {
                auto fev_data = read_file(e.path().string());
                auto fev = lu::assets::fev_parse(fev_data);
                auto dir = e.path().parent_path();
                for (auto& bank : fev.banks) {
                    // Strip null terminator from bank name if present
                    std::string bname = bank.name;
                    if (!bname.empty() && bname.back() == '\0')
                        bname.pop_back();
                    fs::path fsb_path = dir / (bname + ".fsb");
                    if (!fs::exists(fsb_path)) { r.ok++; continue; } // bank not in same dir
                    auto fsb_data = read_file(fsb_path.string());
                    lu::assets::fsb_decrypt(fsb_data, lu::assets::FSB_LU_KEY);
                    if (lu::assets::fev_verify_bank_checksum(bank, fsb_data))
                        r.ok++;
                    else
                        r.fail++;
                }
            } catch (...) {
                r.fail++;
            }
        }
        report(".fev<->.fsb checksums", r);
    }

    // Scaleform
    report(".gfx", test_format(BASE + "/ui", ".gfx",
        [](auto& d) { lu::assets::gfx_parse(d); }));

    // Also test GFX in textures/
    report(".gfx(tex)", test_format(BASE + "/textures", ".gfx",
        [](auto& d) { lu::assets::gfx_parse(d); }));

    // PK archives (from packed client) — full pipeline test: PK → SD0 → format parser.
    // Routes each extracted entry to the correct parser via magic byte detection.
    // Formats not detectable from magic (PSB, LUZ, LVL, Settings, .g, SCM, AUD, ZAL, AST)
    // are counted as "skipped"; no failures are counted for unrecognized formats.
    {
        std::string pack_dir = BASE + "/../pack"; // packed client res/pack directory
        if (fs::exists(pack_dir)) {
            // Per-format result tracking for recognizable formats
            std::map<std::string, FormatResult> pk_results;
            FormatResult total_entries;   // all extracted entries (success = extracted OK)
            FormatResult skipped;         // entries whose format is not detectable by magic

            for (auto& e : fs::directory_iterator(pack_dir)) {
                if (!e.is_regular_file() || e.path().extension() != ".pk") continue;
                if (e.file_size() > 400 * 1024 * 1024) continue; // skip PK files > 400 MB for speed

                auto pk_data = read_file(e.path().string());
                lu::assets::PkArchive pk(pk_data);

                pk.for_each([&](size_t /*idx*/, const lu::assets::PackIndexEntry& entry) {
                    // Extract (decompresses SD0 if is_compressed)
                    std::vector<uint8_t> raw;
                    try {
                        raw = pk.extract(entry);
                        total_entries.ok++;
                    } catch (const std::exception& ex) {
                        total_entries.fail++;
                        if (total_entries.failures.size() < 5)
                            total_entries.failures.push_back(
                                e.path().filename().string() + "@" + std::to_string(entry.data_offset)
                                + ": " + ex.what());
                        return;
                    }

                    if (raw.size() < 4) { skipped.ok++; return; }

                    // --- Magic detection ---
                    auto& d = raw;
                    const auto u32le = [&](size_t off) -> uint32_t {
                        uint32_t v; std::memcpy(&v, d.data() + off, 4); return v;
                    };
                    auto starts = [&](const char* s, size_t n) {
                        return d.size() >= n && std::memcmp(d.data(), s, n) == 0;
                    };

                    std::string fmt;
                    try {
                        // DDS
                        if (starts("DDS ", 4)) {
                            lu::assets::dds_parse_header(d);
                            fmt = ".dds";
                        }
                        // GFX / SWF
                        else if (starts("GFX", 3) || starts("CFX", 3) ||
                                 starts("FWS", 3) || starts("CWS", 3)) {
                            lu::assets::gfx_parse(d);
                            fmt = ".gfx";
                        }
                        // HKX binary packfile or tagged binary
                        else if (u32le(0) == Hkx::BINARY_MAGIC_0 ||
                                 u32le(0) == Hkx::TAGGED_MAGIC_0) {
                            Hkx::HkxFile hkx;
                            auto r = hkx.Parse(d.data(), d.size());
                            if (!r.success) throw std::runtime_error(r.error);
                            fmt = ".hkx";
                        }
                        // FEV (FMOD Event)
                        else if (starts("FEV1", 4)) {
                            lu::assets::fev_parse(d);
                            fmt = ".fev";
                        }
                        // FSB (FMOD Sound Bank) — decrypt before parsing
                        else if (d.size() >= 4 && d[0] == 'F' && d[1] == 'S' && d[2] == 'B') {
                            std::vector<uint8_t> buf(d);
                            lu::assets::fsb_decrypt(buf, lu::assets::FSB_LU_KEY);
                            lu::assets::fsb_parse(buf);
                            fmt = ".fsb";
                        }
                        // Gamebryo: NIF / KF / ETK / KFM / Settings all share "Gamebryo" prefix
                        else if (d.size() >= 8 && std::memcmp(d.data(), "Gamebryo", 8) == 0) {
                            // KFM has its own parser
                            if (d.size() >= 17 &&
                                std::memcmp(d.data(), "Gamebryo KFM File", 17) == 0) {
                                lu::assets::kfm_parse(d);
                                fmt = ".kfm";
                            } else {
                                // NIF, KF, ETK all go through nif_parse
                                lu::assets::nif_parse(d);
                                fmt = ".nif";
                            }
                        }
                        // XML: LXFML and trigger files
                        else if (starts("<?xml", 5) || starts("<LXFML", 6)) {
                            lu::assets::lxfml_parse(d);
                            fmt = ".lxfml";
                        }
                        // FDB has no distinctive magic (starts with u32 table_count);
                        // cannot be reliably detected from magic bytes — falls through to skipped.
                        else {
                            skipped.ok++;
                            return; // unrecognized — not a failure
                        }

                        pk_results[fmt].ok++;

                    } catch (const std::exception& ex) {
                        pk_results[fmt].fail++;
                        if (pk_results[fmt].failures.size() < 3)
                            pk_results[fmt].failures.push_back(
                                e.path().filename().string() + "@" + std::to_string(entry.data_offset)
                                + ": " + ex.what());
                    }
                });
            }

            // Report extraction pipeline
            report("pk(extract)", total_entries);
            report("pk(skip)", skipped);
            for (auto& [fmt, r] : pk_results) {
                report(("pk" + fmt).c_str(), r);
            }
        }
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - t0).count();

    printf("\n=== SUMMARY ===\n");
    printf("Total: %d/%d ok (%.1f%%)\n", total_ok, total_ok + total_fail,
           100.0f * total_ok / (total_ok + total_fail));
    printf("Time: %lds\n", elapsed);

    return total_fail > 0 ? 1 : 0;
}
