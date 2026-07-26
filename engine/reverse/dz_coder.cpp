// engine/reverse/dz_coder.cpp
//
// See dz_coder.hpp for the provenance of every function below.

#include "dz_coder.hpp"

#include <cstring>

namespace resf2::dz {
namespace {

// ---------------------------------------------------------------------------
// [ORIGINAL] Model — adaptive frequency table stored as a complete binary tree.
//   node i has children 2i+1 / 2i+2; internal nodes hold the sum of their
//   left subtree, leaves hold symbol frequencies (uint16).
// Built by FUN_00408fe0, rescaled by FUN_004087b0, summed by FUN_004088a0.
// ---------------------------------------------------------------------------
class Model {
public:
    void init(uint32_t n_sym, uint32_t inc, uint32_t max_total) {
        n_sym_ = n_sym;
        inc_ = inc;
        max_total_ = max_total;

        const uint32_t n = n_sym * 2u - 1u;
        n_internal_ = n - n_sym;  // == n_sym - 1

        // [ORIGINAL] FUN_00408fe0: tree_off_ is the largest (2^k - 1) < n.
        tree_off_ = 0;
        if (n > 1) {
            uint32_t v = 1;
            while (v < n) {
                tree_off_ = v;
                v = tree_off_ * 2u + 1u;
            }
        }
        split_ = n_sym - (tree_off_ - n_internal_);

        tree_.assign(n, 0);
        reset();
    }

    // [ORIGINAL] FUN_00409300 re-runs FUN_00408fe0 with all frequencies = 1
    // at the start of every entry.
    void reset() {
        std::fill(tree_.begin(), tree_.end(), static_cast<uint16_t>(0));
        for (uint32_t s = 0; s < split_; ++s)
            tree_[tree_off_ + s] = 1;
        for (uint32_t s = split_; s < n_sym_; ++s)
            tree_[n_internal_ + (s - split_)] = 1;
        rebuild_sums(0);
        total_ = n_sym_;
    }

    uint32_t total() const { return total_; }
    void set_total(uint32_t t) { total_ = t; }
    uint32_t inc() const { return inc_; }
    uint32_t max_total() const { return max_total_; }
    uint32_t n_internal() const { return n_internal_; }
    uint32_t n_sym() const { return n_sym_; }
    uint32_t tree_off() const { return tree_off_; }
    uint16_t* tree() { return tree_.data(); }

    // [ORIGINAL] FUN_004087b0 — halve every leaf, rebuild internal sums.
    uint32_t rescale() { return rescale_node(0); }

private:
    uint32_t rebuild_sums(uint32_t i) {  // [ORIGINAL] FUN_004088a0
        if (i >= n_internal_) return tree_[i];
        const uint32_t a = rebuild_sums(2 * i + 1);
        const uint32_t b = rebuild_sums(2 * i + 2);
        tree_[i] = static_cast<uint16_t>(a);
        return a + b;
    }

    uint32_t rescale_node(uint32_t i) {
        if (i >= n_internal_) {
            uint16_t f = tree_[i];
            f = static_cast<uint16_t>(f - (f >> 1));
            tree_[i] = f;
            return f;
        }
        const uint32_t a = rescale_node(2 * i + 1);
        const uint32_t b = rescale_node(2 * i + 2);
        tree_[i] = static_cast<uint16_t>(a);
        return a + b;
    }

    std::vector<uint16_t> tree_;
    uint32_t n_sym_ = 0;
    uint32_t n_internal_ = 0;
    uint32_t tree_off_ = 0;
    uint32_t split_ = 0;
    uint32_t total_ = 0;
    uint32_t inc_ = 0;
    uint32_t max_total_ = 0;
};

// ---------------------------------------------------------------------------
// [ORIGINAL] Carry-less range decoder — FUN_00408810 (init) / FUN_00408e70.
// ---------------------------------------------------------------------------
class RangeDecoder {
public:
    RangeDecoder(const uint8_t* data, size_t size)
        : d_(data), size_(size) {
        for (int i = 0; i < 4; ++i)
            code_ = (code_ << 8) | next_byte();
    }

    uint32_t decode(Model& m) {
        uint32_t total = m.total();
        uint32_t low = low_;
        uint32_t code = code_;
        uint16_t* tree = m.tree();

        const uint32_t r = range_ / total;
        const uint32_t target = (code - low) / r;
        const uint32_t inc = m.inc();

        uint32_t node = 0;
        uint32_t cum = target;
        const uint32_t n_internal = m.n_internal();
        while (node < n_internal) {
            const uint16_t f = tree[node];
            if (cum < f) {
                tree[node] = static_cast<uint16_t>(f + inc);
                node = node * 2 + 1;
            } else {
                cum -= f;
                node = node * 2 + 2;
            }
        }

        const uint32_t consumed = target - cum;
        int32_t sym = static_cast<int32_t>(node) - static_cast<int32_t>(m.tree_off());
        if (sym < 0) sym += static_cast<int32_t>(m.n_sym());

        low += consumed * r;
        const uint16_t f = tree[node];
        total += inc;
        uint32_t rng = static_cast<uint32_t>(f) * r;
        tree[node] = static_cast<uint16_t>(f + inc);

        if (total > m.max_total())
            total = m.rescale();
        m.set_total(total);

        // [ORIGINAL] renormalisation loop at the tail of FUN_00408e70
        for (;;) {
            if (((rng + low) ^ low) > 0xFFFFFFu) {
                if (rng > 0xFFFFu) {
                    range_ = rng;
                    low_ = low;
                    code_ = code;
                    return static_cast<uint32_t>(sym);
                }
                rng = (0u - low) & 0xFFFFu;
            }
            code = (code << 8) | next_byte();
            rng <<= 8;
            low <<= 8;
        }
    }

private:
    uint32_t next_byte() {
        const uint32_t b = (p_ < size_) ? d_[p_] : 0u;
        ++p_;
        return b;
    }

