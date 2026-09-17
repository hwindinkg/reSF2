#pragma once

// Gc.DK decision partition + tail (sf2.502f0946.js L673-674) — exact port
// of the pure logic, mirroring reference/tools/combat_golden.js
// (dkPartition S10 + dkTail S12). Operates on plain candidate structs so
// both the Node harness and this header agree bit-for-bit; the runtime
// reaction pick (Fighter::try_react) uses the partition ORDER (priority
// descending, matching hb_) while the weighted-roulette pick inside Pkb
// stays OPEN here (the reaction path has no tactic/feature state, and
// `Gc.DK`'s caller is the hit-reaction latch, not the per-frame `de.ia`).
//
// `Gc.DK` (L673) has exactly TWO reachable shapes and this header models
// the partition for both:
//   * `c == false` (the per-frame `dxa` L678 call, and only that): `d` =
//     the `eb`-true non-`Rha` candidates. `eb` is set solely by
//     `Gc.Vkb` -> `Ih(2,a,!0)` (L673), which is reached only from the AI's
//     Random-tactic `hJa` (L500 `a.type==1`) via `ca.Vgb` (L388). So this
//     shape is the AI's `Pkb` (tactic-roulette) path.
//   * `c == true` (the `Pkb` tail's `this.DK(a,d,!0)` re-entry, L676): `d`
//     is EMPTY, so the tail takes the else branch and starts `e` (the
//     `Aua` max-`priority` group pick) directly.
//
// The HUMAN key path never sets `eb`: `Gc.mS(a){this.Ih(2,a)}` (L672)
// leaves it false, so on `c == false` the human's `d` is empty too and
// `Pkb` — its `M7.Wcb` mirror filter, its `va.Ts` <Tactics><Conditions>
// filter and its `Gc.jL` -> `de.jL` -> `Md.jL` weighted ROUTLETTE — is
// never reached. See `Fighter::try_select_move` for the human path.
//
// JS (L673-674, verbatim shape):
//   partition: d=[], f=[], g=[]; e=null; Ukb=null;
//     per candidate h: c||!h.eb||h.animation.Rha||d.push(h);
//       h.animation.Rha ? Aua(h,g) : Aua(h,f);
//     Aua(a,b): ap=a.priority, bp=b[0]?.priority ?? 0;
//       ap>=bp && (ap>bp && (b.length=0), b.push(a));
//     f.length>0 && (e=f[sja(f.length)]);
//     g.length>0 && (ukb=g[sja(g.length)].animation);
//   tail: g.length>0 && a.Ukb(g[sja].animation);
//     d.length>0 ? (e!=null && d.push(e), Pkb(a,d))
//                : e!=null && (e.MS ? jJa(e) : Nsb(e), zY/jza set).
//
// The `d.push(h)` operand sits at the END of an `||` chain, so it runs only
// when every prior operand is FALSY: push iff `!c && h.eb && !h.Rha`.
// With `c == true` NOTHING is pushed (`d` empty -> the tail's else branch,
// which starts `e` directly and skips `Pkb` entirely); with `c == false`
// only the `eb`-true, non-`Rha` candidates are usable-now. This is exactly
// the split the two callers rely on: `c == false` comes from the per-frame
// `dxa` (L678) and keeps the AI's `Vkb` (`eb=true`) set on the filtered
// `Pkb` path, while the `Pkb` tail re-enters with `DK(a,d,!0)`
// (`c == true`) to start the surviving pick directly.

#include <cstddef>
#include <string>
#include <vector>

