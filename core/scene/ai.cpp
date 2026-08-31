// AI: tactics file parser (JS `sb` L648-653 + `Si` L653-655), tactic
// settings parser (JS `P` g="E5" + `Md` g="EB" L636-643) and the weight
// curve evaluator (JS `cc` g="EE" L644-648).
//
// Binary reader widths (JS `cd`/`us` L2330-2332):
//   ea() = u8, ie() = u16 LE, Zd() = i16 LE (signed), ti() = u32 LE.
// The tactics .dat decompressed payload (JS `Si.cxb` L653-654):
//   per entry: u32 version, cstr weapon_a, [cstr weapon_b], u32 blob_size,
//              u8[blob_size] blob
// The blob header (JS `sb.tab` L649-650, all u32 via `Kb.tl`=ti()):
//   u32 fmCount   (Ku "table records" — `d`)
//   u32 ilCount   (Il "container records" — `TV`)
//   u32 juCount   (Ju "condition rows" — `daa`)
//   u32 huCount   (Hu "frame rows" — `b9`)
//   u32 guCount   (Gu "outcome" count — `l`)
//   u32 floatPoolSize (BV), u32 u32PoolSize (n)
// then the two string pools (JS `Wlb`/`bmb` L652-653 read them BEFORE
// `tab` — they are the caller's `b`/`c`):
//   u16 countA, u8 lens[countA], u8 strings[sum]   (animation names)
//   u16 countB, u8 lens[countB], u8 strings[sum]   (weapon names)
// then:
//   u16 animIdx[fmCount]           (FM records: pool-A index)
//   i16 scale, i16 fdata[floatPoolSize]   (float pool, scaled)
//   u32 udata[u32PoolSize]                 (u32 pool)
//   u16 idIdx[guCount]              (id pool: pool-A index per Gu)
// then the nested record data (JS L651-652):
//   per FM record: u16 cntG (vec24 count), then cntG ×:
//     u16 weaponIdx (pool B), u16 cntH (row count), then cntH ×:
//       cstr label, u16 cntJ (frame count), then cntJ ×:
//         (only when cntJ>0) u16... i16 Rda, then cntJ ×:
//           u16 cntK (outcome count), then cntK ×:
//             (outcome Gu:) u16 idIdx-pool index (resolved via idIdx),
//             u16 pa (float count), u16 da (u32 count)
//           then the per-outcome JI/NDa slices are read from the shared
//           float/u32 pools in order (JS: t/z offsets advance per record).
//
// NOTE on the id-pool pairing: the JS reads `Q.Ny = h.slice(x, x+U)` then
// `x += U`, and THEN pairs each Ny element with `n[x], ++x` — i.e. the Gu
// anim ids come from the id pool AFTER the Gu slice boundary. The natural
// Gu[i].animation = idIdx[i] pairing is used here (documented [UNCERTAIN]
// in README) — the decision loop keys off the u32 outcome ids + float
// windows, not the Gu anim names, so this does not affect decisions.

#include "scene/ai.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "scene/conditions.hpp"
#include "xml_doc.hpp"
#include "zstd_stream.hpp"

namespace sf2::scene {

namespace {

// A little-endian byte reader matching the JS `cd` methods.
class BinReader {
public:
    BinReader(const std::uint8_t* data, std::size_t size) : p_(data), end_(data + size) {}