    const uint8_t* d_;
    size_t size_;
    size_t p_ = 0;
    uint32_t low_ = 0;
    uint32_t code_ = 0;
    uint32_t range_ = 0xFFFFFFFFu;
};

}  // namespace

bool DzCoderParams::parse(const uint8_t* header, size_t size, DzCoderParams& out) {
    if (size < kHeaderSize) return false;
    out.win_size = header[0];
    out.flags = header[1];
    out.offset_table_size = header[2];
    out.offset_tables = header[3];
    out.offset_contexts = header[4];
    out.ref_length_table_size = header[5];
    out.ref_length_tables = header[6];
    out.ref_offset_table_size = header[7];
    out.ref_offset_tables = header[8];
    out.big_min_match = header[9];

    // [ORIGINAL] FUN_00409d90 rejects the archive unless all three hold.
    if (out.win_size >= 0x1F) return false;
    if (out.flags >= 4) return false;
    if (out.offset_contexts > 8) return false;
    if (out.offset_tables == 0 || out.offset_contexts == 0) return false;
    if (out.offset_table_size == 0 || out.offset_table_size > 16) return false;
    return true;
}

std::vector<uint8_t> dz_decode_block(const DzCoderParams& params,
                                     const uint8_t* in, size_t in_size,
                                     size_t out_size) {
    std::vector<uint8_t> out;
    if (in == nullptr || out_size == 0) return out;
    out.reserve(out_size);

    // [ORIGINAL] FUN_00409110 allocates the model set; FUN_00409300 resets it
    // for every entry. Main model: 0x202 symbols, increment 0x10, cap 0x10000.
    Model main;
    main.init(0x202, 0x10, 0x10000);

    // [ORIGINAL] offset models: [OffsetContexts][OffsetTables], each with
    // (1 << OffsetTableSize) symbols; increment starts at 0x20 and grows by 4
    // for each successive table (FUN_00409300, `iVar4 = 0x20; iVar4 += 4`).
    const uint32_t n_ctx = params.offset_contexts;
    const uint32_t n_tbl = params.offset_tables;
    const uint32_t n_off_sym = 1u << params.offset_table_size;
    std::vector<Model> off(static_cast<size_t>(n_ctx) * n_tbl);
    for (uint32_t c = 0; c < n_ctx; ++c)
        for (uint32_t k = 0; k < n_tbl; ++k)
            off[c * n_tbl + k].init(n_off_sym, 0x20 + 4 * k, 0x10000);

    RangeDecoder rc(in, in_size);
    int32_t reps[4] = {0, 0, 0, 0};
    const uint32_t cont_bit = 1u << (params.offset_table_size - 1);
    const uint32_t bits = params.offset_table_size;

    while (out.size() < out_size) {
        const uint32_t sym = rc.decode(main);
        if (sym < 0x100) {
            out.push_back(static_cast<uint8_t>(sym));
            continue;
        }
        if (sym == 0x201) break;  // [ORIGINAL] end-of-block symbol

        const uint32_t length = sym - 0xFEu;
        if (length >= 0x103u) {
            // [HEURISTIC-TODO] Cross-entry reference symbols (>= 0x202) only
            // occur when an entry carries flag bit 0 (a shared dictionary
            // block). No Shadow Fight 2 archive uses them; decoding them needs
            // FUN_00409ba0/FUN_00409970/FUN_00409ff0/FUN_0040a160.
            return {};
        }

        // [ORIGINAL] FUN_004097e0 — offset symbol, MSB of each chunk is the
        // "more chunks follow" flag.
        Model* row = &off[static_cast<size_t>(
            (length - 2 < n_ctx - 1) ? (length - 2) : (n_ctx - 1)) * n_tbl];

        uint32_t shift = 0;
        uint32_t raw = 0;
        uint32_t k = 0;
        uint32_t v = rc.decode(row[0]);
        while (v & cont_bit) {
            raw |= (v & ~cont_bit) << (shift & 0x1F);
            shift = (shift - 1 + bits) & 0xFF;
            if (k + 1 < n_tbl) ++k;
            v = rc.decode(row[k]);
        }
        raw |= v << (shift & 0x1F);

        int32_t dist;
        if (raw < 4) {
            const int32_t t = reps[0];
            reps[0] = reps[raw];
            reps[raw] = t;
            dist = reps[0];
        } else {
            reps[3] = reps[2];
            reps[2] = reps[1];
            reps[1] = reps[0];
            dist = static_cast<int32_t>(raw) - 3;
            reps[0] = dist;
        }

        if (dist <= 0 || static_cast<size_t>(dist) > out.size())
            return {};  // back-reference before the start of the block
        // [ORIGINAL] the copy always emits the full match length; the original
        // output buffer is (window + 0x102) bytes precisely so the last match
        // of a window may overshoot. Any overshoot is trimmed below.
        for (uint32_t i = 0; i < length; ++i)
            out.push_back(out[out.size() - static_cast<size_t>(dist)]);
    }

    if (out.size() < out_size) return {};
    out.resize(out_size);
    return out;
}

}  // namespace resf2::dz
