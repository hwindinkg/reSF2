// reverse/analysis/atf_record_858.candidate.cpp
//
// GAP-4 step B4 / R2 — candidate C++ for the .atf AnimationFactors probe.
// [ORIGINAL] game_region_runtime.bin (base 0x8F057000, ARM:LE:32:v7):
//   * FUN_8f44ac78 @ 0x8F44AC78 — TacticWeight::evaluate (score + curve)
//   * FUN_8f4b1adc @ 0x8F4B1ADC — per-child AnimationFactors probe accumulator
//   * FUN_8f4b151c @ 0x8F4B151C — 0x1c-byte memory record find-or-create
//
// This file is documentation-grade candidate code for the @re-verifier
// contract. It is NOT wired into the engine (post-GREEN backend step,
// .planning/phases/phase-5/PLAN.md L290); nothing here is added to CMake.
//
// Conventions: native offsets in comments; [UNCERTAIN NAME] where the
// original identifier is unknown. FP term order is preserved exactly —
// do not reassociate.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace rev::atf {

// ---------------------------------------------------------------------------
// Native TacticWeight (0x60 bytes; parse at FUN_8f44c474 + FUN_8f44b4e4).
// All fields float except factor_type_bits (u32 bits: 0 = Exponential,
// 1 = Linear — the scorer float-compares against 0.0f and 1.4013e-45f).
// ---------------------------------------------------------------------------
struct AnimFactorEntry {           // 0x6c bytes, vector at weight+0x3c/+0x40
    const char* name;              // +0x00 cstr (the probe's target animation)
    // TacticWeight fields start at +0x0c, so within the entry:
    //   +0x10 = CounterFactor (weight+0x04)
    //   +0x14 = DamageFactor  (weight+0x08)
    //   +0x2c = HitFactor     (weight+0x20)
    float counter_factor;          // entry+0x10
    float damage_factor;           // entry+0x14
    float hit_factor;              // entry+0x2c
};

struct PairKV {                    // 8 bytes, vectors at weight+0x48 / +0x54
    float key;
    float value;
};

struct TacticWeightNative {        // 0x60 bytes
    float base;                    // +0x00 "Base"
    float counter_factor;          // +0x04
    float damage_factor;           // +0x08
    float health_factor;           // +0x0c
    float enemy_health_factor;     // +0x10
    float animation_frames_factor; // +0x14
    float magic_bullet_factor;     // +0x18
    float missile_bullet_factor;   // +0x1c
    float hit_factor;              // +0x20
    float child_frames_factor;     // +0x24
    float distance_factor;         // +0x28
    float limit;                   // +0x2c ("Limit" read via "AntiLimit"+4)
    float anti_limit;              // +0x30
    float shift;                   // +0x34
    std::uint32_t factor_type_bits;// +0x38 (0 = Exponential, 1 = Linear)
    std::vector<AnimFactorEntry> children;  // +0x3c/+0x40 <AnimationFactors>
    std::vector<PairKV> pair_list1;         // +0x48/+0x4c [UNCERTAIN NAME]
    std::vector<PairKV> pair_list2;         // +0x54/+0x58 [UNCERTAIN NAME]
};

// ---------------------------------------------------------------------------
// Score ctx (FUN_8f45342C @ 0x8F45342C builds it; float* in the scorer).
// ---------------------------------------------------------------------------
struct TacticCtxNative {
    float    counter;              // [0]  from FUN_8f4b1914 (memory, slot 1)
    float    damage;               // [1]  from FUN_8f4b1914
    float    health;               // [2]  self health, normalized 0..1
    float    enemy_health;         // [3]  enemy health, normalized 0..1
    std::int32_t anim_frames;      // [4]  VCVT int -> float in the scorer
    std::int32_t missile_bullets;  // [5]  VCVT
    std::int32_t magic_bullets;    // [6]  VCVT
    float    hits;                 // [8]  from FUN_8f4b1914
    std::int32_t child_frames;     // [9]  VCVT
    float    distance;             // [10]
    float    pair_key1;            // [11] [UNCERTAIN NAME] (an id as float)
    float    pair_key2;            // [12] [UNCERTAIN NAME]
    void*    memory;               // [13] probe object P; M = *(void**)P
};