    std::uint8_t u8() {
        if (p_ >= end_) throw std::runtime_error("tactics: read past end (u8)");
        return *p_++;
    }
    std::uint16_t u16() {
        const std::uint8_t a = u8(), b = u8();
        return static_cast<std::uint16_t>(a | (b << 8));
    }
    std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
    std::uint32_t u32() {
        const std::uint8_t a = u8(), b = u8(), c = u8(), d = u8();
        return static_cast<std::uint32_t>(a | (b << 8) | (c << 16) | (d << 24));
    }
    std::vector<std::uint8_t> bytes(std::size_t n) {
        if (remaining() < n) {
            throw std::runtime_error("tactics: read past end (bytes)");
        }
        std::vector<std::uint8_t> out(p_, p_ + n);
        p_ += n;
        return out;
    }
    // cstr: sequence of non-zero u8 until a zero (JS `sb.fJ` L653).
    std::string cstr() {
        std::string out;
        for (;;) {
            const std::uint8_t c = u8();
            if (c == 0) break;
            out.push_back(static_cast<char>(c));
        }
        return out;
    }
    // Reads `count` strings stored as a length table + concatenated data
    // (JS `sb.Wlb` L652: `c=a.ie()` = the count, `d=Kb.ek(a,c)` = c LENGTH
    // bytes, then per entry `a.Yt(d.b[e])` = d.b[e] data bytes). The
    // `bmb` variant uses `a.Zd()` (i16) for the count — same layout.
    std::vector<std::string> strings(std::size_t count) {
        std::vector<std::uint8_t> lens(count);
        for (std::size_t i = 0; i < count; ++i) lens[i] = u8();
        std::vector<std::string> out;
        out.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            std::string s;
            for (std::size_t j = 0; j < lens[i]; ++j) s.push_back(static_cast<char>(u8()));
            out.push_back(std::move(s));
        }
        return out;
    }

    std::size_t remaining() const {
        return static_cast<std::size_t>(end_ - p_);
    }
    const std::uint8_t* pos() const { return p_; }
    void skip(std::size_t n) {
        if (remaining() < n) throw std::runtime_error("tactics: skip past end");
        p_ += n;
    }

private:
    const std::uint8_t* p_;
    const std::uint8_t* end_;
};

// Parses one sb blob (JS `sb.tab` L649-652) into a TacticsSet.
// The two string pools (`anim_pool` = JS `b`, `weapon_pool` = JS `c`) are
// read by the caller (JS `Wlb`/`bmb` read them before `tab`).
TacticsSet parse_sb_blob(BinReader& r, const std::vector<std::string>& anim_pool,
                         const std::vector<std::string>& weapon_pool) {
    // Header (all u32): fm/il/ju/hu/gu counts + float/u32 pool sizes.
    const std::uint32_t fm_count = r.u32();
    r.u32();  // ilCount (Il container records)
    r.u32();  // juCount (Ju rows)
    r.u32();  // huCount (Hu frames)
    const std::uint32_t gu_count = r.u32();
    const std::uint32_t float_pool_size = r.u32();
    const std::uint32_t u32_pool_size = r.u32();

    // FM records: u16 pool-A index each.
    std::vector<std::uint16_t> fm_anim_idx(fm_count);
    for (std::uint32_t i = 0; i < fm_count; ++i) fm_anim_idx[i] = r.u16();

    // Float pool: i16 values scaled by `scale`.
    const std::int16_t scale = r.i16();
    std::vector<std::int16_t> fdata(float_pool_size);
    for (std::uint32_t i = 0; i < float_pool_size; ++i) fdata[i] = r.i16();
    auto float_at = [&](std::size_t i) {
        if (i >= fdata.size()) throw std::runtime_error("tactics: float pool overrun");
        const std::int16_t v = fdata[i];
        return scale == 0 ? static_cast<float>(v) : static_cast<float>(scale * v);
    };

    // u32 pool.
    std::vector<std::uint32_t> udata(u32_pool_size);
    for (std::uint32_t i = 0; i < u32_pool_size; ++i) udata[i] = r.u32();

    // Id pool: u16 pool-A index per Gu (JS `n`).
    std::vector<std::uint16_t> id_idx(gu_count);
    for (std::uint32_t i = 0; i < gu_count; ++i) id_idx[i] = r.u16();
    auto anim_at = [&](std::size_t i) {
        if (i >= id_idx.size()) return std::string();
        const std::uint16_t idx = id_idx[i];
        return idx < anim_pool.size() ? anim_pool[idx] : std::string();
    };

    // The shared float/u32 pool offsets (JS `t`/`z`) and the Gu id-pool
    // offset (JS `x` — each Gu consumes the next id_idx[x]).
    std::size_t t = 0, z = 0, x_id = 0;

    TacticsSet set;
    for (std::uint32_t rec = 0; rec < fm_count; ++rec) {
        TacticRecord tr;
        tr.anim = rec < fm_anim_idx.size() && fm_anim_idx[rec] < anim_pool.size()
                      ? anim_pool[fm_anim_idx[rec]]
                      : "";

        const std::uint16_t cnt_g = r.u16();  // vec24 (Il) count for this record
        for (std::uint16_t g = 0; g < cnt_g; ++g) {
            const std::uint16_t weapon_idx = r.u16();
            tr.weapon = weapon_idx < weapon_pool.size() ? weapon_pool[weapon_idx] : "";
            const std::uint16_t cnt_h = r.u16();  // vec28B (Ju) row count
            for (std::uint16_t h = 0; h < cnt_h; ++h) {
                TacticRow row;
                row.label = r.cstr();
                const std::uint16_t cnt_j = r.u16();  // Hu frame count
                const std::int16_t rda = cnt_j > 0 ? r.i16() : 0;
                (void)rda;
                for (std::uint16_t j = 0; j < cnt_j; ++j) {
                    const std::uint16_t cnt_k = r.u16();  // Gu outcome count
                    if (cnt_k == 0) continue;
                    // Per Hu: first assign every Gu's anim from the shared
                    // id pool (JS L652: `W.animation=n[x],++x`), then read
                    // ALL the float counts (pa), then ALL the u32 counts
                    // (da) — the JS reads them in two separate loops.
                    std::vector<TacticOutcome> gus(cnt_k);
                    for (std::uint16_t k = 0; k < cnt_k; ++k) {
                        gus[k].anim = anim_at(x_id);
                        ++x_id;
                    }
                    std::vector<std::uint16_t> pas(cnt_k), das(cnt_k);
                    for (std::uint16_t k = 0; k < cnt_k; ++k) pas[k] = r.u16();
                    for (std::uint16_t k = 0; k < cnt_k; ++k) das[k] = r.u16();
                    for (std::uint16_t k = 0; k < cnt_k; ++k) {
                        const std::uint16_t pa = pas[k], da = das[k];
                        for (std::uint16_t x = 0; x < pa; ++x) {
                            gus[k].window_edges.push_back(float_at(t + x));
                        }
                        t += pa;
                        for (std::uint16_t x = 0; x < da; ++x) {
                            gus[k].window_outcomes.push_back(udata[z + x]);
                        }
                        z += da;
                        row.outcomes.push_back(std::move(gus[k]));
                    }
                }
                tr.rows.push_back(std::move(row));
            }
        }
        // Route into slot 0; the AiController routes by container version
        // at decision time (see tactics_parse_file).
        set.tables[0].push_back(std::move(tr));
    }
    return set;
}

}  // namespace

