// memory_indexing_r56.candidate.cpp
//
// GAP-4 step P5 (R5 + R6 + approved v=7 extension) — candidate C++ for the
// Memory/QuickAttack/Evade indexing items. Docs-only deliverable; nothing
// here is wired into engine/ or CMake.
//
// One clearly-labeled candidate per item:
//   R5 depth      -> atf_record_find_or_create   (FUN_8f4b151c @ 0x8F4B151C)
//   R5 strikes    -> atf_memory_feed_strike      (FUN_8f4b173c @ 0x8F4B173C)
//   R5 intervals  -> animplayer_select_intervals (FUN_8f47b528 @ 0x8F47B528)
//   R6 indexing   -> tactic_score_quick_attacks  (FUN_8f45456c @ 0x8F45456C)
//   v=7 semantics -> atf_gather_record_ids       (FUN_8f45bad4 @ 0x8F45BAD4)
//
// Shared native layout (anchors in MEMORY_INDEXING_R56.md):

#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>

struct Fighter;          // the 0x724-byte fighter object (ctor FUN_8f4acb70)
struct TacticNative;     // the 0x648-byte tactic object (ctor FUN_8f4bd670)

// ---------------------------------------------------------------------------
// Memory record — 0x1c bytes, owned by the fighter-embedded TacticMemory
// struct at fighter+0x638 (ctor FUN_8f4b0dac):
//   +0x00  Fighter* owner
//   +0x04  records_slot1 vector (initial capacity 20 records = 0x230 bytes)
//   +0x10  records_slot0 vector (initial capacity 20 records)
// ---------------------------------------------------------------------------
struct AtfMemoryRecord {              // 0x1c bytes
    std::uint32_t anim_record_id;     // +0x00 interned animation record ptr (NOT an int id)
    float         strike_damage;      // +0x04 fed by FUN_8f4b173c (+= damage amount)
    float         damage;             // +0x08 read by probe as "D" — NEVER fed in this build
    float         counter;            // +0x0c fed by FUN_8f4b1830 (+= 1) / read as "C"
    float         strike_count;       // +0x10 fed by FUN_8f4b173c (+= 1)
    float         hits;               // +0x14 read by probe as "H" — NEVER fed in this build
    std::int32_t  last_frame;         // +0x18 last decay stamp (owner+0x71c units)
};

struct TacticMemoryStruct {           // fighter+0x638, from FUN_8f4b0dac
    Fighter*                 owner;
    std::vector<AtfMemoryRecord> records_slot1;   // +0x04
    std::vector<AtfMemoryRecord> records_slot0;   // +0x10
};

// ===========================================================================
// R5 (depth) — [ORIGINAL] FUN_8f4b151c @ 0x8F4B151C
//
// Per-record-id find-or-create inside one slot vector. The Memory "ring
// depth" answer: there is NO ring and NO depth cap — the record vector grows
// by doubling (1 -> 2 -> 4 ... records) and no record is ever evicted. The
// effective memory depth is purely the decay (Strikes rate + RoundFactor
// round-end scale), not a structural bound. (Vector realloc scaffolding is
// elided exactly like the ATF_RECORD_858 candidate elided pair-list copies;
// growth policy: newcap = count ? 2*count : 1 records, 0x1c bytes each.)
// ===========================================================================
AtfMemoryRecord* atf_record_find_or_create(TacticMemoryStruct* P, int slot,
                                           std::uint32_t anim_record_id)
{
    std::vector<AtfMemoryRecord>& vec =
        (slot == 0) ? P->records_slot0       // slot==0 -> P+0x10
                    : P->records_slot1;      // else    -> P+0x04
    const std::size_t count = vec.size();    // (end-begin)>>2 * 0x49249249
    if (count != 0) {
        for (AtfMemoryRecord* r = vec.data(); r < vec.data() + count; ++r) {
            if (r->anim_record_id == anim_record_id) {
                return r;                     // found
            }
        }
    }
    if (vec.size() == vec.capacity()) {
        vec.reserve(vec.capacity() ? vec.capacity() * 2 : 1);  // doubling growth
    }
    AtfMemoryRecord* rec = &vec.emplace_back();  // zero-init order: id, then 6 dwords
    rec->anim_record_id = anim_record_id;
    rec->strike_damage = 0.0f;
    rec->damage        = 0.0f;
    rec->counter       = 0.0f;
    rec->strike_count  = 0.0f;
    rec->hits          = 0.0f;
    rec->last_frame    = 0;
    return rec;
}