namespace sf2::scene {

// One DK candidate (JS `Ti`-ish: animation + flags the tail reads).
struct DkCandidate {
    std::string id;    // harness id (the candidate key)
    std::string anim_id;  // `animation` id recorded by Ukb (defaults to id)
    int priority = 0;  // `animation.priority`
    bool eb = false;   // event-busy flag (JS `h.eb`)
    bool rha = false;  // special-move flag (`animation.Rha`)
    bool ms = false;   // move-strike flag (`MS`, jJa vs Nsb branch)
    int r1 = 0;        // fall param (`R1`, jJa)
    int index = 0;     // table index (`index`, Nsb)
    std::string anim_type;  // `animation.type` (zY)
    int anim_e = 0;         // `animation.E_` (jza)
};

// The partition result (JS `d`/`f`/`g` lists + `e` pick + `ukb`).
struct DkPartition {
    std::vector<std::size_t> d;  // usable-now candidate indices
    std::vector<std::size_t> f;  // normal-candidate indices (Aua ordered)
    std::vector<std::size_t> g;  // special-candidate indices (Aua ordered)
    int e = -1;                  // f[sja] index, or -1
    int ukb = -1;                // g[sja] index, or -1
};

// JS partition loop (L673-674). `c` = the "all usable" flag; `sja_index`
// = the pre-drawn `sja(len)` index for the f-pick (the caller draws it;
// JS draws inline via `uf.sja`).
inline DkPartition dk_partition(const std::vector<DkCandidate>& cands, bool c,
                                std::size_t sja_index) {
    DkPartition out;
    // Aua appends with keep-highest semantics (>= keeps, > resets).
    auto aua = [&](std::size_t ci, std::vector<std::size_t>& b) {
        const int ap = cands[ci].priority;
        const int bp = b.empty() ? 0 : cands[b[0]].priority;
        if (ap >= bp) {
            if (ap > bp) b.clear();
            b.push_back(ci);
        }
    };
    for (std::size_t k = 0; k < cands.size(); ++k) {
        const DkCandidate& h = cands[k];
        // JS `c||!h.eb||h.animation.Rha||d.push(h)`: the push is the LAST
        // operand of an `||` chain, so it runs only when `c`, `!h.eb` and
        // `h.rha` are all falsy.
        if (!c && h.eb && !h.rha) out.d.push_back(k);
        if (h.rha) {
            aua(k, out.g);
        } else {
            aua(k, out.f);
        }
    }
    if (!out.f.empty() && sja_index < out.f.size()) {
        out.e = static_cast<int>(out.f[sja_index]);
    }
    if (!out.g.empty() && sja_index < out.g.size()) {
        out.ukb = static_cast<int>(out.g[sja_index]);
    }
    return out;
}

// The tail-branch Recorder (JS `Ukb`/`Pkb`/`jJa`/`Nsb` calls + `zY`/`jza`
// side effects, L674). The caller supplies the `e` pick; `g_pick` mirrors
// the `g[sja].animation` Ukb call. Pkb/jJa/Nsb bodies are OPEN (geometry +
// weight tails) — the call ORDER and side effects are what the golden
// pins (S12).
struct DkTailCall {
    // Pkb calls: each is the d-list snapshot passed (candidate indices).
    std::vector<std::vector<std::size_t>> pkb;
    // Nsb calls: (candidate index, table index).
    std::vector<std::pair<std::size_t, int>> nsb;
    // jJa calls: (candidate index, R1).
    std::vector<std::pair<std::size_t, int>> jja;
    // Ukb calls: recorded animation ids (`a.Ukb(...)` argument).
    std::vector<std::string> ukb;
    // Side effects on the fighter record (JS `a.Ukb`/`a.zY`/`a.jza`).
    std::string fighter_ukb;
    bool fighter_ukb_set = false;
    std::string zy;
    int jza = 0;
    bool zy_set = false;
};

// JS tail (L674): `g.length>0 && a.Ukb(g[sja].animation)` is modeled by
// passing `g_pick >= 0`. `d` = the d-list (with `e` appended by the caller
// when non-null, mirroring `e!=null&&d.push(e)`).
inline void dk_tail(DkTailCall& rec, const std::vector<DkCandidate>& cands,
                    std::vector<std::size_t> d, int e_idx, int g_pick) {
    if (g_pick >= 0 &&
        static_cast<std::size_t>(g_pick) < cands.size()) {
        const std::string& aid = cands[static_cast<std::size_t>(g_pick)].anim_id;
        rec.ukb.push_back(aid.empty() ? cands[static_cast<std::size_t>(g_pick)].id : aid);
        rec.fighter_ukb = rec.ukb.back();
        rec.fighter_ukb_set = true;
    }
    if (!d.empty()) {
        if (e_idx >= 0) d.push_back(static_cast<std::size_t>(e_idx));
        rec.pkb.push_back(d);
    } else if (e_idx >= 0 &&
               static_cast<std::size_t>(e_idx) < cands.size()) {
        const DkCandidate& e = cands[static_cast<std::size_t>(e_idx)];
        if (e.ms) {
            rec.jja.emplace_back(static_cast<std::size_t>(e_idx), e.r1);
        } else {
            rec.nsb.emplace_back(static_cast<std::size_t>(e_idx), e.index);
        }
        rec.zy = e.anim_type;
        rec.jza = e.anim_e;
        rec.zy_set = true;
    }
}

}  // namespace sf2::scene
