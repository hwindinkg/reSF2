// engine/game/tactic_tables.cpp
//
// Implementation of TacticTableSet (ADR-005 D3).
// The loader iterates a family descriptor table {subdir, parser-fn,
// family-tag} — adding a family = one row + one parser file, never
// editing a shared loader body (C3).

#include "tactic_tables.hpp"

#include "../reverse/atf_tactics.hpp"
#include "../reverse/tbs_tables.hpp"
#include "../reverse/stb_tables.hpp"
#include "../reverse/sts_tables.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace resf2::game {

namespace {

namespace fs = std::filesystem;

using FamilyParser = std::expected<std::vector<TacticTable>, std::string> (*)(
    const fs::path&);

// One row per table family (C3).
struct FamilyDescriptor {
    std::string_view subdir;    // under the tactics root; "" = the root itself
    FamilyParser     parser;
    TacticFamily     family;
};

// --- .atf family (assets/tactics/*.atf) — the only family with assets in
// this dump; parsed by reverse::atf (stride-858 record + string pool) ---
[[nodiscard]] auto parse_atf_family(const fs::path& dir)
    -> std::expected<std::vector<TacticTable>, std::string> {
    std::vector<TacticTable> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() != ".atf") continue;
        auto parsed = reverse::atf::parse_file(entry.path().string());
        if (!parsed) continue;  // one bad file must not sink the family
        TacticTable t;
        t.type = TacticTableType::kAttackTable;
        // [ORIGINAL] index key from the parsed Header, NOT the filename:
        // v=1 pair -> weapon_a + "_" + weapon_b; v=2 single -> weapon_a alone.
        t.name = parsed->header.version == 2
               ? parsed->header.weapon_a_name
               : parsed->header.weapon_a_name + "_" + parsed->header.weapon_b_name;
        t.candidates = parsed->animation_names;
        t.record.assign(parsed->animation_indices.begin(),
                        parsed->animation_indices.end());
        out.push_back(std::move(t));
    }
    return out;
}

// --- stub families (R1: format unreversed) — the reverse-layer stub parse
// is what reports the family unavailable ---
[[nodiscard]] auto parse_tbs_family(const fs::path& dir)
    -> std::expected<std::vector<TacticTable>, std::string> {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() != ".tbs") continue;
        auto parsed = reverse::tbs::parse_file(entry.path().string());
        if (!parsed) {
            return std::unexpected(std::string("tbs: ") +
                                   reverse::tbs::to_string(parsed.error()));
        }
    }
    return std::vector<TacticTable>{};
}

[[nodiscard]] auto parse_stb_family(const fs::path& dir)
    -> std::expected<std::vector<TacticTable>, std::string> {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() != ".stb") continue;
        auto parsed = reverse::stb::parse_file(entry.path().string());
        if (!parsed) {
            return std::unexpected(std::string("stb: ") +
                                   reverse::stb::to_string(parsed.error()));
        }
    }
    return std::vector<TacticTable>{};
}

[[nodiscard]] auto parse_sts_family(const fs::path& dir)
    -> std::expected<std::vector<TacticTable>, std::string> {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() != ".sts") continue;
        auto parsed = reverse::sts::parse_file(entry.path().string());
        if (!parsed) {
            return std::unexpected(std::string("sts: ") +
                                   reverse::sts::to_string(parsed.error()));
        }
    }
    return std::vector<TacticTable>{};
}

// --- directory families (dodge/, movements/, outcometablesforattack/):
// scan-if-present, else empty. Per-file formats are unreversed, so a
// present directory currently yields an empty (but loaded) family ---
[[nodiscard]] auto parse_dir_scan_family(const fs::path& dir)
    -> std::expected<std::vector<TacticTable>, std::string> {
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    (void)it;  // presence scan only — contents unparsed (R1 survey)
    return std::vector<TacticTable>{};
}

// [ORIGINAL] family subdirs from the binary's path strings
// (PORT_GAPS.md:159-164). The family descriptor table: adding a family =
// one row here + one parser file.
const FamilyDescriptor kFamilyDescriptors[] = {
    {"attack",                 parse_tbs_family,      TacticFamily::kTbs},
    {"shift",                  parse_stb_family,      TacticFamily::kStb},
    {"shiftTables",            parse_sts_family,      TacticFamily::kSts},
    {"",                       parse_atf_family,      TacticFamily::kAtf},
    {"dodge",                  parse_dir_scan_family, TacticFamily::kDodge},
    {"movements",              parse_dir_scan_family, TacticFamily::kMovements},
    {"outcometablesforattack", parse_dir_scan_family, TacticFamily::kOutcome},
};

}  // namespace

bool TacticTableSet::load(const std::string& asset_root) {
    tables_.clear();
    families_loaded_.reset();

    namespace fs = std::filesystem;
    // Tactics root: <root>/tactics, with <root>/assets/tactics as the
    // relocated-dump fallback.
    fs::path tactics_root;
    for (const fs::path& cand : {fs::path(asset_root) / "tactics",
                                 fs::path(asset_root) / "assets" / "tactics"}) {
        std::error_code ec;
        if (fs::is_directory(cand, ec)) {
            tactics_root = cand;
            break;
        }
    }
    if (tactics_root.empty()) return true;  // no tactics at all — not an error

    for (const auto& desc : kFamilyDescriptors) {
        const fs::path dir = desc.subdir.empty()
                           ? tactics_root
                           : tactics_root / fs::path(desc.subdir);
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;  // family absent — normal
        auto parsed = desc.parser(dir);
        if (!parsed) continue;  // family unavailable (stub/unreversed) — normal
        for (auto& t : *parsed) tables_.push_back(std::move(t));
        families_loaded_.set(static_cast<std::size_t>(desc.family));
    }
    return true;
}

const TacticTable* TacticTableSet::attack_table(std::string_view weapon_a,
                                                std::string_view weapon_b) const {
    // Mirrors the load-side key rule: v=1 pair -> a_b; v=2 single -> a.
    std::string key{weapon_a};
    if (!weapon_b.empty()) {
        key += '_';
        key += weapon_b;
    }
    return find(TacticTableType::kAttackTable, key);
}

const TacticTable* TacticTableSet::find(TacticTableType type,
                                        std::string_view name) const {
    for (const auto& t : tables_) {
        if (t.type == type && t.name == name) return &t;
    }
    return nullptr;
}

bool TacticTableSet::has_family(TacticFamily f) const {
    return families_loaded_.test(static_cast<std::size_t>(f));
}

float TacticTableSet::animation_factor(std::string_view /*animation*/,
                                       std::string_view /*target*/) const {
    // [HEURISTIC-TODO] neutral 0.0f until @reverser R2 lands the stride-858
    // row semantics (phase-5 PLAN B4). A miss is neutral, never an error
    // (ADR-005 D5).
    return 0.0f;
}

}  // namespace resf2::game