// ===========================================================================
// R5 (strikes) — [ORIGINAL] FUN_8f4b173c @ 0x8F4B173C
//
// THE strike update point. Called on every damage application with the
// ATTACKER's current animation record id — for the victim (slot=1) and the
// attacker (slot=0) alike (feed site FUN_8f4aa998 @ 0x8F4AA998 tail, from
// FUN_8f4aafc0 @ 0x8F4AAFC0, the hit-application path).
// Lazy decay: if cur - rec->last_frame >= 1, k = powf(2, -frames/rate) scales
// damage/counter/hits IN PLACE (write-back), while strike_damage/strike_count
// are decayed into locals and then overwritten by (amount + k*old) /
// (1 + k*old). rate = tactic's Strikes float (fighter+0x6c -> float*, [0] =
// Strikes, [1] = RoundFactor) when the AI object (fighter+0x634) exists;
// otherwise 0.0f (DAT_8f4b182c). cur = fighter+0x71c — the victim's
// hits-taken event counter, NOT a video frame counter.
// ===========================================================================
float atf_memory_decay_rate(const Fighter* owner)
{
    float rate = 0.0f;                                  // DAT_8f4b182c = 0.0f
    if (*reinterpret_cast<const std::int32_t*>(
            reinterpret_cast<const char*>(owner) + 0x634) != 0) {   // AI object
        const float* p = *reinterpret_cast<float* const*>(
            reinterpret_cast<const char*>(owner) + 0x6c);           // tactic ptr
        if (p != nullptr) {
            rate = *p;                                  // tactic->strikes (+0x00)
        }
    }
    return rate;
}

void atf_memory_feed_strike(TacticMemoryStruct* P, int slot,
                            std::uint32_t anim_record_id, float amount)
{
    const std::int32_t cur = *reinterpret_cast<std::int32_t*>(
        reinterpret_cast<char*>(P->owner) + 0x71c);     // hits-taken counter
    const float rate = atf_memory_decay_rate(P->owner);
    AtfMemoryRecord* rec = atf_record_find_or_create(P, slot, anim_record_id);
    const std::int32_t frames = cur - rec->last_frame;
    float sd, sc;
    if (frames < 1) {
        sd = rec->strike_damage;
        sc = rec->strike_count;
    } else {
        const float k = std::pow(2.0f, -(float)frames / rate);   // FUN_8f72ed40
        rec->damage  *= k;                            // write-back order:
        rec->counter *= k;                            //   +0x08, +0x0c,
        sd = k * rec->strike_damage;                  //   (local +0x04),
        rec->hits    *= k;                            //   +0x14,
        sc = k * rec->strike_count;                   //   (local +0x10)
    }
    rec->last_frame = cur;                            // unconditional
    rec->strike_damage = amount + sd;                 // += amount on decayed base
    rec->strike_count  = sc + 1.0f;                   // += 1 on decayed base
}

// Sibling [ORIGINAL] FUN_8f4b1830 @ 0x8F4B1830 — the "Uninterrupt" animation
// event feed (FUN_8f4a5478 @ 0x8F4A5478): identical decay, then
// rec->counter = decayed_counter + 1.0f. Write-back order in the decay
// branch: +0x08, +0x04, +0x14, (local +0x0c), +0x10.
void atf_memory_feed_counter(TacticMemoryStruct* P, int slot,
                             std::uint32_t anim_record_id)
{
    const std::int32_t cur = *reinterpret_cast<std::int32_t*>(
        reinterpret_cast<char*>(P->owner) + 0x71c);
    const float rate = atf_memory_decay_rate(P->owner);
    AtfMemoryRecord* rec = atf_record_find_or_create(P, slot, anim_record_id);
    const std::int32_t frames = cur - rec->last_frame;
    float c;
    if (frames < 1) {
        c = rec->counter;
    } else {
        const float k = std::pow(2.0f, -(float)frames / rate);
        rec->damage        *= k;
        rec->strike_damage *= k;
        rec->hits          *= k;
        c = k * rec->counter;
        rec->strike_count  *= k;
    }
    rec->last_frame = cur;
    rec->counter = c + 1.0f;
}

