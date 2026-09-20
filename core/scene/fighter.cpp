// Fighter: clip sampling (Te.eda, MODEL_FORMAT §2.2) + macro-node
// computation (Fl.seb, L797) + 2D triangle vertex build (dv.ia, L840).
//
// Phase 3.2b: move execution — input -> move selection -> clip playback,
// intervals, facing/mirror (JS study cited inline in fighter.hpp).

#include "scene/fighter.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <set>

#include "anim_archive.hpp"
#include "scene/ai.hpp"        // StrikeMemory (the `wd.Cn` strike accumulator)
#include "scene/conditions.hpp"
#include "scene/move_def.hpp"

namespace sf2::scene {

namespace {
// JS `v.wya` (`internal_settings.xml` `<PivotNode Name>`, parse L1155:
// `v.wya = b != null ? b : "NPivot"`). The config parse overwrites the
// initial `""` default; the shipped value is "NPivot".
std::string& pivot_bone_storage() {
    static std::string name = "NPivot";
    return name;
}
} // namespace

const std::string& fighter_pivot_bone() { return pivot_bone_storage(); }

void set_fighter_pivot_bone(const std::string& name) {
    if (!name.empty()) pivot_bone_storage() = name;
}

void Fighter::set_model(const Model& model) {
    model_ = model;
    pos_.assign(model_.bones.size() * 2, 0.0f);
    playhead_ = 0;
    prepend_.clear();

    // [FIX stretched mesh — ragdoll solver] The game keeps the body/head
    // cloth nodes attached to the skeleton with a Verlet ragdoll solver
    // (JS `Al`, L296179: `ia()` = `sk()` integrate + `jE()` edge relax x
    // `IterativeProcess`=2, internal_settings.xml Physics). The old native
    // anchored each cloth node at its bind offset from the bind-nearest
    // skeleton bone — but the cloth's <Edges> constraints (rest lengths
    // 10-45) bind the cloth to DIFFERENT bones (the head cloth to the
    // HEAD-MacroNode* weighted averages, the body cloth to knees/ankles/
    // hips), which move independently of the bind-nearest bone. The static
    // offset left cloth+mesh triangles stretched tens of units (median
    // aspect 2.1, max 462.9 in the idle pose). The solver state below is
    // seeded from the bind pose (JS `Vc` ctor: ma = mf = p8) and stepped
    // once per sample() call — the game's 60 Hz cadence.
    const std::size_t n3 = model_.bones.size() * 3;
    sol_ma_.assign(n3, 0.0f);
    sol_mf_.assign(n3, 0.0f);
    for (std::size_t i = 0; i < model_.bones.size(); ++i) {
        const Bone& b = model_.bones[i];
        sol_ma_[i * 3] = b.x;
        sol_ma_[i * 3 + 1] = b.y;
        sol_ma_[i * 3 + 2] = b.z;
        sol_mf_[i * 3] = b.x;
        sol_mf_[i * 3 + 1] = b.y;
        sol_mf_[i * 3 + 2] = b.z;
    }
    // [FIX root-motion - no cross-clip state translation] The old solver-state
    // continuity seed and the per-move `translate_solver_state` it fed are
    // REMOVED: the align shift in `compute_align` (JS `Te.Gub`/`Gla`) already
    // re-expresses every new clip into the previous pose own continuous space,
    // so `sol_ma_` needs no translation (JS `Vc` ctor L793-794: ma = mf = bind,
    // then one `Al.ia` step per frame, L582 - no warmup, no cross-clip shift).
    solver_init_ = true;
    // JS `Vc` ctor (L793-794): ma = mf = the bind position (`p8`). The
    // solver then runs exactly one `Al.ia()` step per frame (L582) — the
    // game has NO warmup and NO cross-clip state translation.
    align_x_ = align_y_ = align_z_ = 0.0f;

    // Build mirror swap pairs for _1 ↔ _2 (JS Te.Peb L560 → Ua.Oeb L692).
    // [F1/F3] JS `Dl.Hqb` (L580) -> `Dl.v5a` (L580): for EVERY node whose name
    // ends in `_1`, find the node named `<stem>_2` and push `Ba(_1.id, _2.id)`
    // — the ORDER of the two in `Va.all` is irrelevant. The old
    // `if (j <= i) continue;` guard dropped every pair the skeleton stores
    // `_2`-first (NShoulder, NElbow, NWrist, NHip, NKnee, NAnkle, NToe,
    // NHeel, NToeS, NToeTip, NKnuckles, NFingertips, NChestS, NStomachS,
    // NHeadS, MacroNode1..6, ...) — i.e. every body pair; only the
    // `Weapon-Node1..4` pairs (stored `_1`-first) survived.
    mirror_pairs_.clear();
    for (std::size_t i = 0; i < model_.bones.size(); ++i) {
        const std::string& nm = model_.bones[i].name;
        if (nm.size() < 3) continue;
        if (nm.compare(nm.size() - 2, 2, "_1") != 0) continue;
        const std::string other = nm.substr(0, nm.size() - 2) + "_2";
        const int j = model_.bone_by_name(other);
        if (j < 0) continue;
        mirror_pairs_.emplace_back(static_cast<int>(i), j);
    }
    // Per-move mirror state (set by `start_move_impl`): idle until a move runs.
    mirror_swap_ = false;
    mirror_x_ = false;
    mirror_prepend_ = false;
    clip_mirror_ = 1;  // JS `Te` ctor L545 / `Te.reset` L548: `FX = 1`
    pose_sampled_ = false;
}

// ---------------------------------------------------------------------------
// Move execution (Phase 3.2b) — JS `wd`/`Te`/`de` semantics
// ---------------------------------------------------------------------------

// JS `ra.Hza` (L684-685): the fighter's move set `me` is built by testing
// every parsed move's Locks against the fighter's items. Task contract:
// the equipped weapon (Fists) contributes all moves tagged
// `TacticWeapon == weapon_subtype` (JS `Fa.Ueb` L711 reads TacticWeapon
// into `QX`).
//
// ORDER (P4a): the JS never priority-sorts `me`. `ra.Hza` (L684-685) walks
// `ra.Lk` — the moves in `<Moves><Move>` DOCUMENT order — and pushes each
// locks-passing move; every downstream consumer keeps that order:
//   `wd.ia` L499/L502 `Su.FT(this.me)` -> `ru.iQ` (`ru.FT` L539 /
//   `dea(2)` L540) -> `Gc.EZa` L676 walks it to build the KeyPressed
//   candidates, `Gc.DK`/`Aua` L674 breaks equal-`priority` ties by
//   insertion order, and `de.Pqb` L604-608 / `jL` L594 never re-sort.
// The previous `std::sort(priority >)` was JS-wrong twice: it is not the
// JS order at all, and being UNSTABLE it resolved equal-`priority` ties to
// the `std::map` iteration order = ALPHABETICAL — the observed F420 list
// `DoublePunch,HeavyPunch,HighPunch` (each 100) ahead of `StepForward`.
// `MoveDef::profile_order` IS the JS document index (`ra.Ul` push order,
// `Fa.Ueb` L712), so ordering by it reproduces `ra.Lk`.
void Fighter::build_move_list(const std::map<std::string, MoveDef>& all_moves,
                              const std::string& weapon_subtype,
                              bool include_universal) {
    hb_.clear();
    for (const auto& kv : all_moves) {
        const MoveDef& m = kv.second;
        if (!m.tactic_weapon.empty()) {
            // Weapon-locked move: only the matching weapon contributes it.
            if (m.tactic_weapon != weapon_subtype) {
                continue;
            }
        } else if (!include_universal) {
            continue;
        }
        // No TacticWeapon -> universal (Skeleton lock passes for all).
        hb_.push_back(&m);
    }
    // P4a: JS `ra.Lk` document order (`profile_order`), NOT priority desc —
    // see the ORDER note on `build_move_list` above. `Aua` (L674) does the
    // priority grouping at selection/hit-reaction time, keeping ties in this
    // document order (insertion order).
    std::sort(hb_.begin(), hb_.end(), [](const MoveDef* a, const MoveDef* b) {
        return a->profile_order < b->profile_order;
    });
}

// JS `ra.Hza` (L684-685): the move set `me` is built by testing every move's
// Locks against the fighter's items (`f.nw(d,b)`). A lock group passes when:
//   - a plain <Item Type SubType Name> matches an owned item;
//   - an <Operator Type="Or"> group passes when ANY member item matches;
// moves with no locks are universal (every fighter has the Skeleton).
// This mirrors the game exactly: a WEAPON_KNIVES (SubType="Knives") owner
// gets KnivesSlash (Locks: Or{Weapon Knives, Weapon Keris}).
//
// The item test is the JS `Hm.he` (L758) VERBATIM:
//   c = (this.uc==""||this.uc==b.type)
//       ? (this.Zta==""||this.Zta==b.Yb) : false;
//   c = c ? (this.Ba==""||this.Ba==b.name) : false;
//   if(c) return !this.cb;   return this.cb;
// i.e. Type / SubType / Name must all match (an empty lock field is a
// wildcard) and `this.cb` (`Not`, `tb.init` L763) INVERTS the result —
// `Not="1"` passes when NO owned item matches. Both halves were missing:
// the name was never compared (so `Armor Name="BODY_GATEKEEPER"` matched ANY
// armor) and `Not` was parsed away (so `Not="1"` passed on an OWNED item —
// the exact opposite).
template <typename Owned>
static bool owned_item_matches(const Lock& l, const Owned& owned) {
    bool matched = false;
    for (const auto& o : owned) {
        if (o.type != l.type) continue;
        if (!l.subtype.empty() && o.subtype != l.subtype) continue;
        // `this.Ba == b.name`: a NAME-bearing lock requires the item name.
        // An owned entry with no name can never satisfy a named lock.
        if (!l.name.empty() && o.name != l.name) continue;
        matched = true;
        break;
    }
    // `Hm.he`: `if(c) return !this.cb; return this.cb;`
    return l.not_ ? !matched : matched;
}

void Fighter::build_move_list_locks(
    const std::map<std::string, MoveDef>& all_moves,
    const std::vector<OwnedItem>& owned,
    bool include_universal, const std::string& weapon_subtype) {
    auto owned_item = [&owned](const Lock& l) {
        return owned_item_matches(l, owned);
    };
    hb_.clear();
    for (const auto& kv : all_moves) {
        const MoveDef& m = kv.second;
        // JS `ra.Hza` applies BOTH of the move's gates against the fighter:
        //   - the TacticWeapon list (`Fa.Ueb` L711 -> `QX`, L800
        //     `TacticWeapon.split("|")`; `bCa` L510 + `c2a` L820 test the
        //     CURRENT move's `QX` membership) and
        //   - the `<Locks>` item test (`f.nw(d,b)`).
        // This builder used to apply only the Locks half, so weapon-locked
        // moves whose locks happened to pass stayed in the list (e.g.
        // `RatWavePlayer`, `TacticWeapon="MassBomb"`, `<Locks>` =
        // PERK_RAT_WAVE + Skeleton, Priority 110 - it won the Up key and
        // played `rats_wave`). `weapon_subtype` empty keeps the legacy
        // Locks-only behaviour for the display/probe callers.
        if (!weapon_subtype.empty() && !m.tactic_weapon.empty()) {
            bool allows = false;
            if (!m.qx.empty()) {
                for (const std::string& w : m.qx) {
                    if (w == weapon_subtype) {
                        allows = true;
                        break;
                    }
                }
            } else {
                allows = m.tactic_weapon == weapon_subtype;
            }
            if (!allows) continue;
        }
        if (m.locks.empty()) {
            // No locks -> universal (JS: every move's Skeleton lock passes).
            if (include_universal) hb_.push_back(&m);
            continue;
        }
        // Evaluate the lock list: every top-level lock must pass.
        // Or-group locks (l.or_) pass when ANY member passes — the parser
        // flattens the Or group into one lock per member item with or_=true.
        bool all_pass = true;
        // Locks flattened from ONE `<Operator>` form ONE group (JS: a single
        // Or node); the group passes when ANY member passes. SEPARATE
        // operators are separate groups, AND-combined (`Lock::group`). The
        // old single `or_group`/`any_or` pair conflated them: the `StanceLeft`
        // template carries `Or{Screen ShopWeapon, Screen Profile, Screen
        // Fight}` and `KnucklesStartStance-Left` adds `Or{Item
        // Weapon/Knuckles, ...}`; the passing `Screen="Fight"` set `any_or`,
        // so the item requirement was satisfied and EVERY weapon's
        // `StartStance*` entered `hb_` — the highest-priority one (Knuckles,
        // Priority 12) then played regardless of the equipped weapon (the
        // reported wrong intro). Per-group evaluation fixes it.
        std::map<int, bool> groups;  // group id -> a member has passed
        for (const Lock& l : m.locks) {
            if (l.never) {
                // An unmodelled lock kind (`<Perk Name=..>`, `<Screen
                // Name=..>`): the JS tests it (`Bm`/`Gm`), the port cannot.
                // `<Screen>` IS modelled (the `Lock::screen` name + the JS
                // `Gm.he` class 18 map `hfa`: "Fight" -> 10, "Profile" -> 9,
                // "ShopWeapon" -> 2, ...; `he`: `a.ul == this.tVa`). This list
                // is built for the FIGHT screen, so a `Screen Name="Fight"`
                // lock PASSES. Failing it closed removed every `Throw*` move
                // from `hb_` (they all inherit `<Locks><Screen
                // Name="Fight"/></Locks>` from the `Throw` template,
                // moves.xml L546), so no throw could ever be selected — the
                // throw-gate bug. Every other screen name stays fail-closed
                // (the ShopTryOn moves never belong in a fight list).
                const bool screen_pass = (l.screen == "Fight");
                if (l.group >= 0) {
                    if (screen_pass) groups[l.group] = true;
                    else groups.emplace(l.group, false);
                    continue;
                }
                if (screen_pass) continue;
                all_pass = false;
                break;
            }
            const bool pass = owned_item(l);
            if (l.group >= 0) {
                if (pass) groups[l.group] = true;
                else groups.emplace(l.group, false);
            } else if (!pass) {
                all_pass = false;
                break;
            }
        }
        if (!all_pass) continue;
        bool groups_ok = true;
        for (const auto& g : groups) {
            if (!g.second) {
                groups_ok = false;
                break;
            }
        }
        if (!groups_ok) continue;
        hb_.push_back(&m);
    }
    // P4a: JS `ra.Lk` document order (`profile_order`), NOT priority desc —
    // see the ORDER note on `build_move_list` above. `Aua` (L674) does the
    // priority grouping at selection/hit-reaction time, keeping ties in this
    // document order (insertion order).
    std::sort(hb_.begin(), hb_.end(), [](const MoveDef* a, const MoveDef* b) {
        return a->profile_order < b->profile_order;
    });
}

// The idle auto-play's move pick, JS `Aua` (L673) over the fighter's OWN
// unlocked list. `hb_` already carries only the moves the equipped weapon
// admits (`ra.Hza` L684-685), so the `Template` tag is enough to isolate the
// stance family — the old hardcoded `FistsStartStanceIdle-*` name ignored the
// equipped weapon and made a knives fighter play the Fists stance.
const MoveDef* Fighter::stance_move(const std::vector<std::string>& templates,
                                    bool is_player) const {
    // `Aua` (L673): keep the max-`priority` group. Equal priorities stay in
    // `hb_` (document) order.
    std::vector<const MoveDef*> group;
    for (const MoveDef* m : hb_) {
        bool has = false;
        for (const std::string& t : templates) {
            if (m->template_tags.count(t) != 0) {
                has = true;
                break;
            }
        }
        if (!has) continue;
        if (group.empty() || m->priority > group.front()->priority) {
            group.clear();
            group.push_back(m);
        } else if (m->priority == group.front()->priority) {
            group.push_back(m);
        }
    }
    if (group.empty()) return nullptr;
    // Within the group: the `<Player Number=1>` (controlled) variant is the
    // `-Left` one (`Dm.he` L755 `Number==1 == qb`); the other side takes
    // `-Right`. A variant with no side suffix is common to both and wins when
    // it is the only one (Knives/Daggers/... one idle move per weapon).
    const std::string want = is_player ? "-Left" : "-Right";
    const MoveDef* unsuffixed = nullptr;
    for (const MoveDef* m : group) {
        if (m->name.size() >= want.size() &&
            m->name.compare(m->name.size() - want.size(), want.size(), want) == 0) {
            return m;
        }
        const bool has_left =
            m->name.size() >= 5 && m->name.compare(m->name.size() - 5, 5, "-Left") == 0;
        const bool has_right =
            m->name.size() >= 6 && m->name.compare(m->name.size() - 6, 6, "-Right") == 0;
        if (!has_left && !has_right) unsuffixed = m;
    }
    return unsuffixed != nullptr ? unsuffixed : group.front();
}

// JS `Pi.Ex` (L2301 region): the shop's try-on animation state is `LX=7`
// (`iz.XBa("TryOn")=7`, L444). The move that plays is resolved by the normal
// move machinery against the WORN items + the open shop screen: a move whose
// Template carries `ShopTryOn` (moves.xml L229 `<Template Name="ShopTryOn">`)
// and whose `<Screen>` + `<Item>` locks pass. E.g. WEAPON_KNIVES
// (SubType="Knives") resolves `ShopKnivesSuperSlash` (moves.xml L9437,
// `knives_super_slash.bin`; Screen ShopWeapon + Item{Weapon,Knives}).
// `Aua` (L673) keeps the max-`priority` group; ties stay in document order.
const MoveDef* Fighter::shop_tryon_move(
    const std::map<std::string, MoveDef>& all_moves,
    const std::vector<OwnedItem>& worn, const std::string& shop_screen) {
    const auto screen_pass = [&shop_screen](const Lock& l) {
        return !l.screen.empty() && l.screen == shop_screen;
    };
    // Evaluate a move's lock list. `<Screen>` passes on the shop screen;
    // `<Item>` uses the `Hm.he` test; any other unmodelled lock (`<Perk>`)
    // fails the move closed (the shipped ShopTryOn moves carry none).
    const auto locks_pass = [&](const MoveDef& m) {
        bool any_or = false, or_group = false;
        for (const Lock& l : m.locks) {
            if (!l.screen.empty()) {
                if (l.or_) { or_group = true; if (screen_pass(l)) any_or = true; }
                else if (!screen_pass(l)) return false;
                continue;
            }
            if (l.never) return false;  // `<Perk>` / other unmodelled: closed
            if (l.or_) {
                or_group = true;
                if (owned_item_matches(l, worn)) any_or = true;
            } else if (!owned_item_matches(l, worn)) {
                return false;
            }
        }
        return !or_group || any_or;
    };
    const MoveDef* best = nullptr;
    for (const auto& kv : all_moves) {
        const MoveDef& m = kv.second;
        if (m.template_tags.count("ShopTryOn") == 0) continue;
        if (!locks_pass(m)) continue;
        // `Aua` (L673): max-priority group, document order on ties.
        if (best == nullptr || m.priority > best->priority) best = &m;
    }
    return best;
}

// JS `zl.yLa` (L799): `zg.Fh` = a Hold for every currently-down key
// (`Ff[].sl`). Rebuilt from the physical held set each tick/press.
void Fighter::rebuild_holds() {
    for (auto it = keys_.begin(); it != keys_.end();) {
        if (it->press == press_type::hold) it = keys_.erase(it);
        else ++it;
    }
    for (const key_type k : held_keys_) keys_.push_back({k, press_type::hold});
}

void Fighter::age_keys() {
    // JS `zl.ia` (L798):
    //   Qe==30 -> zg.clear() (drop holds + releases), Qe=0;
    //   dX>=15 -> dX=0, zg.sh.length=0 (drop the Tap sequence);
    //   yLa(); dX++; Qe++.
    if (hold_age_ == 30) {
        for (auto it = keys_.begin(); it != keys_.end();) {
            if (it->press == press_type::hold || it->press == press_type::release)
                it = keys_.erase(it);
            else ++it;
        }
        hold_age_ = 0;
    }
    if (tap_age_ >= 15) {
        tap_age_ = 0;
        for (auto it = keys_.begin(); it != keys_.end();) {
            if (it->press == press_type::tap) it = keys_.erase(it);
            else ++it;
        }
    }
    rebuild_holds();  // yLa()
    ++tap_age_;       // dX++
    ++hold_age_;      // Qe++
}

// JS `zl.Sgb` (L798): on a key-down edge append the key to the 2-slot Tap
// sequence `zg.sh` (NO same-key replacement — two taps of one key are two
// entries, which is what the `2key`/`3key` templates require), evicting the
// oldest past 2, then rebuild `zg.Fh` from the down keys. `zl.Xgb` (L799)
// on release drops the hold and records the release only when the key was
// never tapped (`!zg.sh.includes(index)`).
//
// `zl.Sgb` is guarded: `if(a!=null && !a.sl){ a.sl=!0; ... }` — a key that
// is ALREADY down (`sl` set) produces no second tap row, so a held key
// cannot spam duplicates. The body then calls `this.zg.clear()`
// (`zd.clear` L688: `this.Fh.length=0; this.ev=this.released.length=0`),
// which drops the stale `released` rows of the previous press (`Fh` is
// immediately rebuilt by `yLa`).
void Fighter::input(sf2::scene::key_type key, sf2::scene::press_type press) {
    if (press == press_type::tap) {
        // JS `Sgb` guard `!a.sl` — the key is already down: ignore the
        // duplicate edge (no second Tap row).
        if (held_keys_.count(key) != 0) return;
        // JS `Sgb` -> `zd.clear()` (L688): a new press clears the stale
        // `released` rows (holds are rebuilt below by `rebuild_holds`).
        keys_.erase(std::remove_if(keys_.begin(), keys_.end(),
                                   [](const key_input& k) {
                                       return k.press == press_type::release;
                                   }),
                    keys_.end());
        keys_.push_back({key, press_type::tap});
        int taps = 0;
        for (const key_input& k : keys_) {
            if (k.press == press_type::tap) ++taps;
        }
        while (taps > 2) {
            for (auto it = keys_.begin(); it != keys_.end(); ++it) {
                if (it->press == press_type::tap) {
                    keys_.erase(it);
                    break;
                }
            }
            --taps;
        }
        tap_age_ = 0;            // `this.dX=0`
        held_keys_.insert(key);  // `a.sl=!0`
        rebuild_holds();         // `yLa()`
    } else if (press == press_type::hold) {
        held_keys_.insert(key);
        rebuild_holds();
    } else {  // release (JS `zl.Xgb`)
        bool in_taps = false;
        for (const key_input& k : keys_) {
            if (k.key == key && k.press == press_type::tap) in_taps = true;
        }
        const bool was_held = held_keys_.erase(key) != 0;
        rebuild_holds();
        if (!in_taps && was_held) keys_.push_back({key, press_type::release});
    }
}

// Test/trace accessors (no behavior change).
int Fighter::buffered_tap_count() const {
    int n = 0;
    for (const key_input& k : keys_) {
        if (k.press == press_type::tap) ++n;
    }
    return n;
}

int Fighter::buffered_hold_count() const {
    int n = 0;
    for (const key_input& k : keys_) {
        if (k.press == press_type::hold) ++n;
    }
    return n;
}

// JS `jc.c7a` (L691): an interval is active when
//   max(start, qx) <= frame <= min(finish, Lj).
std::vector<std::string> Fighter::intervals_at(int frame) const {
    std::vector<std::string> out;
    if (current_move_ == nullptr) return out;
    for (const Interval& iv : current_move_->intervals) {
        const int s = std::max(iv.start, current_move_->first_frame);
        // JS `fe.init`: finish = `End` attr, else `pva+2` (`Interval::
        // end_default` -> the loaded clip's length + 2).
        const int e = interval_last(iv);
        if (s <= frame && frame <= e) {
            out.push_back(iv.name.empty() ? "type" + std::to_string(iv.type) : iv.name);
        }
    }
    return out;
}

int Fighter::interval_type(const std::string& name) const {
    if (current_move_ == nullptr) return 0;
    for (const Interval& iv : current_move_->intervals) {
        const std::string key =
            iv.name.empty() ? "type" + std::to_string(iv.type) : iv.name;
        if (key == name) return iv.type;
    }
    return 0;
}

// JS `wd.qYa`/`Nbb` (L514): a Block interval (`yD(5)`) active now.
bool Fighter::has_block() const {
    if (current_move_ == nullptr) return false;
    for (const Interval& iv : current_move_->intervals) {
        if (iv.type != 5) continue;  // `fe.G0`: Block = 5 (L774)
        const int s = std::max(iv.start, current_move_->first_frame);
        if (s <= move_frame_ && move_frame_ <= interval_last(iv)) return true;
    }
    return false;
}

// JS `Te.yD(6)` presence (L553): Invulnerable active now (HZa gate).
bool Fighter::has_invuln() const {
    if (current_move_ == nullptr) return false;
    for (const Interval& iv : current_move_->intervals) {
        if (iv.type != 6) continue;  // `fe.G0`: Invulnerable = 6 (L774)
        const int s = std::max(iv.start, current_move_->first_frame);
        if (s <= move_frame_ && move_frame_ <= interval_last(iv)) return true;
    }
    return false;
}

// JS `Te.hT(5)` (L554, reverse loop): drop every active Block interval.
void Fighter::clear_block() { clear_intervals(5, ""); }

// JS `Te.hT(type)` / `F4(name)` (L554) + scripted `Yob` (L1294): drop every
// active interval matching TYPE (-1 = any) or NAME ("" = any). Interval
// identity is the active-set key (name, or "type<N>" for nameless).
// Per-bone knockback feed (JS `Bl.strike` L582 moves the endpoint bodies).
void Fighter::add_knockback(int bone, const sf2::scene::Vec3& v) {
    if (bone < 0 || model_.bones.empty()) return;
    const std::size_t n = model_.bones.size();
    if (kb_.size() != n) kb_.assign(n, sf2::scene::Vec3{});
    kb_[static_cast<std::size_t>(bone)] =
        kb_[static_cast<std::size_t>(bone)] + v;
}

// JS `Al.start(a)` (L582): `this.nk=!0; this.frameCount=0; this.names=[];
// a!=null&&addRange(this.names,a); this.oa.BKa()`. `BKa()` re-seeds the
// solver bodies; the port promotes the whole solver state to WORLD space
// (the JS node `ma` is world) seeded from the current sampled pose with
// zero velocity.
void Fighter::ragdoll_start(const std::string& reaction, float wall_min,
                            float wall_max, float floor_y) {
    nk_ = true;
    ragdoll_frame_count_ = 0;
    ragdoll_names_.clear();
    if (!reaction.empty()) ragdoll_names_.push_back(reaction);
    ragdoll_wall_min_ = wall_min;
    ragdoll_wall_max_ = wall_max;
    ragdoll_floor_y_ = floor_y;
    const std::size_t n = model_.bones.size();
    if (n == 0) return;
    sol_ma_.assign(n * 3, 0.0f);
    sol_mf_.assign(n * 3, 0.0f);
    const bool have_pos = pos_.size() == n * 2;
    for (std::size_t i = 0; i < n; ++i) {
        const float wx = have_pos ? pos_[i * 2] : model_.bones[i].x;
        const float wy = have_pos ? pos_[i * 2 + 1] : model_.bones[i].y;
        sol_ma_[i * 3] = wx;
        sol_ma_[i * 3 + 1] = wy;
        sol_ma_[i * 3 + 2] = model_.bones[i].z;
        sol_mf_[i * 3] = wx;
        sol_mf_[i * 3 + 1] = wy;
        sol_mf_[i * 3 + 2] = model_.bones[i].z;
    }
    solver_init_ = true;
    solver_world_ = true;
    // `pos = px - px[anchor] + world_x` with `world_x = px[anchor] +
    // render_offset + j8`, so the placement base (world -> clip) is
    // `render_offset + j8`. Captured here so `ragdoll_stop` is continuous.
    solver_base_x_ = render_offset_ + j8_x_;
    solver_base_y_ = render_offset_y_;
}

// JS `Al.stop()` (L582): clears `nk` (frameCount/names reset on next start).
void Fighter::ragdoll_stop() {
    if (!nk_ && !solver_world_) return;
    const bool was_world = solver_world_;
    nk_ = false;
    ragdoll_frame_count_ = 0;
    ragdoll_names_.clear();
    if (was_world && solver_init_ && !sol_ma_.empty()) {
        // Return the solver state to the clip space the resuming `eda` uses,
        // and zero the Verlet velocity so the world delta is not read as an
        // impulse on the transition frame.
        for (std::size_t i = 0; i < sol_ma_.size(); i += 3) {
            sol_ma_[i] -= solver_base_x_;
            sol_ma_[i + 1] -= solver_base_y_;
        }
        sol_mf_ = sol_ma_;
    }
    solver_world_ = false;
    render_offset_valid_ = false;  // re-anchor on the next clip sample
}

// JS `Bl.strike` (L587-588): `a.sx.XA(l)` / `a.Zs.XA(c)` — the impulse-split
// vectors are ADDED to the endpoint nodes' world `ma` (persistent while the
// ragdoll is active; the clip apply never overwrites them then).
void Fighter::strike_node(int bone, const sf2::scene::Vec3& v) {
    if (bone < 0 || model_.bones.empty()) return;
    const std::size_t n = model_.bones.size();
    const std::size_t u = static_cast<std::size_t>(bone);
    if (u >= n) return;
    if (solver_world_ && sol_ma_.size() == n * 3) {
        sol_ma_[u * 3] += v.x;
        sol_ma_[u * 3 + 1] += v.y;
        sol_ma_[u * 3 + 2] += v.z;
    }
    if (pos_.size() == n * 2) {
        pos_[u * 2] += v.x;
        pos_[u * 2 + 1] += v.y;
    }
}

int Fighter::capsule_bbox(float& min_x, float& min_y, float& max_x,
                          float& max_y) const {
    min_x = min_y = max_x = max_y = 0.0f;
    const std::size_t n = model_.bones.size();
    if (pos_.size() < n * 2) return 0;
    int count = 0;
    bool first = true;
    for (const EdgeDef& e : model_.edges) {
        const int i1 = model_.bone_by_name(e.end1);
        const int i2 = model_.bone_by_name(e.end2);
        const int idx[2] = {i1, i2};
        for (int k = 0; k < 2; ++k) {
            const int bi = idx[k];
            if (bi < 0 || static_cast<std::size_t>(bi) >= n) continue;
            const float vx = pos_[static_cast<std::size_t>(bi) * 2];
            const float vy = pos_[static_cast<std::size_t>(bi) * 2 + 1];
            const float r = e.radius;
            if (first) {
                min_x = vx - r; max_x = vx + r;
                min_y = vy - r; max_y = vy + r;
                first = false;
            } else {
                min_x = std::min(min_x, vx - r);
                max_x = std::max(max_x, vx + r);
                min_y = std::min(min_y, vy - r);
                max_y = std::max(max_y, vy + r);
            }
        }
        if (i1 >= 0 || i2 >= 0) ++count;
    }
    return count;
}

// JS `wd.wKa(a)` (L523) — reset the slot's cooldown. Emits `yd(slot,0,0)`.
void Fighter::ability_cooldown_reset(int slot) {
    switch (slot) {
        case 9:  ability_cooldowns_.punch_active_ = false;  ability_cooldowns_.punch_elapsed_ = 0.0f;  break;
        case 10: ability_cooldowns_.kick_active_ = false;   ability_cooldowns_.kick_elapsed_ = 0.0f;   break;
        case 11: ability_cooldowns_.ranged_active_ = false; ability_cooldowns_.ranged_elapsed_ = 0.0f; break;
        case 14: ability_cooldowns_.super_active_ = false;  ability_cooldowns_.super_elapsed_ = 0.0f;  break;
        default: break;  // JS: no case -> no-op
    }
    if (slot == 9 || slot == 10 || slot == 11 || slot == 14) {
        std::fprintf(stdout, "[cd] wKa slot=%d emit yd(%d,0,0)\n", slot, slot);
        std::fflush(stdout);
    }
}

// JS `wd.b5(a, b)` (L524) — arm the slot's cooldown.
void Fighter::ability_cooldown_start(int slot, float duration) {
    if (duration <= 0.0f) duration = 1.0f;  // `b<=0&&(b=1)`
    switch (slot) {
        case 9:  ability_cooldowns_.punch_active_ = true;  ability_cooldowns_.punch_duration_ = duration; break;
        case 10: ability_cooldowns_.kick_active_ = true;   ability_cooldowns_.kick_reload_ = duration;   break;
        case 11: ability_cooldowns_.ranged_active_ = true; ability_cooldowns_.ranged_reload_ = duration; break;
        case 14: ability_cooldowns_.super_active_ = true;  ability_cooldowns_.super_reload_ = duration;  break;
        default: break;
    }
    if (slot == 9 || slot == 10 || slot == 11 || slot == 14) {
        std::fprintf(stdout, "[cd] b5 slot=%d duration=%.3f active\n", slot,
                     static_cast<double>(duration));
        std::fflush(stdout);
    }
}

// JS `wd.MOa()` (L532-533): advance the live cooldowns one frame. The `!=`
// guards on 9/10/11 and the `<` on 14 are load-bearing (they suppress the
// emit once the timer reaches its target / clears `oU`).
void Fighter::tick_ability_cooldowns(float game_speed) {
    AbilityCooldown& cd = ability_cooldowns_;
    auto step = [game_speed](float target, float reload) {
        const float denom = reload * game_speed;
        if (denom == 0.0f) return std::numeric_limits<float>::infinity();
        return target / denom;
    };
    // The JS emit `yd(slot, elapsed, 1)` fires every frame onto `this.yp`
    // (the ability-animation bus the port does not have), so only the
    // transition to ready is logged — a per-frame print would flood the
    // capture/trace output (the Super charge runs 500 frames at fight start).
    if (cd.punch_active_ && cd.punch_elapsed_ != cd.punch_target_) {
        cd.punch_elapsed_ += step(cd.punch_target_, cd.punch_reload_);
        if (cd.punch_elapsed_ > cd.punch_target_) cd.punch_elapsed_ = cd.punch_target_;
        if (cd.punch_elapsed_ == cd.punch_target_) {
            std::fprintf(stdout, "[cd] MOa slot=9 ready (yd(9,%.3f,1))\n",
                         static_cast<double>(cd.punch_elapsed_));
            std::fflush(stdout);
        }
    }
    if (cd.kick_active_ && cd.kick_elapsed_ != cd.kick_target_) {
        cd.kick_elapsed_ += step(cd.kick_target_, cd.kick_reload_);
        if (cd.kick_elapsed_ > cd.kick_target_) cd.kick_elapsed_ = cd.kick_target_;
        if (cd.kick_elapsed_ == cd.kick_target_) {
            std::fprintf(stdout, "[cd] MOa slot=10 ready (yd(10,%.3f,1))\n",
                         static_cast<double>(cd.kick_elapsed_));
            std::fflush(stdout);
        }
    }
    if (cd.ranged_active_ && cd.ranged_elapsed_ != cd.ranged_target_) {
        cd.ranged_elapsed_ += step(cd.ranged_target_, cd.ranged_reload_);
        if (cd.ranged_elapsed_ > cd.ranged_target_) cd.ranged_elapsed_ = cd.ranged_target_;
        if (cd.ranged_elapsed_ == cd.ranged_target_) {
            std::fprintf(stdout, "[cd] MOa slot=11 ready (yd(11,%.3f,1))\n",
                         static_cast<double>(cd.ranged_elapsed_));
            std::fflush(stdout);
        }
    }
    if (cd.super_active_ && cd.super_elapsed_ < cd.super_target_) {
        cd.super_elapsed_ += step(cd.super_target_, cd.super_reload_);
        if (cd.super_elapsed_ > cd.super_target_) cd.super_elapsed_ = cd.super_target_;
        if (cd.super_elapsed_ >= cd.super_target_) {
            cd.super_active_ = false;
            std::fprintf(stdout, "[cd] MOa slot=14 ready (yd(14,%.3f,1))\n",
                         static_cast<double>(cd.super_elapsed_));
            std::fflush(stdout);
        }
    }
}

bool Fighter::ability_cooldown_running(int slot) const {
    const AbilityCooldown& cd = ability_cooldowns_;
    switch (slot) {
        case 9:  return cd.punch_active_ && cd.punch_elapsed_ < cd.punch_target_;
        case 10: return cd.kick_active_ && cd.kick_elapsed_ < cd.kick_target_;
        case 11: return cd.ranged_active_ && cd.ranged_elapsed_ < cd.ranged_target_;
        case 14: return cd.super_elapsed_ < cd.super_target_;
        default: return false;
    }
}

// JS `Bl.s2a()` (L588): `for each body c: c.mf = (c.mf + c.ma) * 0.5`. The
// port's solver state is `sol_ma_` (JS `Vc.ma`) / `sol_mf_` (JS `Vc.mf`),
// stride 3 (x,y,z) over the model bone count.
void Fighter::strike_midpoint_smooth() {
    if (!solver_init_ || sol_ma_.size() != sol_mf_.size()) return;
    for (std::size_t i = 0; i < sol_ma_.size(); ++i) {
        sol_mf_[i] = (sol_mf_[i] + sol_ma_[i]) * 0.5f;
    }
}

void Fighter::clear_intervals(int type, const std::string& name) {    if (current_move_ == nullptr) {
        active_intervals_.clear();
        return;
    }
    for (const Interval& iv : current_move_->intervals) {
        if (type >= 0 && iv.type != type) continue;
        if (!name.empty() && iv.name != name) continue;
        const int s = std::max(iv.start, current_move_->first_frame);
        if (s <= move_frame_ && move_frame_ <= interval_last(iv)) {
            active_intervals_.erase(iv.name.empty() ? "type" + std::to_string(iv.type) : iv.name);
        }
    }
}

// JS `wd.NS` (L506) -> `Te.Skb` (L550): start the move's clip.
//   - conditions tested by the caller (try_select_move)
//   - `Mq = a.qx` (FirstFrame) — native: move_frame = FirstFrame
//   - `Skb`'s sign `b` = `Ae.Wl` (JS `Vi.SBa` L704, `sign(To-From)` =
//     `sign(enemy_x - me_x)` for the shipped `Me -> Enemy` SetDirection) —
//     the clip MIRROR, NOT the `b6a` lock (L603, a separate term).
//   - the `b6a` facing LOCK (L603) and the `Te.FX` clip MIRROR (`hd()`,
//     L547) are SEPARATE terms ([F10] in `start_move_impl`): `FX` =
//     `sign(enemy_x - me_x)` from `Ae.Wl` (`Vi.SBa` L704).
//   - `Peb()` (L560) mirrors the clip BUFFER when `hd()` is -1 and swaps
//     the `_1`/`_2` pairs when the MirrorNode cross disagrees (Te.MYa, L566).
//
// Keys gating: the fighter's OWN context keeps `gm` true (only the AI
// move-finder `de.V1` clears it: `f.gm=!1` L601). With gm=true the Keys
// condition matches the buffered keys (`vm.he` L749:
// `(a.keys.S1||a.Wl>0?this.xn:this.TDa).$ga(a.keys)` — the move's required
// keys must be a subset of the buffered keys). So `keys_gm` is set TRUE
// here and the caller must have buffered the required keys.
bool Fighter::try_start_move(const MoveDef& move, FightContext& ctx) {
    return start_move_impl(move, ctx, /*ai=*/false);
}

// JS `de.V1` (L601-602): the candidate test. `b = this.model.me` (the move
// set), `c.gm = !1` only for the AI; the PLAYER path keeps `gm` true so the
// Keys conditions match the buffered keys (`vm.he` L749). The candidate's
// animation-name list goes into `c.xK` (`ctx.candidate_moves`) and the
// `<Conditions>` tree runs via `a.Yz(...)`.
bool Fighter::move_conditions_pass(const MoveDef& move, FightContext& ctx,
                                  std::string* trace) const {
    ctx.candidate_moves = {move.name};
    ctx.keys.clear();
    for (const auto& k : keys_) {
        ctx.keys.push_back({k.key, k.press});
    }
    ctx.keys_gm = true;
    // JS `Ae.xb`: the fighter's LIVE interval set. A move's OWN
    // `<Conditions>` is gated on `<CurrentInterval Type="..."/>` — including
    // the `Not="1"` restart guards carried by StepForward / DoubleStepForward
    // / ShortUpwardElbowStrike. `FightContext::interval_state::active` defaults
    // to false and `eval_current_interval` SKIPS inactive entries, so leaving
    // this vector empty makes every `<CurrentInterval>` base-FALSE: each
    // `Not="1"` guard then evaluates TRUE and the guard was silently OFF.
    // Mirrors the global-trigger fill at fight.cpp:791-793.
    ctx.intervals.clear();
    for (const std::string& n : intervals_at(move_frame_)) {
        ctx.intervals.push_back({n, interval_type(n), true});
    }
    return eval_move_conditions(move.conditions, ctx, trace);
}

// JS `de.V1` (L601-602): the AI tests a candidate with `Fc.gm=!1`, which
// makes every Keys condition pass (`vm.he` L749 returns true). The native
// port mirrors this with `keys_gm=false`.
bool Fighter::ai_start_move(const MoveDef& move, FightContext& ctx) {
    return start_move_impl(move, ctx, /*ai=*/true);
}

bool Fighter::start_move_impl(const MoveDef& move, FightContext& ctx, bool ai) {
    if (ai) {
        // JS `de.V1` (L601-602): the AI's candidate test evaluates ONLY the
        // move's TACTICS conditions (`a.Yz(this.model,null,a.FQ(2))` = the
        // `Ts` list). The main Conditions tree (Keys/CurrentAnimation/etc)
        // is NOT re-checked when the move starts — `Ykb` (L500) goes
        // straight to `Okb` -> `NS` -> `Skb`. The native AI path therefore
        // evaluates the tactics conditions here (the same ones `de.V1`
        // ran) and skips the input-gated main conditions.
        ctx.candidate_moves = {move.name};
        ctx.keys_gm = false;  // gm=false: every Keys condition passes
        if (!eval_move_conditions(move.tactics, ctx)) {
            return false;
        }
    } else {
        // Input path (JS `wd.NS` L506): the caller (try_select_move) has
        // already tested the main Conditions; here they are re-checked with
        // the buffered keys (gm=true).
        std::string trace;
        if (!move_conditions_pass(move, ctx, &trace)) {
            return false;
        }
    }

    // JS `Te.Skb` (L551) — the move-start (NS/Skb) transition stops the
    // ragdoll (`Nd.stop`): a new animation clip takes over from the solver.
    ragdoll_stop();
    current_move_ = &move;
    ++move_start_count_;  // JS `Te.Skb` L551 -> `x3` -> `Fu.hob()` (dW=null)
    move_frame_ = std::max(0, move.first_frame);  // JS `Mq = a.qx`
    playhead_ = 0;                                // JS `Te.Xh = 0` (Skb)
    active_intervals_.clear();
    // JS `Skb` L551: `this.cX = 2147483647` — the sentinel that makes the
    // first `vp()` (L563) see a frame change and run the action pass, so the
    // move's FIRST-frame actions fire (`Te.Lwa` L563-564).
    last_action_frame_ = -1;
    ended_move_ = nullptr;
    // [FIX Phase 4a — pacing] Subframes per clip-frame (JS `Te.Gka`:
    // `Tx = model.model.HD()`, `rpa.initialize((Ua.XJ+1)*Tx)`; `eda`
    // advances `mo` by `Tx` per step with HD()==1). MidFrames=2 -> 3.
    sub_ = std::max(1, (move.mid_frames + 1) * 1);
    subframe_ = 0;

    // Consume the buffered tap (JS `Okb` L506: `Kl.reset()` + `Kl.Ptb(a)`
    // sets the current key as the move's trigger).
    tap_age_ = 0;
    for (auto it = keys_.begin(); it != keys_.end();) {
        if (it->press == press_type::tap) it = keys_.erase(it);
        else ++it;
    }

    // Clip lookup: FileName -> anim_archive entry (JS `jc.uja` L693 loads
    // the clip by `Eza` = FileName minus ".bytes").
    if (clip_lookup_) {
        std::string clip_name = move.file_name;
        const std::string suffix = ".bytes";
        if (clip_name.size() > suffix.size() &&
            clip_name.compare(clip_name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            clip_name = clip_name.substr(0, clip_name.size() - suffix.size());
        }
        current_clip_ = clip_lookup_(clip_name);
    }
    // JS `jc.Lj` (`Vlb`/`Cdb` resolves it from the loaded clip when the move
    // carries no `EndFrame`): finish intervals whose `<Interval>` had no `End`.
    move_end_frame_ = move.end_frame != 0
                          ? move.end_frame
                          : (current_clip_ != nullptr
                                 ? static_cast<int>(current_clip_->frames.size())
                                 : 0);

    // [F10] The two terms wave 1 conflated: the `b6a` FACING LOCK and the
    // `Te.FX` CLIP MIRROR. They are different quantities and must not share
    // one variable — `b6a` is `a.ma.x-b.ma.x>=0?1:-1` (JS L603), while `FX`
    // is `rub(b)` (L547) of `b = Ae.Wl` = `Vi.SBa` (L704) = `sign(To-From)`
    // = `sign(enemy_x - me_x)` for the shipped `Me -> Enemy` SetDirection.
    //
    // Facing lock (JS `b6a` L603) — the movement/orientation term; the value
    // `facing()` reports (pose-dump `fx`).
    facing_ = (world_x_ - enemy_x_) >= 0.0f ? 1 : -1;

    // [F10] Clip mirror (JS `Te.FX` / `hd()`, L547) — the term that decides
    // the `Qeb`/`Neb` clip-buffer negation. From `Ae.Wl` (`Fa.xD` L697 →
    // `Vi.SBa` L704 `(to.OQ(a)-from.OQ(a))>=0?1:-1`) = `sign(enemy_x - me_x)`
    // for the shipped `Me -> Enemy` pair (moves.xml, e.g. StepForward L10702,
    // StaffStepForward L10911, DoubleStepForward L12508, HighPunch L15074,
    // ShortUpwardElbowStrike L15208, FistsStartStance-Left L4711). With no
    // `<SetDirection>` the JS `xD` returns the previous `hd()`, so the value
    // CARRIES OVER; the port's `MoveDef` carries no `vj.mh` presence flag
    // (the same unconditional `Me -> Enemy` form is assumed by
    // `FightController::fill_ctx_geometry` and `conditions.hpp`'s `direction`),
    // so the term is re-derived here. The `Te` ctor (L545) / `Te.reset` (L548)
    // default is +1, restored by `clear_move`/`set_model`.
    //
    // [F1] `Te.Peb` L560 -> `Te.Qeb` L550 -> `vu.Neb` L668: with `FX == -1`
    // the clip BUFFER x is negated about clip-space 0, from slot `jW?2:0` up.
    // `jW` is true whenever `qrb` seeded the two prepend slots (`vu.Cbb` L667
    // sets `this.jW=!0`), i.e. whenever the move does NOT carry
    // `NoInterpolationFrames`; in the `Pka`-prepend case (`no_interp`,
    // `jW==false`) the negation starts at slot 0 and covers the prepend too.
    clip_mirror_ = (enemy_x_ - world_x_) >= 0.0f ? 1 : -1;
    mirror_x_ = clip_mirror_ < 0;
    mirror_prepend_ = mirror_x_ && move.no_interp;

    // [F3] JS `Te.Peb` L560: `this.rw = Te.MYa(this.model, this.Ua, this.hd(),
    // this.jc.Kh(2).data)` — decided ONCE here (the buffer has already been
    // negated by `Qeb`) and then applied to the WHOLE buffer by `Ua.Oeb`
    // (L692). Two JS gates the old port dropped:
    //   - `!this.Ua.J2.Vj` — the move must carry a `<MirrorNode>`
    //     (`Ou.Grb` L702: empty/absent -> `Vj=true` -> `MYa` never runs);
    //   - `Ic(J2.qq.key)` and its `NE` partner must both resolve.
    // `lwa(a,b,c,d)` (L566): `if(c==-1){a<->b}` first, then
    // `(a.ma.x>=b.ma.x) != (d[a.id].x>=d[b.id].x)` — the POSED node order
    // (`ma` = last frame's node x) against the MIRRORED slot-2 clip order.
    mirror_swap_ = false;
    if (!move.mirror_node.empty() && current_clip_ != nullptr &&
        !current_clip_->frames.empty() &&
        pos_.size() == model_.bones.size() * 2) {
        const int ni_a = model_.bone_by_name(move.mirror_node);
        // `Ou.Grb` L702: the partner name flips the TRAILING digit
        // (`nf(a,len-1,1)` = a minus its last char, + ("2"|"1")) —
        // `NHeel_1` -> `NHeel_2`.
        std::string partner = move.mirror_node;
        partner.back() = (partner.back() == '1') ? '2' : '1';
        const int ni_b = model_.bone_by_name(partner);
        if (ni_a >= 0 && ni_b >= 0) {
            const std::size_t ref = static_cast<std::size_t>(std::max(
                0, std::min(move.first_frame,
                            static_cast<int>(current_clip_->frames.size()) - 1)));
            const auto& rb = current_clip_->frames[ref].bones;
            const std::size_t zclip = std::min(rb.size(), model_.bones.size());
            int ia = ni_a, ib = ni_b;
            // [F10] `lwa`'s 3rd argument is `this.hd()` (`Peb` L560 =
            // `Te.MYa(this.model, this.Ua, this.hd(), …)`) = the CLIP MIRROR,
            // not the `b6a` lock.
            if (clip_mirror_ == -1) std::swap(ia, ib);  // `lwa` swaps the operands
            if (static_cast<std::size_t>(ia) < zclip &&
                static_cast<std::size_t>(ib) < zclip) {
                // `Neb` already ran -> the buffer x is mirrored. `ma` (the
                // previous frame's node x) shares one placement offset, so the
                // order comparison is done on `pos_` (world) exactly like JS.
                const float neg = mirror_x_ ? -1.0f : 1.0f;
                const bool have_pose = pose_sampled_;
                const float pa = have_pose
                                     ? pos_[static_cast<std::size_t>(ia) * 2]
                                     : model_.bones[static_cast<std::size_t>(ia)].x;
                const float pb = have_pose
                                     ? pos_[static_cast<std::size_t>(ib) * 2]
                                     : model_.bones[static_cast<std::size_t>(ib)].x;
                const float ba = neg * rb[static_cast<std::size_t>(ia)].x;
                const float bb = neg * rb[static_cast<std::size_t>(ib)].x;
                mirror_swap_ = ((pa >= pb) != (ba >= bb));
            }
        }
    }

    // JS `Te.Skb` (L551) runs `Gub()` (align, L557-559) right after loading
    // the clip (and after `Peb`) and before the first `ia()` sample.
    compute_align(move);

    // [FIX root motion — JS `Skb` L551-552] Seed the authored root-motion
    // state exactly as the JS controller does when a move starts:
    //   `a=this.j8; a.x=0` (always reset the applied offset),
    //   `this.Ua.qta || (this.DM = Qfa() = wua; this.DM.x*=hd())`
    //     (DM = <Velocity X/Y/Z>, x mirrored by facing; kept when
    //      SaveVelocity/`qta` is set),
    //   `this.aV = t9a() = Coa; this.aV.x*=hd()`
    //     (aV = <Velocity Ax/Ay/Az>, x mirrored by facing — always).
    // Reference: `Fa.ykb` L721-722 (parse), `Te.Qfa`/`Te.t9a` L699,
    // `Te.jub` L722 (`SaveVelocity` -> `qta`). The multiplier is the `Skb`
    // sign `b` = the CLIP MIRROR (`hd()`), not the `b6a` lock.
    const float fsign = clip_mirror_ < 0 ? -1.0f : 1.0f;
    if (move.velocity.has_velocity) {
        root_active_ = true;
        if (!move.velocity.save_velocity) {
            root_dm_x_ = move.velocity.x * fsign;
        }
        root_av_x_ = move.velocity.ax * fsign;
    } else {
        root_active_ = false;
        root_dm_x_ = 0.0f;
        root_av_x_ = 0.0f;
    }
    j8_x_ = 0.0f;  // JS `Skb` L551: `a=this.j8; a.x=0`
    // [FIX render anchor — JS `Te.Gub` L557-559 + `Te.Gla` L550] `compute_align`
    // above just used `sol_ma_[<Align><Pivot Part>]` as the continuity target
    // (`e = currentNode.ma`), so capture the PREVIOUS frame's rendered world x
    // of that Part HERE, before `translate_solver_state` rewrites the persisted
    // state. `sample()` renders clip bones as
    //   pos_[p] = (sol_ma_[p] - sol_ma_[anchor]) * facing + world_x_
    // so that Part's last world x is exactly this expression. `sample()` then
    // pins `world_x_` (the NPivot anchor = JS `Dl.Fe()`) from it, letting
    // NPivot ride the clip — exactly like the JS. Without an align (JS
    // `Gla(0,0,0)`, no shift) the anchor stays where it was.
    align_pivot_u_ = -1;
    render_offset_valid_ = true;  // default: anchor-continuous (no align)
    // [B1 FIX vertical anchor] No align / no Y axis -> the JS `Gla` y shift is
    // `eja` (`ShiftY`, 0 shipped), so the whole clip's own y stands. Without
    // this the old code pinned the model to the spawn y and dropped the clip's
    // vertical root travel (the "floating / twitching legs" report).
    render_offset_y_ = 0.0f;
    if (move.align.has_align) {
        const int pv = model_.bone_by_name(move.align.pivot_part);
        const int an = model_.bone_by_name(fighter_pivot_bone());
        // [F1/F3] The `<Align><Pivot Part>` node the JS actually anchors on is
        // the POST-`Peb` one (`os`, i.e. `NQ(UE)` when `rw`) — see compute_align.
        const int aur = align_ref_u_ >= 0 ? align_ref_u_ : pv;
        if (aur >= 0 && an >= 0 && solver_init_ &&
            sol_ma_.size() == model_.bones.size() * 3) {
            const std::size_t pu = static_cast<std::size_t>(aur);
            const std::size_t au = static_cast<std::size_t>(an);
            // [F1] The previous WORLD x of the `<Align><Pivot Part>` node.
            // `pos_[pu] - pos_[au] == px[pu] - px[au]` (the placement offset
            // cancels) and `sol_ma_` is that same clip-space state, so the
            // clip-space difference maps to world 1:1 — NO facing factor is
            // involved any more (the mirror now lives INSIDE `px`). The old
            // `* fsign` belonged to the pre-F1 placement
            // `pos_ = (px[i]-px[anchor])*f + x`, where the world difference was
            // `(px[pu]-px[au])*f`; keeping it after moving the mirror into the
            // buffer negated the whole `(sol_ma_[pu]-sol_ma_[au])` term and
            // threw the anchor 2x that offset off the spawn.
            prev_align_pivot_world_x_ =
                (sol_ma_[pu * 3] - sol_ma_[au * 3]) + world_x_;
            // [B1 FIX vertical anchor] `Gub` (L559) computes the SAME world
            // delta on the y axis: `f = currentNode.ma.y (+ a.eja)`,
            // `c.y = f - d.y`; `Gla` then shifts the buffer y by `Fk.y` when
            // the `<Position>` declares the Y axis (`a.dI`), else by `ShiftY`.
            prev_align_pivot_world_y_ =
                (sol_ma_[pu * 3 + 1] - sol_ma_[au * 3 + 1]) + world_y_;
            align_pivot_u_ = aur;
            // JS `Gub` (L559) reads `d` from the RAW buffer (`jc.Kh(2)` =
            // clip[FirstFrame]) and `e` from the posed node (`currentNode.ma`)
            // BEFORE the first `eda`, i.e. exactly this `sol_ma_` state. So
            // the align constant maps `sol_ma_` (this clip space) to world:
            //   world_x = px[anchor] + (prev_world(Part) - sol_ma_[Part])
            // using the PRE-sample `sol_ma_`, not the first interpolated
            // sample (the buffer's slot-2 reference is the raw clip frame).
            render_offset_ = prev_align_pivot_world_x_ - sol_ma_[pu * 3];
            // [B1 FIX vertical anchor] JS `Gla(a.dI ? Fk.y : a.eja)` (L559):
            // the y shift is the world delta only when the Y axis is an align
            // axis; otherwise it is `ShiftY` (0 shipped).
            render_offset_y_ = move.align.axis_y
                                   ? (prev_align_pivot_world_y_ - sol_ma_[pu * 3 + 1])
                                   : move.align.shift_y;
            render_offset_valid_ = true;
        }
    }
    // JS `Te.Skb` order: the play buffer prepend (`Pka`/`qrb`) is built
    // BEFORE the first `eda` sample and after `Gub` (align).
    //
    // [FIX root-motion - no cross-clip state translation] `compute_align`
    // above already re-expressed the NEW clip into the OLD pose space: the
    // align shift `Fk = posed_prev[Pivot Part] - raw_new[Pivot Part]` is added
    // to every clip bone (`Gla` -> `vu.shift`, applied inside `ctl`), so the
    // new clip Pivot Part lands exactly on the previous pose (`Te.Gub`
    // L557-559). The solver/persisted state `sol_ma_` therefore already lives
    // in that same continuous space - it is the previous frame posed `px`.
    // The old port additionally translated the whole solver state by
    // `tx - sol_prev_com_` (the previous move align-node travel), which
    // double-applied that travel: `qrb` (`build_prepend`) then froze slots
    // 0/1 from the shifted state, so the first ~5 frames of every new clip
    // blended toward a pose one move root travel away and the fighter
    // visibly snapped/slid back at each move->idle boundary. The JS has NO
    // such translation (see the solver-seed note in `set_model`).
    build_prepend(move);
    sample_current();
    return true;
}

// The PLAYER's move start — the JS `Gc.DK` `c == false` branch (L673-674),
// NOT the tactic roulette. Full hop table in `fighter.hpp`.
//
//   candidates: `Gc.EZa` (L676) walks `d.Su.dea(c.type)` with `c.type == 2`
//     (`Gc.mS` L672 <- `wd.BHa` L507 <- `zl.rwa` L799 <- `zl.Sgb` L798 <-
//     `wd.yJa` L501 <- `ca.N0a` L426 `this.eu==2 && b.yJa(a)`). `ru.FT`
//     (L538) files a move under type 2 iff its `<Events>` contains
//     `<KeyPressed/>` (the `1key` Template -> `Controlled`, moves.xml), and
//     `EZa` then re-tests the move's own `<Conditions>` via `f.Yz(b,null,g)`
//     (L677) — the `<Keys>` gate lives there.
//   `DK(a,b,false)`: `d` = {eb && !Rha} = EMPTY because `Gc.mS` /
//     `Gc.Ih(2,a)` leaves `eb` false, so `d.length>0` is false and `Pkb`
//     never runs; therefore NO `M7.Wcb` mirror filter, NO `va.Ts`
//     <Tactics><Conditions> filter and NO `this.jL` -> `de.jL` -> `Md.jL`
//     weighted roulette (all of them live inside `Pkb`, L674-676).
//   `DK` else branch: `f` = the `Aua` max-`priority` group of the non-`Rha`
//     candidates; `g` = the same for the `Rha` ones; `g.length>0 &&
//     a.Ukb(g[sja].animation)` (L674) only parks the name in `wd.P9` (no
//     clip); `e = f[uf.sja(f.length)]` (L674) — a UNIFORM pick from
//     `Math.random` (`uf.sja` L115 = `floor(uf.OKa.RGa()*n)`,
//     `uf.OKa.RGa() = Math.random`, L114/L2471) — then
//     `Gc.Nsb(a, this.Ek[e.index], e.animation, e.sign)` (L674) ->
//     `wd.fJa` (L506) -> `Ml` -> `wd.Bnb` (L507) -> `wd.NS` (L505) ->
//     `Te.Skb` (L550).
//
// PRECONDITION (JS `wd.BHa` <- `zl.rwa`): a press EDGE, i.e. a Tap in the
// buffer. A lingering Hold is a continuation for the running move's
// conditions, not a new press.
std::string Fighter::try_select_move(FightContext& ctx) {
    decision_ = MoveDecision();  // the previous decision is stale from here on
    bool has_tap = false;
    for (const auto& k : keys_) {
        if (k.press == press_type::tap) {
            has_tap = true;
            break;
        }
    }
    if (!has_tap) return "";

    // `Gc.EZa` (L676) candidates, in the `hb_` (JS `ra.Lk` document) order
    // that `ru.iQ` is filed in (`Su.FT` L538 pushes in `me` order, and `me`
    // is built from `ra.Lk` in order):
    //   * `m->has_event("KeyPressed")` = `ru.dea(2)` membership
    //     (`d.va.Hc` type 2), the `<Events><KeyPressed/>` marker;
    //   * `move_conditions_pass` = `f.Yz(b,null,g)` (L677), the move's own
    //     `<Conditions>` tree with `gm` TRUE (the Keys condition really
    //     matches the buffered keys — the AI's `de.V1` L601 clears `gm`).
    std::vector<const MoveDef*> passing;
    for (const MoveDef* m : hb_) {
        if (m == nullptr) continue;
        if (!m->has_event("KeyPressed")) continue;
        std::string trace;
        // [TASK B DIAGNOSTIC] `SF2_TRACE_COND=1` dumps the failing condition
        // tree + the enemy interval list for the throw family so the exact
        // gate is visible (no effect when the env var is unset).
        static const bool trace_cond = std::getenv("SF2_TRACE_COND") != nullptr;
        const bool pass = move_conditions_pass(*m, ctx, trace_cond ? &trace : nullptr);
        if (!pass) {
            if (trace_cond &&
                (m->name.rfind("Throw", 0) == 0 || m->name == "HighPunch" ||
                 m->name == "StepForward" || m->name == "ShortUpwardElbowStrike")) {
                std::fprintf(stdout, "[cond] %s FAIL cur=%s@%d enemy_intervals=[",
                             m->name.c_str(),
                             current_move_ != nullptr ? current_move_->name.c_str() : "",
                             move_frame_);
                for (const FightContext::interval_state& iv : ctx.intervals_enemy) {
                    std::fprintf(stdout, "%s/t%d%s ", iv.name.c_str(), iv.type,
                                 iv.active ? "" : "!");
                }
                std::fprintf(stdout, "] me=%.1f en=%.1f dist=%.1f dir=%.0f\n%s",
                             ctx.me_x, ctx.enemy_x, ctx.dist_x, ctx.direction,
                             trace.c_str());
                std::fflush(stdout);
            }
            continue;
        }
        passing.push_back(m);
    }
    if (passing.empty()) return "";

    // `Aua(h, h.animation.Rha ? g : f)` (L673): keep only the max-`priority`
    // group of each list (`ap>=bp && (ap>bp && b.length=0, b.push(a))`).
    // `Rha` = `<NoAnimation>` (MoveDef::no_animation).
    std::vector<const MoveDef*> f;  // non-Rha candidates
    std::vector<const MoveDef*> g;  // `Rha` candidates
    auto aua = [](std::vector<const MoveDef*>& b, const MoveDef* m) {
        const int ap = m->priority;
        const int bp = b.empty() ? 0 : b.front()->priority;
        if (ap >= bp) {
            if (ap > bp) b.clear();
            b.push_back(m);
        }
    };
    for (const MoveDef* m : passing) {
        if (m->no_animation) {
            aua(g, m);
        } else {
            aua(f, m);
        }
    }

    decision_.valid = true;
    decision_.cands.clear();
    decision_.cands.reserve(passing.size());
    for (const MoveDef* m : passing) {
        decision_.cands.emplace_back(m->name, m->priority);
    }
    decision_.f_group.clear();
    decision_.f_group.reserve(f.size());
    for (const MoveDef* m : f) decision_.f_group.push_back(m->name);
    // `g.length>0 && a.Ukb(g[sja].animation)` (L674) — `wd.Ukb` (L506) only
    // stores into `P9`; `wd.Mnb` (L507) clears it on the next `ia`, so no
    // clip starts from the `Rha` group. Recorded for the probe only.
    if (!g.empty()) {
        std::size_t gi = 0;
        if (g.size() > 1 && math_random_) {
            float r = math_random_();
            if (r < 0.0f) r = 0.0f;
            if (r >= 1.0f) r = 0.9999999f;
            gi = static_cast<std::size_t>(r * static_cast<float>(g.size()));
            if (gi >= g.size()) gi = g.size() - 1;
        }
        const char* gi_name = g[gi]->name.c_str();
        decision_.ukb = gi_name;
        decision_.ukb_set = true;
    }
    // `e = f[uf.sja(f.length)]` (L674). `|f| <= 1` needs no draw:
    // `floor(r*1) == 0` for every `r`, so the pick is value-exact without
    // consuming an `uf.OKa.RGa()` tick.
    if (f.empty()) return "";  // `e == null` -> nothing fires
    std::size_t idx = 0;
    if (f.size() > 1 && math_random_) {
        float r = math_random_();
        if (r < 0.0f) r = 0.0f;
        if (r >= 1.0f) r = 0.9999999f;
        idx = static_cast<std::size_t>(r * static_cast<float>(f.size()));
        if (idx >= f.size()) idx = f.size() - 1;
        decision_.drew = true;
        decision_.draw = r;
    }
    decision_.index = static_cast<int>(idx);
    // `e.animation.MS ? a.jJa(...) : Gc.Nsb(a, Ek[e.index], e.animation, e.sign)`
    // (L674). `jJa`/`Nsb` both end at `Te.Skb` (the `qs`/`Ml` queues are a
    // port detail); `try_start_move` is the shared `NS`/`Skb` entry.
    // Should the re-tested Conditions no longer hold, fall through the
    // remaining candidates in order so the port never stalls on a stale pick.
    for (std::size_t k = 0; k < f.size(); ++k) {
        const MoveDef* m = f[(idx + k) % f.size()];
        if (try_start_move(*m, ctx)) {
            decision_.picked = m->name;
            return m->name;
        }
    }
    return "";
}

// Hit-reaction pick (JS `Gc.DK` L343452 + `Aua` L343447 + `uf.sja` L57426;
// see fighter.hpp for the roulette caveat).
//
// `DK(a,b,c)`: partition the reaction set into `f` (Rha==false) / `g`
// (Rha==true) with `Aua` — which keeps only the HIGHEST-`priority` group
// (`c>=d && (c>d && b.length=0, b.push(a))`) — then
//   `f.length>0 && (e = f[uf.sja(f.length)])`   // UNIFORM pick via Math.random
//   `g.length>0 && a.Ukb(g[uf.sja(g.length)].animation)`
// `uf.sja(n)` = `Math.floor(Math.random()*n)` (L57426 -> `at.RGa` ->
// `Math.random`). The JS later feeds `e` into the `Pkb` weighted roulette
// (tactic weights at reaction time are not ported — documented OPEN), so the
// port starts the uniformly picked move directly. `rng` is the injected
// `Math.random` analog (`FightController::math_random01`), never `Da.pg`.
std::string Fighter::try_react(FightContext& ctx, bool prefer_fall,
                               const std::function<float()>& rng) {
    // Build the candidate list: moves with a Hit event (JS `b` = the
    // candidate reactions), split by the Fall preference (`Ub`/MS proxy).
    std::vector<const MoveDef*> cands;
    for (const MoveDef* m : hb_) {
        if (m == nullptr || !m->has_event("Hit")) continue;
        // A hit reaction is the `Recoil|...|Hit` family. The Titan boss's
        // `TitanBlock` (`moves.xml` Template="Block|Hit", Priority 720, no
        // <Locks> - universal, so it sits in EVERY fighter's list) would
        // otherwise win the max-`priority` partition for BOTH fighters and
        // play `titan_block.bytes` - a clip authored for the Titan skeleton -
        // on a humanoid mesh (corrupted pose, no visible reaction). A block
        // is never a hit reaction, so the `Block` tag is excluded here.
        if (m->template_tags.count("Block") != 0) continue;
        const bool is_fall = m->name.find("Fall") != std::string::npos;
        if (prefer_fall != is_fall) continue;
        cands.push_back(m);
    }
    if (cands.empty() && prefer_fall) {
        // The shock knockdown had no *Fall* candidate: fall back to the
        // non-Fall reaction set (the JS `g`-empty path).
        return try_react(ctx, false, rng);
    }
    // `Aua` (L343447): keep only the max-`priority` group.
    std::vector<const MoveDef*> top;
    for (const MoveDef* m : cands) {
        // `Aua` partitions by `Rha` (NoAnimation) — false for all shipped
        // moves, so every candidate lands in the non-Rha `f` group.
        const int c = m->priority;
        const int d = top.empty() ? 0 : top.front()->priority;
        if (c >= d) {
            if (c > d) top.clear();
            top.push_back(m);
        }
    }
    if (top.empty()) return "";
    // `uf.sja(f.length)`: uniform index. JS `Math.random`; the port's
    // injected `math_random01()` keeps it deterministic AND off `Da.pg`.
    std::size_t idx = 0;
    if (top.size() > 1 && rng) {
        float r = rng();
        if (r < 0.0f) r = 0.0f;
        if (r >= 1.0f) r = 0.9999999f;
        idx = static_cast<std::size_t>(r * static_cast<float>(top.size()));
        if (idx >= top.size()) idx = top.size() - 1;
    }
    // JS `e.animation.MS ? a.jJa(...) : Nsb(...)` — start the picked move
    // (the first candidate whose conditions pass, in the same order).
    for (std::size_t k = 0; k < top.size(); ++k) {
        const MoveDef* m = top[(idx + k) % top.size()];
        if (ai_start_move(*m, ctx)) return m->name;
    }
    return "";
}

// JS `Te.ia` (L547-548): each 60 Hz update advances `Xh` (playback frame)
// and `fG` (physics frame); when `Xh+2 >= clipLen` the clip ends (`KNa()`
// + lS -> EStopAnimationEvent) and the fighter returns to idle.
// JS `vp` (L562-563) -> `rrb` (L552) -> `jc.c7a` (L691) recomputes the
// active intervals each frame.
//
// [FIX Phase 4a — pacing] The game does NOT play 1 clip-frame per 60 Hz
// step. `Te.Gka` (L285802) sets `Tx = model.model.HD()` (=1) and the
// interpolator `rpa` is initialized with `(Ua.XJ+1)*Tx` subframes per
// clip-frame; `Te.eda` (L282908) advances the subframe index `mo` by
// `Tx` per step. With `MidFrames=XJ=2` (moves.xml: `MidFrames="2"` on the
// fists/stance moves) that is (XJ+1)=3 subframes per clip-frame, i.e. the
// anim lasts (clipLen - FirstFrame)*3 + 1 fight-frames. Evidence (oracle
// reference/traces/console.log, real game at 60 Hz):
//   FistsStartStance-Left: clip stance_1 = 46 frames, FirstFrame=2
//     -> trace F2..F134 = 133 frames = (46-2)*3+1            [exact]
//   HighKick: clip high_kick = 21 frames, FirstFrame=3
//     -> trace F336..F390 = 55 frames = (21-3)*3+1           [exact]
// The old code advanced move_frame_ 1 per step => 3x TOO FAST. The native
// now advances a subframe counter and samples the interpolated pose.
void Fighter::advance(float dt) {
    (void)dt;  // fixed 60 Hz - one frame per call (JS 1/60 step)
    // Frame-action drain: the actions collected by the sub-steps of THIS
    // advance() are what the caller dispatches (JS `Te.Lwa` L563-564 runs
    // inside `Te.ia`, i.e. once per frame advance).
    frame_actions_.clear();
    // Knockback offsets decay every tick (JS Vc.sk friction - OPEN rate).
    if (!kb_.empty()) sf2::scene::decay_knockback(kb_);
    // Timescale steps (SlowModel KT): scale>=1 verbatim (Speed<1 no-ops
    // at apply); fractional remainder carries to the next tick.
    scale_acc_ += time_scale_;
    int steps = static_cast<int>(scale_acc_);
    if (steps < 1) steps = 1;
    if (steps > 4) steps = 4;
    scale_acc_ -= static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) advance_step();
}

void Fighter::advance_step() {
    if (current_move_ == nullptr || current_clip_ == nullptr) {
        return;
    }

    const int clip_len = static_cast<int>(current_clip_->frames.size());
    if (clip_len <= 0) {
        return;
    }

    // Subframes per clip-frame: (XJ+1)*HD with HD=1 (JS `Gka` +
    // `eda`: `mo += Tx`, `Tx = (Ua.XJ+1)`). MidFrames=2 -> 3.
    const int sub = std::max(1, (current_move_->mid_frames + 1) * 1);

    // Clip frame (JS `Te.M0()`: `(Xh<=2?0:Xh-2)+Mq`): playback slot
    // `playhead_` maps to clip frame `FirstFrame + max(0, Xh-2)`. `move_frame_`
    // stays this CLIP frame for the interval/cf consumers, while `sample()`
    // reads the play buffer (prepend slots 0/1 then clip[FirstFrame..]).
    const int ff = std::max(0, current_move_->first_frame);
    move_frame_ = ff + std::max(0, playhead_ - 2);

    // Interval update: active when max(start,qx) <= frame <= min(finish,Lj).
    active_intervals_.clear();
    for (const std::string& n : intervals_at(move_frame_)) {
        active_intervals_.insert(n);
    }

    // Root motion (JS `Te.eda` L556 + `Te.j8`/`DM`/`aV` L546/564).
    //
    //  (a) AUTHORED <Velocity> (JS `Fa.ykb` L721-722; `Skb` L551-552 seeds
    //      `DM`=`wua`, `aV`=`Coa`). `eda` L556 runs `Pab` (`Qab(DM)`:
    //      `j8 += DM*sG`) at frame start and `Nab` (`Oab(aV)`:
    //      `DM += aV*sG`) at frame end; the `j8` offset is added to EVERY
    //      posed bone (`d.x+=c.x; d.y+=c.y; d.z+=c.z`) — i.e. the whole
    //      fighter shifts. `sG = 1/Tx`, `Tx = model.model.HD()` (`Gka`
    //      L561) = 1.
    //  (b) NO <Velocity>: `wua`/`Coa` = 0, so `DM`/`aV`/`j8` stay 0 (JS) and
    //      the ONLY placement is the clip itself, read by the render anchor
    //      in `sample()` (NPivot's interpolated clip x + the align constant).
    // NOTE: every shipped fighter/locomotion move is (b) — all 62 live
    // <Velocity> elements are on projectile/magic moves (summary).
    constexpr float kSG = 1.0f;  // JS `Gka` L561: sG = 1/Tx, Tx = HD() = 1
    // [FIX render anchor] The whole-pose placement is now driven by the render
    // anchor in `sample()` (the clip-interpolated NPivot x + the align
    // constant), so here only the authored `<Velocity>` root motion is
    // integrated: `Pab`/`Qab` (JS L564) `j8 += DM*sG` at frame start; the
    // `Nab`/`Oab` half (`DM += aV*sG`) runs AFTER the sample, per `eda` L556.
    // The old raw bone-0 clip-delta accumulation is removed: NPivot (the JS
    // `Dl.Fe()` anchor), not bone 0, is the node whose swing the JS applies,
    // and accumulating the wrong node's delta drifted the fighter ~19 world
    // units at the intro stance idle start.
    if (root_active_) {
        j8_x_ += root_dm_x_ * kSG;  // `Pab`/`Qab`: j8 += DM*sG
    }

    // Clip end (JS `Te.ia` L547-548: `Xh+2 >= vu.J$a()` -> KNa + lS + Sca).
    // `vu.J$a()` = the play buffer size = 2 prepended slots + the playable
    // range [FirstFrame..len-1], i.e. `clip_len - ff + 2`. The buffer is
    // sampled at `Xh = 0 .. (clip_len - ff - 1)` (the first two `Xh` ranges
    // read the prepend slots — JS `vu.Pka` L340543 / `Te.qrb` L282683), each
    // at `sub` subframes.
    if (playhead_ >= clip_len - ff) {
        // The clip has fully played (the last play buffer range's subframes).
        // JS `Te.lS` L553 (`gh("EStopAnimationEvent", Ua)`) -> `wd.eIa` ->
        // `Gc.kg` L671 `Ih(10,..)` -> `Gnb` L672 `CZa(10)` reads `this.Ua`,
        // which `KNa` did NOT clear. Hand the ended move to the caller for
        // the `AnimationEnd` action pass.
        ended_move_ = current_move_;
        current_move_ = nullptr;
        current_clip_ = nullptr;
        active_intervals_.clear();
        last_action_frame_ = -1;
        subframe_ = 0;
        playhead_ = 0;
        prepend_.clear();
        align_x_ = align_y_ = align_z_ = 0.0f;
        // JS `stop()`/`KNa()` call `jc.reset()`; `Skb` L551 zeroes `j8`.
        root_active_ = false;
        root_dm_x_ = root_av_x_ = 0.0f;
        j8_x_ = 0.0f;
        render_offset_ = 0.0f;
        render_offset_y_ = 0.0f;
        render_offset_valid_ = true;
        align_pivot_u_ = -1;
        return;
    }

    ++subframe_;
    if (subframe_ >= sub) {
        subframe_ = 0;
        ++playhead_;  // JS `Xh++` (once per `sub` steps)
    }
    move_frame_ = ff + std::max(0, playhead_ - 2);
    sample_current();
    // JS `Te.ia` L547-548 order: `eda()` (the pose apply) THEN `vp()`
    // (L563) which detects the frame change (`a != this.cX`) and runs
    // `rrb()` + the action pass `Lwa()` (L563-564:
    // `e.$eb(b, this.Ua, ...) && c.push(e)` with `b = this.ip()` = the clip
    // frame `move_frame_`). Collect once per frame change; the caller
    // dispatches them (`gh("EActionStart", c)` L564).
    if (current_move_ != nullptr && move_frame_ != last_action_frame_) {
        last_action_frame_ = move_frame_;
        for (const MoveAction& act : current_move_->actions) {
            if (act.frame_trigger && act.frame == move_frame_) {
                frame_actions_.push_back(&act);
            }
        }
    }
    // JS `Te.eda` L556 frame END: `Nab`/`Oab(aV)` -> `DM += aV*sG` (only the
    // authored `<Velocity>` path carries a non-zero `aV`).
    if (root_active_) {
        root_dm_x_ += root_av_x_ * kSG;
    }
}

// JS `Te.Lwa` (L563-564) drain: the frame actions collected by the last
// `advance()`. `gh("EActionStart", c)` L530 -> `wd.mHa` L530 -> `wd.BNa`
// L523 -> each action's `Uh(this)`.
const std::vector<const MoveAction*>& Fighter::take_frame_actions() {
    return frame_actions_;
}

// JS `Te.CZa(a)` (L555): every action of the CURRENT move whose trigger is
// the event `a` (`cb.afb` L724: `zy.Z5 == 1 && zy.event == a`).
std::vector<const MoveAction*> Fighter::move_actions_for_event(
    const std::string& event) const {
    std::vector<const MoveAction*> out;
    if (current_move_ == nullptr) return out;
    for (const MoveAction& act : current_move_->actions) {
        if (!act.frame_trigger && act.event == event) out.push_back(&act);
    }
    return out;
}

// The move whose clip ended during the last `advance()` (JS `KNa` keeps
// `Ua`; `Gnb` L672 then runs `CZa(10)` on it). One-shot.
const MoveDef* Fighter::take_ended_move() {
    const MoveDef* m = ended_move_;
    ended_move_ = nullptr;
    return m;
}

void Fighter::sample_current() {
    if (current_clip_ != nullptr) {
        // [F10] The `sample()` mirror argument is the CLIP MIRROR (`Te.FX` /
        // `hd()`), not the `b6a` facing lock.
        sample(*current_clip_, move_frame_, world_x_, world_y_, clip_mirror_,
               /*interp=*/true, current_move_ != nullptr ? current_move_->first_frame : 0,
               playhead_);
    }
}

void Fighter::clear_move() {
    current_move_ = nullptr;
    current_clip_ = nullptr;
    move_end_frame_ = 0;
    active_intervals_.clear();
    subframe_ = 0;
    playhead_ = 0;
    prepend_.clear();
    // The action sentinels are per-move (JS `Te.reset` L548 clears `Ua`).
    last_action_frame_ = -1;
    ended_move_ = nullptr;
    frame_actions_.clear();
    align_x_ = align_y_ = align_z_ = 0.0f;
    // JS `stop()` -> `jc.reset()` / `Skb` L551: the authored root state is
    // per-move; clear it so a later move starts from a zero `j8`.
    root_active_ = false;
    root_dm_x_ = root_av_x_ = 0.0f;
    j8_x_ = 0.0f;
    render_offset_ = 0.0f;
    render_offset_y_ = 0.0f;
    render_offset_valid_ = true;
    align_pivot_u_ = -1;
    // JS `stop()`/`jc.reset()` drops the per-clip mirror decision too;
    // `Te.reset` L548 also restores the ctor default `FX = 1`.
    mirror_swap_ = false;
    mirror_x_ = false;
    mirror_prepend_ = false;
    clip_mirror_ = 1;
}

// JS `Dl.NQ` (L575): `for(d in this.Wf.b3){if(a==d.first)return d.second;
// if(a==d.second)return d.first} return -1` — the mirror partner of bone `i`.
int Fighter::mirror_partner(int i) const {
    for (const auto& pr : mirror_pairs_) {
        if (pr.first == i) return pr.second;
        if (pr.second == i) return pr.first;
    }
    return -1;
}

// JS `Ua.Oeb` (L692): when `rw` is set, `b.Kh(l).data[a.first] <-> data[a.second]`
// for every slot `l >= 2` and every pair with BOTH ids `< b.Kh(2).size`. So the
// value sampled for bone `i` is the buffered value of its partner.
int Fighter::mirror_swap_src(int i, std::size_t zclip) const {
    if (!mirror_swap_) return i;
    const int j = mirror_partner(i);
    if (j < 0 || static_cast<std::size_t>(j) >= zclip) return i;
    return j;
}

// [FIX root-motion align] JS `Te.Gub` (L557-559) + `Te.Gla` (L550):
// compute the move's <Align> offset and store it as a clip-buffer shift
// (`Gla` -> `jc.shift` L550). Called once at clip start — JS `Skb` (L551)
// runs `Gub()` right after loading the clip, before the first `ia()`.
// Object mapping (JS `Fa.jva` L719-720): Pivot Object = `VE`, Position
// Object = `JK`; the axis flags (`cI`=X, `dI`=Y, `MY`=Z) select which
// components shift (`Gla(cI?Fk.x:dja, dI?Fk.y:eja, MY?Fk.z:0)`).
//
// The shipped Fists stance moves are
// `<Pivot Object="Nodes" Part="NHeel_2"/><Position Object="Pivot" ShiftX=..>`
// -> d = the clip's NHeel_2 at FirstFrame (`jc.Kh(2)`), e = the posed
// NHeel_2 (`currentNode.ma`) + facing*ShiftX, so the clip is shifted to
// keep the pivot bone where the previous pose left it.
void Fighter::compute_align(const MoveDef& move) {
    align_x_ = align_y_ = align_z_ = 0.0f;
    if (!move.align.has_align || current_clip_ == nullptr ||
        current_clip_->frames.empty()) {
        return;
    }
    const Align& al = move.align;
    const std::size_t n = model_.bones.size();
    // JS `Gub` L558/559 switch on the resolved `VE`/`JK` enum.
    const auto map_object = [](const std::string& s) -> int {
        if (s == "Nodes") return 1;      // EObjectNodes
        if (s == "Animation") return 2;  // EObjectAnimation
        if (s == "Wall") return 3;       // EObjectWall
        if (s == "Pivot") return 4;      // EObjectPivot
        return 0;                        // EObjectNone
    };
    const int ve = map_object(al.pivot_object);
    const int jk = map_object(al.pos_object);

    // Reference frame = FirstFrame (JS `Pka(jc, Mq)` loads it before `Gub`;
    // `this.jc.Kh(2)` is that buffer, `Kh(2).data[node]` = the raw clip pos).
    const std::size_t f0 = static_cast<std::size_t>(
        std::max(0, std::min(move.first_frame,
                             static_cast<int>(current_clip_->frames.size()) - 1)));
    const auto& fb = current_clip_->frames[f0].bones;
    const int pivot_idx = model_.bone_by_name(al.pivot_part);  // UE / `this.os`
    // [F10] `Gub` L559's `e += this.hd()*a.dja` — `hd()` is the CLIP MIRROR
    // (`Te.FX`), the same term `Qeb`/`Neb` used on the buffer above (the JS
    // order is `Peb(); Gub();`, so `Gub` reads the mirrored buffer).
    const float f = clip_mirror_ < 0 ? -1.0f : 1.0f;
    // [F1/F3] `Te.Skb` L551 order is `Mkb(); Mqb(); Peb(); Gub();`. `Mqb`
    // (L563) sets `os = align.UE` / `currentNode = Va.all[os]`; `Peb` (L560)
    // then — when `rw` — sets `os = model.NQ(os)` and `currentNode` to that
    // partner. `Gub` therefore reads BOTH sides at the POST-SWAP node:
    //   d (L558): `b = b.rw && a.Tia > -1 ? a.Tia : a.UE` (`Tia` = `NQ(UE)`),
    //             then `this.jc.Kh(2).data[b]` — the already-swapped buffer;
    //   e (L559): `c.L7a(c.rw&&a.bja>-1 ? a.bja : a.TS).ma` / `currentNode.ma`.
    // The old port always used `<Align><Pivot Part>` itself, which is a
    // DIFFERENT bone whenever `rw` (38 world units here for the fists stance)
    // and left the anchor off the spawn.
    int align_idx = pivot_idx;
    if (mirror_swap_ && pivot_idx >= 0) {
        const int j = mirror_partner(pivot_idx);
        if (j >= 0) align_idx = j;
    }
    align_ref_u_ = align_idx;
    const std::size_t zclip = std::min(fb.size(), model_.bones.size());
    // `Oeb`-swapped buffer source for a bone index.
    const auto buf_src = [&](int idx) -> std::size_t {
        if (idx < 0) return 0;
        const int s = mirror_swap_src(idx, zclip);
        return static_cast<std::size_t>(s < 0 ? idx : s);
    };

    // d = the Pivot object's position (JS `Gub` L558).
    // [F1] `Skb` L551 runs `Peb();Gub();` — `Peb` -> `Qeb` (L550) ->
    // `vu.Neb` (L668) has ALREADY negated the buffer x (from slot `jW?2:0`,
    // and slot 2 — the `Kh(2)` this reads — is always covered), so `d` must be
    // the MIRRORED clip x when facing -1. The old port read the raw `fb[u].x`.
    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
    if (ve == 1 || ve == 4) {  // EObjectNodes / EObjectPivot: clip buffer node
        if (align_idx >= 0 && static_cast<std::size_t>(align_idx) < fb.size()) {
            const std::size_t u = buf_src(align_idx);
            // [F10] `Gub` L558 reads `this.jc.Kh(2)` AFTER `Peb` ran
            // `Qeb` → `Neb`, i.e. the clip mirror `Te.FX` (`hd()`).
            const float msign = (clip_mirror_ < 0) ? -1.0f : 1.0f;
            dx = msign * fb[u].x;  // `Neb` negates x ONLY (`data[b].x*=-1`)
            dy = fb[u].y;
            dz = fb[u].z;
        }
    }
    // ve == 2 (EObjectAnimation) -> d = 0. ve == 3 (EObjectWall) needs the
    // scene wall bounds `yu`/`zu` (not owned by Fighter) — OPEN, d stays 0.

    // e/f/g = the Position object's position (JS `Gub` L559), in solver
    // (clip) space: `currentNode.ma` / `L7a(i).ma` are the posed positions.
    float ex = 0.0f, ey = 0.0f, ez = 0.0f;
    auto posed = [&](int idx, float& ox, float& oy, float& oz) {
        if (idx < 0 || static_cast<std::size_t>(idx) >= n) return;
        if (sol_ma_.size() != n * 3) return;
        const std::size_t u = static_cast<std::size_t>(idx);
        ox = sol_ma_[u * 3];
        oy = sol_ma_[u * 3 + 1];
        oz = sol_ma_[u * 3 + 2];
    };
    if (jk == 1) {        // EObjectNodes: posed Position-Part bone
        // JS L559: `c.L7a(c.rw&&a.bja>-1 ? a.bja : a.TS).ma` — the Position
        // Part's own mirror partner when `rw` (`bja` = `NQ(TS)`).
        int pi = model_.bone_by_name(al.pos_part);
        if (mirror_swap_ && pi >= 0) {
            const int j = mirror_partner(pi);
            if (j >= 0) pi = j;
        }
        posed(pi, ex, ey, ez);
    } else if (jk == 4) { // EObjectPivot: posed pivot node (`currentNode.ma`)
        posed(align_idx, ex, ey, ez);
    }
    // jk == 2 (EObjectAnimation) -> e = this.Fk = 0 at clip start.
    // jk == 3 (EObjectWall) needs the wall bounds — OPEN, e stays 0.
    ex += f * al.shift_x;  // JS `e += this.hd()*a.dja`
    ey += al.shift_y;      // JS `f += a.eja`

    // `Fk = e - d` (JS L559 `c.x=e-d.x; ...`); `Gla` selects the per-axis
    // component (X/Z here; `ShiftY` when Y is not an align axis).
    align_x_ = al.axis_x ? (ex - dx) : al.shift_x;
    align_y_ = al.axis_y ? (ey - dy) : al.shift_y;
    align_z_ = al.axis_z ? (ez - dz) : 0.0f;
}

// JS `Te.Skb` L550-551: builds the two play-buffer slots prepended before the
// clip. `vu.Pka(a,b,c,d)` (L340543) — when the move has `NoInterpolationFrames`
// (`c` true = `WGa` = `!Qqa`) it copies clip frame `min(len-1, FirstFrame+2)`
// into BOTH slots. Otherwise `Te.qrb(ZW)` (L282683) seeds slot0 = `ma -
// 1.5*(ma-mf)` and slot1 = `ma + 1.5*(ma-mf)` from the CURRENT posed node
// (`ma`) and its previous position (`mf`) — the clip-start pose blend, where
// 1.5 = (MidFrames+1)/2. `ZW` = the clip bone count, so only the clip bones
// are seeded; the buffer is indexed 0..ZW-1 (JS `m.resize(this.fq, a.ZW, ...)`).
void Fighter::build_prepend(const MoveDef& move) {
    prepend_.clear();
    if (current_clip_ == nullptr || current_clip_->frames.empty()) {
        return;
    }
    const std::size_t nclip = current_clip_->bone_count();
    if (nclip == 0) {
        return;
    }
    prepend_.assign(nclip * 2 * 3, 0.0f);
    const int ff = std::max(0, move.first_frame);
    // [F1] JS `Te.Skb` L551 order is `Peb()` (mirror) THEN `Gub()` (align), so
    // the prepend slots end up as `-(clip x) + align`. The two transforms
    // differ per branch (`vu.shift`/`vu.Neb` both start at `this.jW?2:0`):
    //   - `qrb` prepend (`!no_interp`, `jW==true`): neither the negation nor
    //     the shift touches slots 0/1, so they are stored verbatim;
    //   - `Pka` prepend (`no_interp`, `jW==false`): both run from slot 0, so
    //     the prepend is negated (facing<0) and then shifted by the align.
    const float msign = mirror_prepend_ ? -1.0f : 1.0f;
    const float sh_x = move.no_interp ? align_x_ : 0.0f;
    const float sh_y = move.no_interp ? align_y_ : 0.0f;
    const float sh_z = move.no_interp ? align_z_ : 0.0f;
    if (move.no_interp) {
        // `vu.Pka` prepend: both slots = clip[min(len-1, FirstFrame+2)].
        const int f = std::min(static_cast<int>(current_clip_->frames.size()) - 1, ff + 2);
        const auto& fb = current_clip_->frames[static_cast<std::size_t>(f)].bones;
        for (std::size_t i = 0; i < nclip; ++i) {
            const sf2::data::anim_keyframe k = i < fb.size() ? fb[i] : sf2::data::anim_keyframe{};
            for (int slot = 0; slot < 2; ++slot) {
                const std::size_t u = (static_cast<std::size_t>(slot) * nclip + i) * 3;
                prepend_[u] = msign * k.x + sh_x;
                prepend_[u + 1] = k.y + sh_y;
                prepend_[u + 2] = k.z + sh_z;
            }
        }
        return;
    }
    // `Te.qrb`: slot0/1 = ma ∓ (XJ+1)/2 · (ma-mf) from the pre-clip solver
    // state (`Al` ma/mf). Without solver state fall back to clip[FirstFrame]
    // (the JS `ma`/`mf` would still hold the bind pose, not the origin).
    if (!solver_init_ || sol_ma_.size() != model_.bones.size() * 3 ||
        sol_mf_.size() != sol_ma_.size()) {
        const int f = std::min(static_cast<int>(current_clip_->frames.size()) - 1, ff);
        const auto& fb = current_clip_->frames[static_cast<std::size_t>(f)].bones;
        for (std::size_t i = 0; i < nclip; ++i) {
            const sf2::data::anim_keyframe k = i < fb.size() ? fb[i] : sf2::data::anim_keyframe{};
            for (int slot = 0; slot < 2; ++slot) {
                const std::size_t u = (static_cast<std::size_t>(slot) * nclip + i) * 3;
                prepend_[u] = k.x;
                prepend_[u + 1] = k.y;
                prepend_[u + 2] = k.z;
            }
        }
        return;
    }
    const float half = static_cast<float>(move.mid_frames + 1) * 0.5f;  // (XJ+1)/2
    for (std::size_t i = 0; i < nclip; ++i) {
        const std::size_t u = i * 3;
        const float vx = (sol_ma_[u] - sol_mf_[u]) * half;
        const float vy = (sol_ma_[u + 1] - sol_mf_[u + 1]) * half;
        const float vz = (sol_ma_[u + 2] - sol_mf_[u + 2]) * half;
        const std::size_t u0 = (0 * nclip + i) * 3;
        const std::size_t u1 = (1 * nclip + i) * 3;
        prepend_[u0] = sol_ma_[u] - vx;
        prepend_[u0 + 1] = sol_ma_[u + 1] - vy;
        prepend_[u0 + 2] = sol_ma_[u + 2] - vz;
        prepend_[u1] = sol_ma_[u] + vx;
        prepend_[u1 + 1] = sol_ma_[u + 1] + vy;
        prepend_[u1 + 2] = sol_ma_[u + 2] + vz;
    }
}

void Fighter::sample(const sf2::data::anim_clip& clip, int frame, float x,
                     float y, int mirror_sign, bool interp, int first_frame,
                     int playhead) {
    if (clip.frames.empty()) {
        return;
    }
    frame = std::max(0, std::min(frame, static_cast<int>(clip.frames.size()) - 1));
    const std::size_t n = model_.bones.size();
    if (n == 0) {
        return;
    }
    const auto& bones = model_.bones;
    // Clip bone count (JS `Ua.ZW`). In the playback path the three Bezier
    // control points are play-buffer slots [playhead, playhead+1, playhead+2]
    // where slots 0,1 are the prepend (`prepend_`) and slots >=2 are clip
    // frame `FirstFrame + slot - 2` (JS `Te.Gka` reads `jc.Kh(Xh..Xh+2)`). The
    // static path (`interp=false`) keeps the legacy (frame, frame+1, frame+2)
    // mapping used by the dojo probe/bag poses.
    const int clip_len_i = static_cast<int>(clip.frames.size());
    const int fbase = interp ? std::max(0, first_frame) : frame;
    const auto& frame_bones =
        clip.frames[static_cast<std::size_t>(std::max(0, std::min(fbase, clip_len_i - 1)))].bones;
    const std::size_t nclip = std::min(frame_bones.size(), n);

    // [FIX root-motion] JS `wu` Bezier (Te.Gka/wu.f6a L1284-1286):
    // P0=mid(a,b), P1=b, P2=mid(b,c) at t=(mo+1)/UM. Linear over-scales.
    const int sub_i = std::max(1, sub_);
    const float t_bez = (static_cast<float>(subframe_) + 1.0f) / static_cast<float>(sub_i);
    const float omt = 1.0f - t_bez;
    const float w0 = omt * omt;
    const float w1 = 2.0f * omt * t_bez;
    const float w2 = t_bez * t_bez;
    const float t_lin = static_cast<float>(subframe_) / static_cast<float>(sub_i);

    // [F3] JS `Ua.Oeb` L692: when `rw` (`mirror_swap_`) is true the buffered
    // clip's `_1`/`_2` pairs are swapped in EVERY slot from slot 2 up (the two
    // prepend slots are never swapped — `let h=2,k=b.size`); pairs whose
    // partner id is outside the slot-2 size are skipped
    // (`a[c].first<e&&a[c].second<e`). Handled by `mirror_swap_src` in `ctl`.
    // [F1] JS `Te.Qeb` L550 -> `vu.Neb` L668: with facing -1 the clip buffer x
    // is negated around clip-space 0 (`data[b].x*=-1` — x only). Applied to
    // the raw clip control points here; the prepend slots are already stored
    // mirrored by `build_prepend` for the `jW==false` case (`Neb` starts at
    // slot 0 there), so they are used verbatim. `sample_current()` always
    // passes `clip_mirror_`, so this matches the `mirror_x_` used at move
    // start (both are the `Te.FX`/`hd()` clip-mirror term, JS L547/L550).
    const float mneg = (mirror_sign < 0) ? -1.0f : 1.0f;

    // Resolve one (control slot, bone) position in clip/model space. In the
    // playback path the buffer index is `playhead + rel`; slots 0,1 are the
    // clip-start prepend and slots >=2 are clip frame `FirstFrame + slot - 2`.
    // Returns false when the bone has no key at that slot (the caller then
    // keeps the bind position).
    auto ctl = [&](int rel, std::size_t i, float& ox, float& oy, float& oz) -> bool {
        int abs_slot;
        if (interp) {
            abs_slot = playhead + rel;
            if (abs_slot < 2) {
                if (i >= nclip || prepend_.size() != nclip * 6) return false;
                const std::size_t u = (static_cast<std::size_t>(abs_slot) * nclip + i) * 3;
                ox = prepend_[u];
                oy = prepend_[u + 1];
                oz = prepend_[u + 2];
                return true;
            }
        } else {
            abs_slot = frame + rel;
        }
        int f = interp ? (fbase + abs_slot - 2) : abs_slot;
        f = std::max(0, std::min(f, clip_len_i - 1));
        const auto& fb = clip.frames[static_cast<std::size_t>(f)].bones;
        if (i >= fb.size()) return false;
        const std::size_t src =
            static_cast<std::size_t>(mirror_swap_src(static_cast<int>(i), nclip));
        // [F1] The mirror (`Neb`) and the align (`Gla` -> `vu.shift`, L550/L667)
        // are BUFFER transforms: `shift` adds the align to every slot from
        // `jW?2:0` up, i.e. to these clip-frame slots (never to the `qrb`
        // prepend). Applying them here — instead of to the interpolated `px`
        // afterwards — is what the JS does: `wu`/`rp` blend buffer slots, so
        // the prepend contribution must NOT carry the align.
        ox = mneg * fb[src].x + align_x_;
        oy = fb[src].y + align_y_;
        oz = fb[src].z + align_z_;
        return true;
    };

    // Render anchor: JS `Dl.Fe()` (L575) = `Va.Yd` = the `<PivotNode Name>`
    // node ("NPivot", read from internal_settings.xml). Hoisted here so the
    // anchor-drive below and the `pos_` placement share one lookup.
    int anchor = model_.bone_by_name(fighter_pivot_bone());
    if (anchor < 0) {
        // Shipped models all carry the pivot; a model without it keeps the
        // legacy COM anchor (JS `Dl.Trb` L577 falls back to `all[0]`).
        anchor = model_.bone_by_name("COM");
    }
    if (anchor < 0) {
        anchor = 0;
    }
    const std::size_t anchor_u = static_cast<std::size_t>(anchor);

    std::vector<float> px(n), py(n), pz(n);
    for (std::size_t i = 0; i < n; ++i) {
        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        float bx = 0.0f, by = 0.0f, bz = 0.0f;
        float cx = 0.0f, cy = 0.0f, cz = 0.0f;
        const bool ha = ctl(0, i, ax, ay, az);
        const bool hb = ctl(1, i, bx, by, bz);
        const bool hc = ctl(2, i, cx, cy, cz);
        if (i < nclip && hb && hc && sub_i > 1) {
            const float p0x = (ax + bx) * 0.5f, p0y = (ay + by) * 0.5f, p0z = (az + bz) * 0.5f;
            const float p2x = (bx + cx) * 0.5f, p2y = (by + cy) * 0.5f, p2z = (bz + cz) * 0.5f;
            px[i] = w0 * p0x + w1 * bx + w2 * p2x;
            py[i] = w0 * p0y + w1 * by + w2 * p2y;
            pz[i] = w0 * p0z + w1 * bz + w2 * p2z;
        } else if (i < nclip && hb) {
            px[i] = ax + (bx - ax) * t_lin;
            py[i] = ay + (by - ay) * t_lin;
            pz[i] = az + (bz - az) * t_lin;
        } else if (i < nclip && ha) {
            px[i] = ax; py[i] = ay; pz[i] = az;
        } else { px[i] = bones[i].x; py[i] = bones[i].y; pz[i] = bones[i].z; }
    }

    // [FIX root-motion align — JS `Te.Gub` L557-559 -> `Te.Gla` L550 -> `vu.shift`
    // L667] `Gla` shifts the clip BUFFER (`jc.shift`, slots `jW?2:0` and up)
    // once at clip start; every later `eda` (JS L556) reads the shifted buffer.
    // The shift is therefore applied to the buffer slots inside `ctl` above —
    // NOT to the interpolated `px` here, which would also shift the `qrb`
    // prepend's share of the blend (the JS prepend slots are never shifted).

    // [FIX render anchor — JS `Dl.Fe()` L575 + `Te.Gub`/`Te.Gla` L557-559/L550]
    // The JS anchor (`Dl.Fe()` = `Va.Yd` = NPivot) is a POSED node: every
    // `eda` (L556) sets `ma = fq[mo] + j8`, and `fq` is the clip buffer after
    // `Gla` shifted it so the buffer's `<Align><Pivot Part>` reference frame
    // matched the previous pose. So the anchor's world x is
    //   clip_interp(NPivot) + (prev_world(Part) - clip(Part, FirstFrame))
    // — the clip's OWN motion of NPivot, plus the per-move constant in
    // `render_offset_` (captured in `start_move_impl`), plus the authored
    // `<Velocity>` offset (`j8`).
    // The old code instead accumulated the raw clip bone-0 delta onto
    // `world_x_` — a different node's swing — which is the intro-stance drift
    // (~19u at the idle start) this fixes.
    if (interp && current_move_ != nullptr && !solver_world_) {
        if (!render_offset_valid_) {
            // No `<Align>` (JS `Gla(0,0,0)` shifts nothing): hold the anchor
            // itself continuous.
            render_offset_ = world_x_ - px[anchor_u];
            render_offset_valid_ = true;
        }
        world_x_ = px[anchor_u] + render_offset_ + j8_x_;
        x = world_x_;  // `pos_` below is built from the recomputed anchor
        // [B1 FIX vertical anchor] The SAME drive on y. JS `Te.eda` (L556)
        // writes `ma = fq[mo] + j8` for every clip bone, so NPivot's y rides
        // the clip exactly like its x (the anchor is NOT re-pinned to the
        // spawn after `Dl.oL` at init). Leaving `world_y_` at the spawn placed
        // the model at `clip_y - clip_pivot_y + spawn_y`: the feet sank and
        // then floated by the clip's pivot travel (dojo `FistsStartStance-Left`
        // pivot y runs -135..-84 against a -93 spawn, i.e. +42..-9 world units
        // ≈ ±33 px at the fight zoom 1.3 — the reported float/twitch).
        world_y_ = py[anchor_u] + render_offset_y_;
        y = world_y_;
    }

    // 2. [FIX stretched mesh — ragdoll solver] The game's per-frame pose
    //    pipeline (JS fighter `ia` L253769: `da.ia()` [Te.eda applies the
    //    clip] -> `Nd.ia()` [Al.ia = sk + jE] -> `oa.Qja()` [macros]):
    //    (see the step comments inside; n3 = the solver state stride)
    //
    //    a) `Te.eda` (L282908): every clip-driven bone gets `f4()` (mf =
    //    ma — the previous SOLVED position) then `XA(clip)` (ma = the
    //    interpolated clip position). Bones past the clip bone count keep
    //    their solved state (the cloth ragdoll).
    //    b) `Al.sk` (L296832, `Vc.sk` L405734): Verlet integrate every
    //    non-immovable bone — new = ma + (ma - mf) * (cloth ? 1-Att :
    //    1) + (0, grav, 0); grav = `xd.fDa` = 0.4 (internal_settings.xml
    //    Physics Gravitation). Immovable (`NG`): Fixed="1" bones and ALL
    //    MacroNodes (JS `Fl` ctor `QMa(1)` -> nh=false -> NG=true).
    //    c) `Al.jE` (L296592): `IterativeProcess`=2 passes over every
    //    <Edges> entry; `yu.bFa` (L403731) relaxes the pair toward the
    //    rest Length, mass-weighted (`f=(1-len/dist)/(w1+w2)`), moving
    //    only the non-immovable endpoints. This is what keeps the cloth
    //    at its constraint lengths around the posed skeleton — the
    //    static bind-offset anchoring it replaces stretched the cloth
    //    triangles (the cloth's edges bind to head macros / knees /
    //    ankles, not the bind-nearest bone).
    //    d) `Dl.Qja` (L294688) -> `Fl.seb` (L406288): every macro not
    //    clip-posed this frame is re-derived as the weighted average of
    //    its children's SOLVED positions (mf = ma — velocity zeroed).
    //    The solver state (sol_ma_/sol_mf_) persists across sample()
    //    calls — one step per call matches the game's 60 Hz cadence.
    const std::size_t n3 = n * 3;
    if (solver_init_ && sol_ma_.size() == n3) {
        // [F9] The per-sample COM translation that used to live here is
        // REMOVED. The JS solver space is inherently continuous: compute_align
        // (JS Te.Gub/Gla, L557-560/L550) re-expresses each new clip into the
        // previous pose own space via the align shift, and the persisted solver
        // state is that same space, so no per-sample or per-clip translation is
        // needed. Re-applying a delta here re-stepped a translation and measured
        // it from bone 0 - a node the align never uses.
        // (a) eda: clip bones mf = solved, ma = interpolated clip pose.
        // While the ragdoll is active (`nk`/`solver_world_`) the clip apply
        // does NOT overwrite the solver bodies — their `ma` is world space
        // and must persist (the JS node `ma` is world; this is the
        // hit-reaction that no longer snaps back).
        if (!solver_world_) {
            for (std::size_t i = 0; i < nclip; ++i) {
                sol_mf_[i * 3] = sol_ma_[i * 3];
                sol_mf_[i * 3 + 1] = sol_ma_[i * 3 + 1];
                sol_mf_[i * 3 + 2] = sol_ma_[i * 3 + 2];
                sol_ma_[i * 3] = px[i];
                sol_ma_[i * 3 + 1] = py[i];
                sol_ma_[i * 3 + 2] = pz[i];
            }
        }
        // [FIX stretched mesh — JS-faithful] `Al.ia()` (L582) runs exactly
        // ONE solver step per 60 Hz frame: `sk(); jE();`. The invented
        // 600-step warmup (PORT_AUDIT_RENDER §2.4 candidate 1; absent from
        // JS) is removed.
        // (b) sk: Verlet integrate (grav 0.4 = `xd.fDa`).
        // [FIX stretched mesh — cloth-only] JS `Al.sk` @296832 gates EVERY
        // body on `this.nk` (ragdoll-active, set ONLY by `Al.start` @296449
        // from `Lwb` @260135 = ragdoll start): `... && (this.nk || c.jy ||
        // ...) && c.sk(...)`. For a NORMAL animated fighter `nk` is false, so
        // only cloth bodies (`jy`) integrate; the clip-driven skeleton bodies
        // are NOT moved by the solver (they keep their `Te.eda` pose). The
        // old native integrated every non-fixed non-macro bone, dragging the
        // posed skeleton off the clip (the stretched mesh).
        constexpr float kGrav = 0.4f;
        for (std::size_t i = 0; i < n; ++i) {
            const Bone& b = bones[i];
            // JS `Al.sk` (L583): `!c.NG && (this.nk || c.jy || oa.vc && c.vc)`.
            // NG = MG || !nh (MG = Fixed; nh false for MacroNodes, `QMa(1)`);
            // jy = PG && nh = cloth. `nk` is false (no ragdoll start in the
            // port), so a NON-cloth node integrates only under the model shock
            // latch and its own Shock flag — identical to cloth-only when no
            // node carries Shock="1".
            const bool ng = b.fixed || b.is_macro;
            const bool jy = b.cloth && !b.is_macro;
            // JS `Al.sk` L583: `!c.NG && (this.nk || c.jy || oa.vc && c.vc)`.
            if (ng || !(nk_ || jy || (shock_latch_ && b.shock))) continue;
            const std::size_t i3 = i * 3;
            float vx = sol_ma_[i3] - sol_mf_[i3];
            float vy = sol_ma_[i3 + 1] - sol_mf_[i3 + 1];
            float vz = sol_ma_[i3 + 2] - sol_mf_[i3 + 2];
            if (b.cloth) {
                const float k = 1.0f - b.attenuation;  // `Vc.bI` damp
                vx *= k;
                vy *= k;
                vz *= k;
            }
            sol_mf_[i3] = sol_ma_[i3];
            sol_mf_[i3 + 1] = sol_ma_[i3 + 1];
            sol_mf_[i3 + 2] = sol_ma_[i3 + 2];
            sol_ma_[i3] += vx;
            sol_ma_[i3 + 1] += vy + kGrav;
            sol_ma_[i3 + 2] += vz;
        }
        // (c) jE: 2 edge-relaxation passes (`yu.bFa` mass-weighted).
        // JS `Al.jE` (L583) cA per node: `d.cA = d.nh && !d.NG &&
        // (this.nk || d.jy || oa.vc && d.vc)` — with `nh` true for ordinary
        // Nodes, this is the SAME predicate as `Al.sk` (non-macro, non-NG,
        // cloth or shock-participating).
        auto cA_of = [&](std::size_t u) {
            const Bone& b = bones[u];
            if (b.fixed || b.is_macro) return false;
            const bool jy = b.cloth && !b.is_macro;
            // JS `Al.jE` L583: `d.cA = d.nh && !d.NG && (this.nk || d.jy ||
            // a && d.vc)` — the same predicate as `Al.sk`.
            return nk_ || jy || (shock_latch_ && b.shock);
        };
        constexpr int kEdgeIters = 2;  // `xd.jE` IterativeProcess
        for (int it = 0; it < kEdgeIters; ++it) {
            for (const EdgeDef& e : model_.edges) {
                const int bi1 = model_.bone_by_name(e.end1);
                const int bi2 = model_.bone_by_name(e.end2);
                if (bi1 < 0 || bi2 < 0) continue;
                const std::size_t i1 = static_cast<std::size_t>(bi1);
                const std::size_t i2 = static_cast<std::size_t>(bi2);
                if (i1 >= n || i2 >= n) continue;
                // JS `Al.jE` @296592: `d.cA = d.nh && !d.NG &&
                // (this.nk || d.jy || a && d.vc)`. `nh` = the body
                // participates, `NG` = immovable (Fixed / MacroNode),
                // `jy` = cloth; a Shock node also relaxes while the model
                // shock latch is set. Non-cloth clip bones are NOT relaxed
                // (they stay exactly at the clip pose).
                const bool cA1 = cA_of(i1);
                const bool cA2 = cA_of(i2);
                if (!cA1 && !cA2) continue;
                const std::size_t u1 = i1 * 3;
                const std::size_t u2 = i2 * 3;
                const float ex = sol_ma_[u2] - sol_ma_[u1];
                const float ey = sol_ma_[u2 + 1] - sol_ma_[u1 + 1];
                const float ez = sol_ma_[u2 + 2] - sol_ma_[u1 + 2];
                const float dist = std::sqrt(ex * ex + ey * ey + ez * ez);
                if (dist < 1e-9f) continue;
                const float r = e.length / dist;
                const float w1 = bones[i1].mass;
                const float w2 = bones[i2].mass;
                const float fk = (1.0f - r) / (w1 + w2);
                const float g1 = w1 * fk;
                const float g2 = w2 * fk;
                const float bx = sol_ma_[u1] * g1 + sol_ma_[u2] * g2;
                const float by = sol_ma_[u1 + 1] * g1 + sol_ma_[u2 + 1] * g2;
                const float bz = sol_ma_[u1 + 2] * g1 + sol_ma_[u2 + 2] * g2;
                if (cA1) {
                    sol_ma_[u1] = sol_ma_[u1] * r + bx;
                    sol_ma_[u1 + 1] = sol_ma_[u1 + 1] * r + by;
                    sol_ma_[u1 + 2] = sol_ma_[u1 + 2] * r + bz;
                }
                if (cA2) {
                    sol_ma_[u2] = sol_ma_[u2] * r + bx;
                    sol_ma_[u2 + 1] = sol_ma_[u2 + 1] * r + by;
                    sol_ma_[u2 + 2] = sol_ma_[u2 + 2] * r + bz;
                }
            }
        }
        // JS `Al.ia` (L582) tail: `sk(); jE(); nk&&frameCount++`.
        if (nk_) ++ragdoll_frame_count_;
        // JS `Al.fha` (L582, run from `Al.ia`): the arena/ground response for
        // every solver body — `Al.P6a` (revert to the previous position, snap
        // y to the floor, re-advance minus the friction distance) for a
        // collidable body at/below the floor, then the `NO`/`MO` x clamp. Only
        // the world-space ragdoll state needs it — the clip-space solver is
        // authored inside the arena.
        if (solver_world_) {
            // The port's world y is down-positive (the model parse negates the
            // XML Y), so the floor is the MAX y a body may reach: `y >= floor`
            // is the JS `b.y >= 0`, and `Al.P6a`'s `a.y = 0` is `y = floor`.
            int wall_hits = 0;
            for (std::size_t i = 0; i < n; ++i) {
                float& nx = sol_ma_[i * 3];
                float& ny = sol_ma_[i * 3 + 1];
                float& nz = sol_ma_[i * 3 + 2];
                const float px = sol_mf_[i * 3];
                const float pz = sol_mf_[i * 3 + 2];
                const float dx = sf2::scene::fha_body(
                    nx, ny, nz, px, pz, bones[i].collisible,
                    ragdoll_wall_min_, ragdoll_wall_max_, ragdoll_floor_y_);
                if (dx != 0.0f) {
                    ++wall_hits;
                    std::fprintf(stdout,
                                 "[wall] F%d node=%s x %.2f -> %.2f (d=%.2f)\n",
                                 frame, bones[i].name.c_str(), nx - dx, nx, dx);
                }
            }
            if (wall_hits > 0) std::fflush(stdout);
        }
        // (d) Qja/seb: macros re-derived from the solved children.
        std::vector<std::uint8_t> visiting(n, 0);
        std::function<void(std::size_t)> compute_macro = [&](std::size_t idx) {
            if (visiting[idx]) {
                return;  // cycle guard
            }
            visiting[idx] = 1;
            const auto it2 = model_.macro_children.find(bones[idx].name);
            if (it2 != model_.macro_children.end()) {
                const MacroChildren& mc = it2->second;
                float ax = 0.0f, ay = 0.0f, az = 0.0f;
                for (std::size_t c = 0; c < mc.child_names.size(); ++c) {
                    const int ci = model_.bone_by_name(mc.child_names[c]);
                    if (ci < 0) continue;
                    const std::size_t u = static_cast<std::size_t>(ci);
                    if (u >= n) continue;
                    if (bones[u].is_macro && u >= nclip) {
                        compute_macro(u);
                    }
                    const float w = c < mc.weights.size() ? mc.weights[c] : 0.0f;
                    ax += sol_ma_[u * 3] * w;
                    ay += sol_ma_[u * 3 + 1] * w;
                    az += sol_ma_[u * 3 + 2] * w;
                }
                sol_ma_[idx * 3] = ax;
                sol_ma_[idx * 3 + 1] = ay;
                sol_ma_[idx * 3 + 2] = az;
                sol_mf_[idx * 3] = ax;
                sol_mf_[idx * 3 + 1] = ay;
                sol_mf_[idx * 3 + 2] = az;
            }
            visiting[idx] = 0;
        };
        for (std::size_t i = nclip; i < n; ++i) {
            if (bones[i].is_macro) {
                compute_macro(i);
            }
        }
        // The solved pose becomes this frame's positions.
        for (std::size_t i = 0; i < n; ++i) {
            px[i] = sol_ma_[i * 3];
            py[i] = sol_ma_[i * 3 + 1];
            pz[i] = sol_ma_[i * 3 + 2];
        }
    } else {
        // No solver state (defensive): fall back to the bind pose for the
        // non-clip bones (the pre-solver behavior minus the cloth anchor).
        for (std::size_t i = nclip; i < n; ++i) {
            if (bones[i].is_macro) {
                continue;
            }
            px[i] = bones[i].x;
            py[i] = bones[i].y;
            pz[i] = bones[i].z;
        }
    }

    // [FIX root motion — JS `Te.bYa` L564, invoked from `eda` L556 while
    // `Ua.zX != 0`] The move's <Rotation Angle> + <Position> pivot rotates
    // EVERY posed bone about the pivot by `Angle` degrees in the local
    // model (x,y) plane: `e = zX*0.017453292519943295; h=f.x-b.x;
    // f=f.y-b.y; f=(cos(e)*h-sin(e)*f+b.x, sin(e)*h+cos(e)*f+b.y, 0, 1)`.
    // The pivot `b = Ua.AX.nt(model.Fc)` (`ee.nt` L786): for
    // Object="Nodes" it is the posed node `Part`, with `c.x += ix*Wl`
    // (Wl = fighter scale = 1) and `c.y -= jx`. Only the shipped
    // Object="Nodes" case is handled; Object=Animation/Pivot/Wall are OPEN
    // (none of the 6 shipped <Rotation> moves use them). Applies to px/py
    // after the solver — the JS `bYa` runs in `eda` before `Al.ia`, so the
    // native cloth solver state (`sol_ma_`) is NOT rotated (OPEN, cloth-only
    // on rotation moves).
    if (!solver_world_ && current_move_ != nullptr && current_move_->rotation.has_rotation &&
        current_move_->rotation.angle != 0.0f &&
        current_move_->rotation.pos_object == "Nodes") {
        const int pit = model_.bone_by_name(current_move_->rotation.pos_part);
        if (pit >= 0 && static_cast<std::size_t>(pit) < n) {
            const std::size_t pu = static_cast<std::size_t>(pit);
            const float ox = px[pu] + current_move_->rotation.shift_x;  // `c.x += ix*Wl`
            const float oy = py[pu] - current_move_->rotation.shift_y;  // `c.y -= jx`
            const float rad = current_move_->rotation.angle * 0.017453292519943295f;
            const float cs = std::cos(rad);
            const float sn = std::sin(rad);
            for (std::size_t i = 0; i < n; ++i) {
                const float hx = px[i] - ox;
                const float hy = py[i] - oy;
                px[i] = cs * hx - sn * hy + ox;
                py[i] = sn * hx + cs * hy + oy;
            }
        }
    }

    // JS mirror: `Te.Qeb`/`vu.Neb` (L550/L668) negate the clip BUFFER x and
    // `Ua.Oeb` (L692) swaps the `_1`/`_2` pairs — both are applied to the
    // buffered clip above (`ctl`), decided ONCE at clip start in
    // `start_move_impl` (`mirror_swap_`). The old per-frame post-solve swap
    // here (re-derived from `prev_x_`) is removed.

    // [F1] World placement: the fighter's (x, y) anchors the model's PivotNode
    //    bone at (x, y) — JS `Dl.oL` (L577) offsets every bone so the anchor's
    //    `ma` lands on the placement point (`pos = ma - ma[anchor] + anchor`).
    //    The facing mirror is NOT applied here: JS mirrors the CLIP BUFFER
    //    (`Te.Qeb` L550 -> `vu.Neb` L668, applied in `ctl` above), so the
    //    placement is a plain offset. The old `(px[i]-px[anchor_u])*f` applied
    //    the mirror a SECOND time on top of the buffer negation.
    //    `internal_settings.xml` ships `<PivotNode Name="NPivot"/>` (parse
    //    L1155, default "NPivot"); `anchor`/`anchor_u` were resolved above
    //    (shared with the anchor drive, which rewrites `x` to the JS-posed
    //    NPivot world x).
    if (solver_world_) {
        // The ragdoll wins: `px/py` are already WORLD (the solver state was
        // promoted to world by `ragdoll_start`), so there is no clip
        // placement and no offset — the clipped/reaction displacement
        // persists across frames.
        world_x_ = px[anchor_u];
        world_y_ = py[anchor_u];
        for (std::size_t i = 0; i < n; ++i) {
            pos_[i * 2] = px[i];
            pos_[i * 2 + 1] = py[i];
        }
    } else {
    const float anchor_y = py[anchor_u];
    const float dy = y - anchor_y;
    for (std::size_t i = 0; i < n; ++i) {
        pos_[i * 2] = px[i] - px[anchor_u] + x;
        pos_[i * 2 + 1] = py[i] + dy;
        // Knockback ride: the impulse-split offsets displace the hit bones
        // on top of the clip pose (JS endpoint-body moves persist into the
        // next frame's `ma`).
        if (i < kb_.size()) {
            pos_[i * 2] += kb_[i].x;
            pos_[i * 2 + 1] += kb_[i].y;
        }
    }
    }
    // JS `dv.ia` (L840) drops z when it skins the mesh (`Xg[a++] = d.x;
    // Xg[a++] = d.y`), so no per-bone depth is retained for drawing — the
    // triangles draw in XML document order (see build_vertices).
    pose_sampled_ = true;  // `pos_` now holds a real frame (the `ma` analog)
}

std::size_t Fighter::build_vertices(std::vector<float>& out) const {
    out.clear();
    const std::size_t ntri = model_.resolved_tris.size();
    out.reserve(ntri * 6);

    // JS `dv.ia` (L840) emits the triangles in XML DOCUMENT order: `zU` is
    // pushed in `dv.DXa` resolution order and `Xg` copies each node's
    // `ma.x/ma.y` (z dropped) — there is NO depth sort. The previous native
    // painter's sort by mean pose z re-ordered overlapping limbs and read as
    // "some triangles wrong" against the oracle (PORT_AUDIT_RENDER §3.3/§4.1,
    // ranked P0 #1): restore the document order exactly.
    for (const TriResolved& tri : model_.resolved_tris) {
        out.push_back(pos_[static_cast<std::size_t>(tri.i1) * 2]);
        out.push_back(pos_[static_cast<std::size_t>(tri.i1) * 2 + 1]);
        out.push_back(pos_[static_cast<std::size_t>(tri.i2) * 2]);
        out.push_back(pos_[static_cast<std::size_t>(tri.i2) * 2 + 1]);
        out.push_back(pos_[static_cast<std::size_t>(tri.i3) * 2]);
        out.push_back(pos_[static_cast<std::size_t>(tri.i3) * 2 + 1]);
    }
    return out.size() / 2;
}

void Fighter::triangle_bbox(float& min_x, float& min_y, float& max_x,
                            float& max_y) const {
    min_x = min_y = max_x = max_y = 0.0f;
    bool first = true;
    for (const TriResolved& tri : model_.resolved_tris) {
        const int idx[3] = {tri.i1, tri.i2, tri.i3};
        for (int k = 0; k < 3; ++k) {
            const float vx = pos_[static_cast<std::size_t>(idx[k]) * 2];
            const float vy = pos_[static_cast<std::size_t>(idx[k]) * 2 + 1];
            if (first) {
                min_x = max_x = vx;
                min_y = max_y = vy;
                first = false;
            } else {
                min_x = std::min(min_x, vx);
                max_x = std::max(max_x, vx);
                min_y = std::min(min_y, vy);
                max_y = std::max(max_y, vy);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// `Vu` (mu g="D0" L249972) + `Cn` (tu g="DE" L297387) — the hit-reaction latch
// and the strike memory. See the fighter.hpp comments for the JS cites.
// ---------------------------------------------------------------------------

// JS `Te.lrb(a,b,c)` (L523, called from `ca.Cgb` L395 when the attacker's
// active Attack interval has `DL = !NoEffect`):
//   `this.Vu.bk=a; this.Vu.fg=b; this.Vu.time=c; this.Vu.Ica=!0`
void Fighter::latch_reaction(const sf2::scene::Vec3& pos,
                             const sf2::scene::Vec3& dir, float time) {
    reaction_.pos = pos;
    reaction_.dir = dir;
    reaction_.time = time;
    reaction_.active = true;
}

// JS `Eu.gT(a,b)` (L298840): `let c=a-this.Yta; 0<c&&(b=Math.pow(2,-c/b),
// this.Xb*=b, this.Yo*=b, this.count*=b, this.tf*=b, this.gy*=b);
// this.Yta=a`. A non-positive half-life would divide by zero in JS; the
// shipped `<Memory Strikes>` is 3, so guard it to a full decay.
void StrikeMemory::decay(Accum& a, double t, double half_life) {
    const double c = t - a.yta;
    if (c > 0.0) {
        double b = 0.0;
        if (half_life > 0.0) b = std::pow(2.0, -c / half_life);
        a.xb *= b;
        a.yo *= b;
        a.count *= b;
        a.tf *= b;
        a.gy *= b;
    }
    a.yta = t;
}

// JS `tu.F0(a,b)` (L297466): `a=a?this.Ysa:this.bqa; if(!has) set(new Eu)`
// — the per-(side, move) accumulator, created on first touch.
StrikeMemory::Accum& StrikeMemory::entry(
    bool mine, const MoveDef* move) {
    std::map<const MoveDef*, Accum>& m = mine ? mine_ : theirs_;
    return m[move];
}

// JS `tu.nY(a,b,c)` (L297623): `let d=model.lU, e=kfa();
// this.F0(a,b).nY(c,d,e)` and `Eu.nY` (L298864):
// `this.gT(b,c); this.Yo+=a; this.gy+=1`.
void StrikeMemory::nY(bool mine, const MoveDef* move, double value) {
    Accum& a = entry(mine, move);
    decay(a, time_, half_life_);
    a.yo += value;
    a.gy += 1.0;
}

// JS `tu.rY(a,b)` (L297684): `F0(a,b).rY(model.lU, kfa())` and
// `Eu.rY` (L298970): `this.gT(a,b); this.count+=1`.
void StrikeMemory::rY(bool mine, const MoveDef* move) {
    Accum& a = entry(mine, move);
    decay(a, time_, half_life_);
    a.count += 1.0;
}

// JS `tu.v_(a,b)` (L297706): `F0(a,b).v_()` and `Eu.v_` (L298964):
// `this.Xb+=this.Yo; this.Yo=0; this.tf+=this.gy; this.gy=0`.
void StrikeMemory::v_(bool mine, const MoveDef* move) {
    Accum& a = entry(mine, move);
    a.xb += a.yo;
    a.yo = 0.0;
    a.tf += a.gy;
    a.gy = 0.0;
}

// JS `tu.d0(a,b,c,d)` (L298213): `a=F0(!0,a); let e=model.lU, f=kfa();
// b.G=a.c0(e,f); c.G=a.h6a(e,f); d.G=a.U6a(e,f)` where
//   `c0`  (L298918): `gT(t,h); return this.count`
//   `h6a` (L298902): `gT(t,h); return this.Xb`
//   `U6a` (L298918): `gT(t,h); return this.tf`
// JS `F0` INSERTS a zero entry on miss; a miss here yields the same zeros.
void StrikeMemory::d0(const MoveDef* move, double& count, double& xb,
                               double& tf) {
    count = 0.0;
    xb = 0.0;
    tf = 0.0;
    if (move == nullptr) return;
    Accum& a = entry(true, move);
    decay(a, time_, half_life_);
    count = a.count;
    xb = a.xb;
    tf = a.tf;
}

// JS `tu.$K()` (L297964) + `Eu.aKa(a)` (L298976):
// `c.count*=mt(); c.Xb*=mt(); c.Yo*=mt(); c.tf*=mt(); c.aKa(mt())` where
// `aKa(a){this.Xb*=a;this.Yo*=a;this.count*=a;this.tf*=a;this.gy*=a}`.
// `mt()` = the tactic's `<Memory RoundFactor>` (`KW.Q4`).
void StrikeMemory::round_factor(double factor) {
    auto scale = [factor](std::map<const MoveDef*, Accum>& m) {
        for (auto& kv : m) {
            Accum& a = kv.second;
            a.count *= factor;
            a.xb *= factor;
            a.yo *= factor;
            a.tf *= factor;
            a.gy *= factor;
        }
    };
    scale(mine_);
    scale(theirs_);
}

} // namespace sf2::scene
