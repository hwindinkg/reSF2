// ============================================================================
// R4 candidate: FUN_8f446cb4 @ 0x8F446CB4 — weighted-random pick of one
// candidate animation. The "picked <Animation> weight" step of the decision.
// Binary: reverse/binaries/game_region_runtime.bin (base 0x8F057000, ARM:LE:32:v7)
//   deps: FUN_8f44ac78 @ 0x8F44AC78 — TacticWeight evaluate (R2 GREEN)
//         FUN_8f47cf1c @ 0x8F47CF1C — anim-name match (name @ anim+0x7c,
//                                      alt-name list @ anim+0xa8)
//         FUN_8f264414 @ 0x8F264414 / FUN_8f26447c @ 0x8F26447C — rolls
// Layout per decompile + ASM (0x8F446CB4..0x8F446F38). Field names not
// provable from the binary are tagged [UNCERTAIN NAME]; FP order as compiled.
//
// Full chain (see reverse/analysis/DECISION_SEMANTICS.md, section R4):
//   ExpectedWait weight w = FUN_8f446b98(table+0x63c, currentAnim, ctx)
//     → attack gate P = 1/w (FUN_8f459b44); weights also drive this roulette;
//   the Wait frames printed by "Decision: %s {Wait=%d}" come from the
//   {anim,value} pair values (animFrames/animRange/enemy-duration formulas),
//   NOT from the weight. This function returns the pick index; the caller
//   reads wait frames at decision+0x18[idx] (FUN_8f4550e0 split).
// ============================================================================

// ---- externs (binary addresses) --------------------------------------------
extern float FUN_8f44ac78(const void* weight /*rec+0x0c*/, const void* ctx);
extern unsigned FUN_8f264a34(void);   // PRNG part A
extern unsigned FUN_8f264a64(void);   // PRNG part B

// FUN_8f264414 @ 0x8F264414 — u1/u2 + (u3/u4)/u5; not clamped to [0,1]
static float roll01(void) {
    float f6 = (float)FUN_8f264a34(), f3 = (float)FUN_8f264a64();
    float f7 = (float)FUN_8f264a34(), f4 = (float)FUN_8f264a64();
    float f5 = (float)FUN_8f264a64();
    return f6 / f3 + (f7 / f4) / f5;   // exact op order as compiled
}

// FUN_8f47cf1c @ 0x8F47CF1C — 1 iff name == anim name or in anim alt-name list
static int animMatchesName(const void* anim, const char* name) {
    const char* s = *(const char**)((const char*)anim + 0x7c);
    const char* e = *(const char**)((const char*)anim + 0x80);
    int len = (int)strlen(name);
    if (len == (int)(e - s) && memcmp(s, name, (size_t)len) == 0) return 1;
    const char** it  = *(const char***)((const char*)anim + 0xa8);
    const char** end = *(const char***)((const char*)anim + 0xac);
    while (it < end) {                       // 12-byte entries {ptr,len,cap}
        if (strcmp(it[0], name) == 0) return 1;
        it += 3;
    }
    return 0;
}

// record count of a 0x6c-stride vector: the binary divides the byte count by
// 27 via magic constant (words * 0x97B425ED) >> 36 (rendered -0x684bda13).
static int recordCount(const void* vec /*{begin,end} ptrs*/) {
    const char* b = *(const char**)vec;
    const char* e = *(const char**)((const char*)vec + 4);
    unsigned words = (unsigned)(e - b) >> 2;
    return (int)(((unsigned long long)words * 0x97B425EDu) >> 36); // == words/27
}

// candidate weight: first AnimationWeights record (list @ TacticSet+0x630)
// with empty name or name == anim name; default 0.0f (DAT_8f446f40)
static float candidateWeight(const void* table, const void* anim, const void* ctx) {
    const char** rec    = *(const char***)((const char*)table + 0x630);
    int count = recordCount((const char*)table + 0x630);
    for (int i = 0; i < count; ++i) {
        const char* name = rec[0];
        if (name == rec[1] /*empty name = default record*/ ||
            animMatchesName(anim, name)) {
            return FUN_8f44ac78((const char*)rec + 0x0c, ctx);
        }
        rec += 0x1b;                          // 0x6c stride
    }
    return 0.0f;
}

// FUN_8f446cb4 @ 0x8F446CB4 — returns index into the candidate vector or -1.
//   table    : TacticSet (records @ +0x630; +0x63c = ExpectedWait list, unused here)
//   cand     : candidate vector {begin,end} of anim-object ptrs (decision+0xd0);
//              id == 0 = "current animation" (uses `filter` when set)
//   filter   : current-animation object (decision+0x58), may be 0
//   ctx      : TacticContext (FUN_8f45342c output)
int pickIndexByWeight(const void* table, const int* cand,
                      const void* filter, const void* ctx) {
    const int* end = *(const int**)((const char*)cand + 4);
    const int* it  = *(const int**)cand;

    // pass 1: sum of weights over filter-eligible candidates
    float sum = 0.0f;
    while (it < end) {
        const void* anim;
        int id = *it;
        if (id != 0) {
            anim = (const void*)id;
        } else {
            ++it;
            if (it >= end) break;
            id = *it;
            if (id != 0) {
                anim = (const void*)id;
            } else if (filter != 0) {
                anim = filter;                // zero slot = current animation
            } else {
                continue;                     // skip zero slot, no filter
            }
        }
        sum += candidateWeight(table, anim, ctx);
        ++it;
    }

    if (sum > 0.0f) {
        float point = sum * roll01();         // FUN_8f26447c(rng, sum)
        it  = *(const int**)cand;
        int idx = 0;                          // counts processed slots
        while (it < end) {
            const void* anim;
            int id = *it;
            if (id != 0) {
                anim = (const void*)id;
            } else {
                ++it;
                if (it >= end) return -1;
                id = *it;
                if (id != 0) {
                    anim = (const void*)id;
                } else if (filter != 0) {
                    anim = filter;
                } else {
                    continue;
                }
            }
            point -= candidateWeight(table, anim, ctx);
            if (point < 0.0f) return idx;
            ++it;
            ++idx;
        }
    }
    return -1;
}