// ===========================================================================
// R5 (intervals) — [ORIGINAL] FUN_8f47b528 @ 0x8F47B528
//
// THE Intervals/EnemyIntervals reset point. Called from FUN_8f45f6ac
// (@ 0x8F45F6AC) whenever the current animation frame crosses the +0x78
// window boundary (per-frame check FUN_8f46046c @ 0x8F46046C) and on
// current-move change. Both output vectors are CLEARED at entry
// (active->end = active->begin, expiring->end = expiring->begin), then
// refilled from the current move's interval list:
//   active   <- intervals with max(start, move->f74) <= frame
//   expiring <- intervals with max(start, move->f74) >  frame AND
//               frame - 1 == min(end, (u8)move->f78)
// Records: move-side interval records {+0x04 start, +0x08 end,
// +0x0c std::string name, +0x18 type}. filter = a std::map<int,...>-style
// balanced tree of EXCLUDED type ids (lower_bound(type) with key == type
// skips the record); animplayer+0x4c supplies it at the call site.
// ===========================================================================
struct IntervalRec {                    // move-side interval record
    std::uint32_t field_00;             // +0x00 [UNCERTAIN NAME]
    std::int32_t  start;                // +0x04
    std::int32_t  end;                  // +0x08
    char*         name_begin;           // +0x0c (std::string)
    char*         name_end;             // +0x10
    char*         name_cap;             // +0x14
    std::int32_t  type;                 // +0x18 (0 Dodge,1 Unstable,2 SelfUninterrupt,
                                        //      3 Attack,4 Invulnerable,5 Invisible)
};

struct IntervalTypeNode {               // filter tree node (std::map node)
    IntervalTypeNode* left;             // +0x00
    IntervalTypeNode* parent;           // +0x04 [UNCERTAIN NAME]
    IntervalTypeNode* right;            // +0x08
    IntervalTypeNode* prev;             // +0x0c [UNCERTAIN NAME]
    std::int32_t    key;                // +0x10 (the type id)
};

struct AnimMoveRecord {
    // ... +0x74: frame base [UNCERTAIN NAME], +0x78: window u8 [UNCERTAIN NAME]
    std::int32_t frame_base;            // +0x74
    std::uint8_t window;                // +0x78
    // +0x94 -> node holding the interval pointer vector at +0x28/+0x2c
};

static bool interval_type_filtered(const IntervalTypeNode* root, std::int32_t type)
{
    if (root == nullptr || root->parent == nullptr) return false;  // empty tree
    const IntervalTypeNode* result = root;
    const IntervalTypeNode* node = root->parent;
    while (node != nullptr) {
        if (node->key < type) {
            node = node->prev;
        } else {
            result = node;
            node = node->right;
        }
    }
    return result != root && result->key <= type;   // lower_bound hit == excluded
}

void animplayer_select_intervals(AnimMoveRecord* move, std::uint32_t frame,
                                 std::vector<IntervalRec*>* active,
                                 std::vector<IntervalRec*>* expiring,
                                 const IntervalTypeNode* filter)
{
    active->clear();                    // param_3[1] = *param_3  — THE reset
    expiring->clear();                  // param_4[1] = *param_4
    IntervalRec** it  = *reinterpret_cast<IntervalRec***>(
        *reinterpret_cast<char**>(reinterpret_cast<char*>(move) + 0x94) + 0x28);
    IntervalRec** end = *reinterpret_cast<IntervalRec***>(
        *reinterpret_cast<char**>(reinterpret_cast<char*>(move) + 0x94) + 0x2c);
    for (; it < end; ++it) {
        IntervalRec* rec = *it;
        std::int32_t s = rec->start;
        const std::int32_t base = *reinterpret_cast<std::int32_t*>(
            reinterpret_cast<char*>(move) + 0x74);
        if (s < base) s = base;                                   // cpycc
        std::int32_t e = rec->end;
        const std::uint32_t w = *reinterpret_cast<std::uint8_t*>(
            reinterpret_cast<char*>(move) + 0x78);
        if ((std::int32_t)w <= e) e = (std::int32_t)w;            // cpyge
        if (s <= (std::int32_t)frame) {
            if (!interval_type_filtered(filter, rec->type)) {
                active->push_back(rec);
            }
        } else if (frame - 1 == (std::uint32_t)e) {
            if (!interval_type_filtered(filter, rec->type)) {
                expiring->push_back(rec);
            }
        }
    }
}