// ---------------------------------------------------------------------------
// TacticMemory record — 0x1c bytes (FUN_8f4b151C element).
// ---------------------------------------------------------------------------
struct AtfMemoryRecord {
    std::uint32_t id;              // +0x00 record id (interned animation)
    float           f04;           // +0x04 [UNCERTAIN NAME] accumulator
    float           damage;        // +0x08 damage accumulator
    float           counter;       // +0x0c counter accumulator
    float           f10;           // +0x10 [UNCERTAIN NAME] accumulator
    float           hits;          // +0x14 hits accumulator
    std::int32_t    last_frame;    // +0x18 last probe/update frame
};

struct TacticMemoryNative {        // the probe object P (ctx.memory)
    // +0x04  records_slot1 (probe uses slot==1 -> P+4)
    // +0x10  records_slot0 (slot==0 -> P+0x10)
    std::vector<AtfMemoryRecord> records_slot0;
    std::vector<AtfMemoryRecord> records_slot1;
    // M = *(void**)P carries:
    float*        decay_rate;      // M+0x6c  (pointer to the rate float)
    std::int32_t  decay_enabled;   // M+0x634 (gate for the rate override)
    std::int32_t  frame_counter;   // M+0x71c (current frame)
};

// [ORIGINAL] 0x8F45BAD4 — global map {animation name -> record-id list}.
// Implemented in the binary as a vector scan + strcmp + merge-unique
// (FUN_8f45b930). Ids are interned-animation record ids built at load time.
void atf_gather_record_ids(const char* animation_name, std::vector<std::uint32_t>& out_ids);

// [ORIGINAL] 0x8F4B151C — find-or-create a 0x1c record keyed by id.
static AtfMemoryRecord* atf_record_find_or_create(std::vector<AtfMemoryRecord>& vec,
                                                  std::uint32_t id)
{
    for (AtfMemoryRecord& r : vec) {
        if (r.id == id) return &r;
    }
    vec.push_back(AtfMemoryRecord{ id, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0 });
    return &vec.back();
}

// ---------------------------------------------------------------------------
// [ORIGINAL] FUN_8f4b1adc @ 0x8F4B1ADC
// Per-child probe: gather the child's record ids, read each record with lazy
// exponential decay, sum the three accumulators. Mutates the records
// (decay write-back + unconditional last_frame update).
// ---------------------------------------------------------------------------
static void atf_probe_accumulate(TacticMemoryNative* mem, int slot,
                                 const char* child_name,
                                 float* out_counter, float* out_damage,
                                 float* out_hits)
{
    *out_counter = 0.0f;
    *out_damage = 0.0f;
    *out_hits = 0.0f;

    std::vector<std::uint32_t> ids;
    atf_gather_record_ids(child_name, ids);              // FUN_8f45BAD4

    std::vector<AtfMemoryRecord>& vec =
        slot ? mem->records_slot1 : mem->records_slot0;  // slot==1 -> P+4

    for (std::uint32_t id : ids) {
        AtfMemoryRecord* rec = atf_record_find_or_create(vec, id);

        const int cur = mem->frame_counter;              // FUN_8f4A6E44: M+0x71c
        float rate = 0.0f;                               // DAT_8f4b1c68 = 0.0f
        if (mem->decay_enabled != 0) {                   // FUN_8f4A7220: M+0x634
            const float* p = mem->decay_rate;            // FUN_8f454344:  M+0x6c
            if (p != nullptr) rate = *p;
        }

        const int frames = cur - rec->last_frame;
        float c, d, h;
        if (frames < 1) {
            c = rec->counter;
            d = rec->damage;
            h = rec->hits;
        } else {
            const float k = std::pow(2.0f, -(float)frames / rate);  // FUN_8f72ED40
            d = k * rec->damage;   rec->damage  = d;
            c = k * rec->counter;  rec->f04     = rec->f04 * k;
            h = k * rec->hits;     rec->counter = c;
            rec->hits = h;         rec->f10     = rec->f10 * k;
        }
        rec->last_frame = cur;                           // unconditional

        *out_counter += c;
        *out_damage   += d;
        *out_hits     += h;
    }
}