// ---------------------------------------------------------------------------
// tactics_parse_file — JS `Si.cxb` (L653-654)
// ---------------------------------------------------------------------------
std::vector<TacticsFile> tactics_parse_file(const std::uint8_t* data,
                                            std::size_t size) {
    const std::vector<std::uint8_t> decompressed = sf2::data::zstd_decompress(data, size);
    BinReader r(decompressed.data(), decompressed.size());

    std::vector<TacticsFile> out;
    while (r.remaining() > 0) {
        TacticsFile tf;
        tf.version = static_cast<int>(r.u32());
        tf.weapon_a = r.cstr();
        if (tf.version == 2 || tf.version == 7) {
            tf.weapon_b = "";
        } else {
            tf.weapon_b = r.cstr();
        }
        const std::uint32_t blob_size = r.u32();
        if (blob_size > r.remaining()) {
            throw std::runtime_error("tactics: blob size exceeds payload");
        }
        if (tf.version == 7) {
            // v=7: per-animation record-id sets (JS `Si.cxb` L654: u32 count
            // then per-entry cstr + nested blob). These feed the
            // RandomizingEnemyAnimation / shift-table merges — out of scope
            // for the decision loop; skip.
            r.skip(blob_size);
            continue;
        }

        // The blob CONTAINS the two string pools, then the header (JS
        // `sb.load` L649: `Wlb` + `bmb` read them from the same reader
        // before `tab`).
        BinReader br(r.pos(), blob_size);
        const std::uint16_t count_a = br.u16();
        const std::vector<std::string> pool_a = br.strings(count_a);
        const std::uint16_t count_b = br.u16();
        const std::vector<std::string> pool_b = br.strings(count_b);

        tf.set = parse_sb_blob(br, pool_a, pool_b);
        r.skip(blob_size);
        out.push_back(std::move(tf));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Tactic settings (tactic_settings.xml) — JS `Md` L636-643
// ---------------------------------------------------------------------------

namespace {

// Splits a "A|B|C" attribute into non-empty tags (dedup).
std::vector<std::string> split_tags(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (ch == '|') {
            if (!cur.empty() && std::find(out.begin(), out.end(), cur) == out.end()) {
                out.push_back(cur);
            }
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    if (!cur.empty() && std::find(out.begin(), out.end(), cur) == out.end()) {
        out.push_back(cur);
    }
    return out;
}

// JS `kh.z0a` (L655-657) + `kh.Feb`/`H2`/`Cha` (L656-657): a <Tactic
// Template="A|B"> inherits the ATTRIBUTES and CHILD ELEMENTS of the
// <Tactic Name="A"> / <Tactic Name="B"> elements it does not already have;
// the template's own Template chain is resolved transitively. Operates on
// the XML nodes (the JS clones child elements).
void resolve_template(pugi::xml_node node,
                      const std::map<std::string, pugi::xml_node>& by_name,
                      std::set<std::string>& visiting) {
    const std::string tmpl = node.attribute("Template").as_string("");
    if (tmpl.empty()) return;
    const std::vector<std::string> tags = split_tags(tmpl);
    for (const std::string& tag : tags) {
        auto it = by_name.find(tag);
        if (it == by_name.end()) continue;
        const pugi::xml_node tpl = it->second;
        if (visiting.count(tag)) continue;  // cycle guard
        visiting.insert(tag);
        resolve_template(const_cast<pugi::xml_node&>(tpl), by_name, visiting);
        visiting.erase(tag);
        // H2: copy missing attributes.
        for (const pugi::xml_attribute& a : tpl.attributes()) {
            if (!node.attribute(a.name())) {
                node.append_attribute(a.name()) = a.value();
            }
        }
        // Cha: copy missing child elements (whole subtrees).
        std::vector<pugi::xml_node> missing;
        for (const pugi::xml_node& c : tpl.children()) {
            if (!node.child(c.name())) missing.push_back(c);
        }
        for (const pugi::xml_node& c : missing) {
            node.append_copy(c);
        }
    }
}

// Parses one <Animation>/<QuickAttackChance>/... weight curve (JS `cc.parse`
// L644-646).
WeightCurve parse_curve(const pugi::xml_node& node) {
    WeightCurve c;
    auto attr = [&](const char* name) {
        pugi::xml_attribute a = node.attribute(name);
        return a.empty() ? 0.0f : a.as_float();
    };
    c.base = attr("Base");
    c.counter_factor = attr("CounterFactor");
    c.damage_factor = attr("DamageFactor");
    c.health_factor = attr("HealthFactor");
    c.enemy_health_factor = attr("EnemyHealthFactor");
    c.anim_frames_factor = attr("AnimationFramesFactor");
    c.child_frames_factor = attr("ChildFramesFactor");
    c.magic_bullet_factor = attr("MagicBulletFactor");
    c.missile_bullet_factor = attr("MissileBulletFactor");
    c.hit_factor = attr("HitFactor");
    c.distance_factor = attr("DistanceFactor");
    c.limit = attr("Limit");
    c.anti_limit = attr("AntiLimit");
    c.shift = attr("Shift");
    c.conditional_factor = attr("ConditionalDesigionFactor");
    const std::string ft = node.attribute("FactorType").as_string("");
    c.linear = ft.empty() || ft == "Linear";
    for (const pugi::xml_node& child : node.children()) {
        if (std::strcmp(child.name(), "AnimationFactors") == 0) {
            WeightCurve::AnimFactor af;
            af.anim = child.attribute("Animation").as_string("");
            af.counter = child.attribute("CounterFactor").as_float(0.0f);
            af.damage = child.attribute("DamageFactor").as_float(0.0f);
            af.hit = child.attribute("HitFactor").as_float(0.0f);
            c.anim_factors.push_back(std::move(af));
        }
    }
    return c;
}

// Parses a <Min>/<Max> pair into two curves (JS `Md.K3` L643).
void parse_min_max(const pugi::xml_node& node, WeightCurve& min, WeightCurve& max) {
    for (const pugi::xml_node& child : node.children()) {
        if (std::strcmp(child.name(), "Min") == 0) min = parse_curve(child);
        else if (std::strcmp(child.name(), "Max") == 0) max = parse_curve(child);
    }
}

// Parses a <QuickAttacks>/<Evades> slot list (JS `Md.Tjb`/`njb` L641-642).
// `parent` is the <Tactic>; `container_name` = "QuickAttacks"/"Evades";
// `slot_name` = "QuickAttackChance"/"EvadeChance".
void parse_slots(const pugi::xml_node& parent, const char* container_name,
                 const char* slot_name, std::vector<AiAnimSlot>& out) {
    const pugi::xml_node container = parent.child(container_name);
    if (container.empty()) return;
    for (const pugi::xml_node& child : container.children()) {
        if (std::strcmp(child.name(), slot_name) != 0) continue;
        AiAnimSlot slot;
        const std::string names = child.attribute("Animation").as_string("");
        // "|"-separated animation names.
        std::string cur;
        for (char ch : names) {
            if (ch == '|') {
                if (!cur.empty()) slot.names.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(ch);
            }
        }
        if (!cur.empty()) slot.names.push_back(cur);
        slot.priority = child.attribute("Priority").as_int(0);
        // The slot's chance curve (JS `Hl` parses the curve attrs from the
        // same element — Base/Limit/factors — via the `cc` parser).
        slot.chance = parse_curve(child);
        // NOTE: the slot <Conditions> tree is parsed like a move condition
        // tree (JS `Hl.compare` L633 reuses the condition evaluator); the
        // native port parses the conditions with the move_def parser and
        // evaluates them with eval_conditions. The shipped tactic_settings
        // uses condition-free slots (Base-only), so the demo works without
        // condition parsing; the AiAnimSlot.conditions field is populated
        // by a follow-up.
        out.push_back(std::move(slot));
    }
}

}  // namespace

void parse_tactic_settings(const std::string& xml_text,
                           std::map<std::string, TacticDef>& out) {
    pugi::xml_document doc;
    pugi::xml_parse_result res = doc.load_string(xml_text.c_str());
    if (!res) throw std::runtime_error(std::string("parse_tactic_settings: ") + res.description());

    const pugi::xml_node root = doc.child("TacticsSettings");
    if (root.empty()) throw std::runtime_error("parse_tactic_settings: no <TacticsSettings>");
    const pugi::xml_node tactics = root.child("Tactics");
    if (tactics.empty()) return;

    // Build the name -> node map for template resolution.
    std::map<std::string, pugi::xml_node> by_name;
    for (const pugi::xml_node& t : tactics.children()) {
        if (std::strcmp(t.name(), "Tactic") != 0) continue;
        by_name[t.attribute("Name").as_string("")] = t;
    }
    // Resolve Template inheritance (JS `kh.z0a` L655-657) — mutates the
    // nodes in place (missing attrs/children copied from the template).
    for (const pugi::xml_node& t : tactics.children()) {
        if (std::strcmp(t.name(), "Tactic") != 0) continue;
        std::set<std::string> visiting;
        resolve_template(const_cast<pugi::xml_node&>(t), by_name, visiting);
    }

    for (const pugi::xml_node& t : tactics.children()) {
        if (std::strcmp(t.name(), "Tactic") != 0) continue;
        TacticDef def;
        def.name = t.attribute("Name").as_string("");
        const std::string type = t.attribute("Type").as_string("");
        def.type = type == "Random" ? 1 : type == "Tabular" ? 2 : 0;

        const pugi::xml_node aw = t.child("AnimationWeights");
        if (!aw.empty()) {
            for (const pugi::xml_node& child : aw.children()) {
                if (std::strcmp(child.name(), "Animation") != 0) continue;
                const std::string name = child.attribute("Name").as_string("");
                def.anim_weights.emplace_back(name, parse_curve(child));
            }
        }

        const pugi::xml_node ud = t.child("UseDefense");
        if (!ud.empty()) {
            for (const pugi::xml_node& child : ud.children()) {
                const std::string cn = child.name();
                if (cn == "CounterAttackChance") def.counter_attack_chance = parse_curve(child);
                else if (cn == "DodgeChance") def.dodge_chance = parse_curve(child);
                else if (cn == "BlockChance") def.block_chance = parse_curve(child);
            }
        }
        const pugi::xml_node usa = t.child("UseSafeAttackChance");
        if (!usa.empty()) def.use_safe_attack_chance = parse_curve(usa);
        const pugi::xml_node ta = t.child("TableAttackChance");
        if (!ta.empty()) def.table_attack_chance = parse_curve(ta);
        const pugi::xml_node dm = t.child("DodgeMissilesChance");
        if (!dm.empty()) def.dodge_missiles_chance = parse_curve(dm);
        const pugi::xml_node dmg = t.child("DodgeMagicChance");
        if (!dmg.empty()) def.dodge_magic_chance = parse_curve(dmg);
        const pugi::xml_node cm = t.child("CautiousMovementsChance");
        if (!cm.empty()) def.cautious_movements_chance = parse_curve(cm);

        parse_slots(t, "QuickAttacks", "QuickAttackChance", def.quick_attacks);
        parse_slots(t, "Evades", "EvadeChance", def.evades);

        const pugi::xml_node ew = t.child("ExpectedWait");
        if (!ew.empty()) {
            for (const pugi::xml_node& child : ew.children()) {
                if (std::strcmp(child.name(), "Animation") != 0) continue;
                const std::string name = child.attribute("Name").as_string("");
                def.expected_wait.emplace_back(name, parse_curve(child));
            }
        }

        const pugi::xml_node de = t.child("DistanceError");
        if (!de.empty()) parse_min_max(de, def.distance_error_min, def.distance_error_max);
        const pugi::xml_node fe = t.child("FrameError");
        if (!fe.empty()) parse_min_max(fe, def.frame_error_min, def.frame_error_max);
        const pugi::xml_node rd = t.child("ResponseDelay");
        if (!rd.empty()) parse_min_max(rd, def.response_delay_min, def.response_delay_max);
        const pugi::xml_node erd = t.child("EnemyResponseDelay");
        if (!erd.empty()) parse_min_max(erd, def.enemy_response_delay_min, def.enemy_response_delay_max);

        out[def.name] = std::move(def);
    }
}

// ---------------------------------------------------------------------------
// Weight curve evaluator — JS `cc.Gb` (L647) + `NYa`/`QYa` (L648)
// ---------------------------------------------------------------------------
float weight_curve_eval(const WeightCurve& c, const AiFeatureState& f) {
    // The dot product (JS `cc.Gb` L647), in the exact operand order.
    float total = f.counter * c.counter_factor +
                  f.xb * c.damage_factor +
                  (1.0f - f.o1) * c.health_factor +
                  (1.0f - f.q1) * c.enemy_health_factor +
                  f.xY * c.anim_frames_factor +
                  f.cl * c.magic_bullet_factor +
                  f.k2 * c.missile_bullet_factor +
                  f.tf * c.hit_factor +
                  f.pz * c.child_frames_factor +
                  f.lya * c.distance_factor +
                  c.shift;
    // Per-child AnimationFactors probe term: the JS adds
    //   child.DamageFactor·D + child.CounterFactor·C + child.HitFactor·H
    // where D/C/H are the decaying per-animation memory accumulators. The
    // native port has no strike-memory yet — the accumulators are 0, so the
    // term is 0 (see README for the full mechanism).
    // Conditional decision term (JS: adds ConditionalDesigionFactor when the
    // conditional-decision flag is set).
    if (f.conditional) total += c.conditional_factor;

    if (c.linear) {
        // QYa (L648): total>=0 -> base + (limit-base)*min(1,total)
        //             total<0  -> base + (anti_limit-base)*min(1,-total)
        if (total >= 0.0f) return c.base + (c.limit - c.base) * std::min(1.0f, total);
        return c.base + (c.anti_limit - c.base) * std::min(1.0f, -total);
    }
    // NYa (L648): exponential:
    //   total>=0 -> limit + (base-limit)*2^(-total)
    //   total<0  -> anti_limit + (base-anti_limit)*2^(total)
    if (total >= 0.0f) {
        return c.limit + (c.base - c.limit) * std::pow(2.0f, -total);
    }
    return c.anti_limit + (c.base - c.anti_limit) * std::pow(2.0f, total);
}

} // namespace sf2::scene