// ===========================================================================
// R6 (indexing) — [ORIGINAL] FUN_8f45456c @ 0x8F45456C
//
// Per-decision QuickAttack scoring. The i-th <QuickAttackChance> entry
// (0x6c-byte {std::string animation_name @+0x00, TacticWeight @+0x0c},
// tactic+0x1f8 vector) maps 1:1 onto the i-th score slot (0xc-byte
// {score, threshold, decided}, AI_state+0x8c). The index is pure XML
// document order (1-based in the tracer's "QuickAttack[%d]" print);
// the entry's identity is its ANIMATION NAME — not a table, not a record
// id. decided[i] = (threshold[i] < score[i]); thresholds are rolled once
// per slot by FUN_8f45a920 (@ 0x8F45A920, FUN_8f264414 rng) when the score
// vector grows, and persist across decisions.
// The Evade side is the same shape: entries tactic+0x204 (parser
// FUN_8f447d44), scores AI_state+0x98 (threshold roll FUN_8f45aa2c,
// score loop inlined in FUN_8f45ab38).
// ===========================================================================
struct ChanceEntry {                    // 0x6c bytes
    char* name;                         // +0x00 (std::string: Animation attr)
    // +0x04/+0x08 string tail
    std::uint8_t weight[0x60];          // +0x0c TacticWeight (opaque here)
};

struct ScoreSlot {                      // 0xc bytes
    float score;                        // +0x00
    float threshold;                    // +0x04 (rng, rolled by FUN_8f45a920)
    bool  decided;                      // +0x08
};

struct AiState {
    // ...
    TacticNative* tactic;               // +0x6c
    // ...
    std::vector<ScoreSlot> qa_scores;   // +0x8c
    std::vector<ScoreSlot> evade_scores;// +0x98
};

float tactic_weight_evaluate(const void* weight, const void* ctx);  // FUN_8f44ac78

static const ChanceEntry* tactic_quick_attacks(const TacticNative* t)
{                                                     // FUN_8f446b70: tactic+0x1f8
    return *reinterpret_cast<ChanceEntry* const*>(
        reinterpret_cast<const char*>(t) + 0x1f8);
}
static const ChanceEntry* tactic_quick_attacks_end(const TacticNative* t)
{
    return *reinterpret_cast<ChanceEntry* const*>(
        reinterpret_cast<const char*>(t) + 0x1fc);
}

void tactic_score_quick_attacks(AiState* st, const void* ctx)
{
    const ChanceEntry* qa  = tactic_quick_attacks(st->tactic);
    const ChanceEntry* end = tactic_quick_attacks_end(st->tactic);
    const int count = (int)((reinterpret_cast<const char*>(end) -
                             reinterpret_cast<const char*>(qa)) / 0x6c);
    for (int i = 0; i < count; ++i) {
        const float score =
            tactic_weight_evaluate(qa[i].weight /* +0x0c */, ctx);  // FUN_8f44ac78
        ScoreSlot& slot = st->qa_scores[i];
        slot.score = score;
        slot.decided = slot.threshold < score;
    }
}

// ===========================================================================
// v=7 (record-id sets) — [ORIGINAL] FUN_8f45bad4 @ 0x8F45BAD4
//
// The name -> record-id set lookup feeding the probe and the QA/Evade
// candidate expansion. Map @ 0x8F86F258: vector of {const char* name @+0x00,
// std::vector<uint32_t> ids @+0x0c}. NOTE (xref audit, MEMORY_INDEXING_R56.md
// sec. 5): nothing in this dump ever appends an entry — the map stays empty
// at runtime, so this gather always returns 0 here. merge_unique appends the
// ids not already present and returns the number appended
// (FUN_8f45b930 @ 0x8F45B930).
// ===========================================================================
struct NameIdsEntry {                   // 0x18 bytes
    const char*            name;        // +0x00
    std::vector<std::uint32_t> ids;     // +0x0c
};

static int merge_unique(std::vector<std::uint32_t>& dst,
                        const std::vector<std::uint32_t>& src)
{                                                     // FUN_8f45b930
    int added = 0;
    for (std::uint32_t id : src) {
        bool present = false;
        for (std::uint32_t have : dst) {
            if (have == id) { present = true; break; }
        }
        if (!present) {
            dst.push_back(id);
            ++added;
        }
    }
    return added;
}

static std::vector<NameIdsEntry> g_name_ids_map;      // @ 0x8F86F258 (empty here)

int atf_gather_record_ids(const char* name, std::vector<std::uint32_t>& out)
{
    for (const NameIdsEntry& e : g_name_ids_map) {
        if (std::strcmp(name, e.name) == 0) {         // FUN_8f73bd3c
            return merge_unique(out, e.ids);
        }
    }
    return 0;
}