// ---------------------------------------------------------------------------
// [ORIGINAL] FUN_8f44ac78 @ 0x8F44AC78 — TacticWeight::evaluate.
// Gb() dot product + per-child probe term + two pair-list lookups, then the
// Linear/Exponential curve. Native term order preserved.
// ---------------------------------------------------------------------------
float tactic_weight_evaluate(const TacticWeightNative& w, const TacticCtxNative& ctx)
{
    // --- Gb(): the dot product. NOTE: damage*DamageFactor comes FIRST
    // (the engine's current score() has counter first — the binary does not).
    float a = ctx.damage * w.damage_factor
            + ctx.counter * w.counter_factor
            + (1.0f - ctx.health) * w.health_factor
            + (1.0f - ctx.enemy_health) * w.enemy_health_factor
            + (float)ctx.anim_frames * w.animation_frames_factor
            + (float)ctx.magic_bullets * w.magic_bullet_factor
            + (float)ctx.missile_bullets * w.missile_bullet_factor
            + ctx.hits * w.hit_factor
            + (float)ctx.child_frames * w.child_frames_factor
            + ctx.distance * w.distance_factor
            + w.shift;

    // --- the a.a6.S5a probe: one term per <AnimationFactors> child.
    // a += child.DamageFactor*D + child.CounterFactor*C + child.HitFactor*H
    if (!w.children.empty()) {
        for (const AnimFactorEntry& child : w.children) {
            float c = 0.0f, d = 0.0f, h = 0.0f;
            atf_probe_accumulate((TacticMemoryNative*)ctx.memory, /*slot=*/1,
                                 child.name, &c, &d, &h);
            a += child.damage_factor * d
               + child.counter_factor * c
               + child.hit_factor * h;
        }
    }

    // --- pair-list lookups: linear search from begin, first match wins;
    // default DAT_8f44b0e4 = 0.0f. (The binary copies each list to a temp
    // buffer first — an inlined std::vector copy; semantically identical.)
    float v1 = 0.0f;
    for (const PairKV& kv : w.pair_list1) {
        if (kv.key == ctx.pair_key1) { v1 = kv.value; break; }
    }
    float v2 = 0.0f;
    for (const PairKV& kv : w.pair_list2) {
        if (kv.key == ctx.pair_key2) { v2 = kv.value; break; }
    }
    a = a + v1 + v2;

    // --- curve. factor_type at +0x38 as raw bits: 0 -> Exponential
    // (scorer compares == 0.0f), 1 -> Linear (compares == 1.4013e-45f).
    if (w.factor_type_bits == 0) {                       // Exponential
        if (a >= 0.0f) {
            return w.limit + (w.base - w.limit) * std::pow(2.0f, -a);
        }
        return w.anti_limit + (w.base - w.anti_limit) * std::pow(2.0f, a);
    }
    if (w.factor_type_bits == 1) {                       // Linear
        if (a >= 0.0f) {
            float t = 1.0f;
            if (a <= 1.0f) t = a;
            return w.base + (w.limit - w.base) * t;
        }
        const float t = (a < -1.0f) ? 1.0f : -a;
        return w.base + (w.anti_limit - w.base) * t;
    }
    return 0.0f;                                         // DAT_8f44b0e4
}

// ---------------------------------------------------------------------------
// Engine-facing mapping for TacticTableSet::animation_factor(anim, target)
// (ADR-005 D5). There is NO (anim,target)->float table in the binary, and
// NO scalar "AnimationFactors" attribute on the weight (0x8F44C474 reads 15
// attributes; "AnimationFactors" appears only as a child *element* name).
// The probe term for (anim, target) is the contribution of the child entry
// named `target` of anim's weight:
// ---------------------------------------------------------------------------
float animation_factor(const TacticWeightNative& anim_weight,
                       const char* target,
                       TacticMemoryNative* memory)
{
    for (const AnimFactorEntry& child : anim_weight.children) {
        // binary compares the child name cstr (FUN_8f73BD3C == strcmp)
        if (child.name == nullptr || target == nullptr) continue;
        if (std::strcmp(child.name, target) != 0) continue;
        float c = 0.0f, d = 0.0f, h = 0.0f;
        atf_probe_accumulate(memory, /*slot=*/1, child.name, &c, &d, &h);
        return child.damage_factor * d
             + child.counter_factor * c
             + child.hit_factor * h;
    }
    return 0.0f;    // absent child/table/records: neutral (ADR-005 R2 rule)
}

}  // namespace rev::atf
