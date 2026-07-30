'use strict';
/*
 * trace_damage_func.js
 *
 * Traces the damage routine at game+0x438530 -- located by finding code that
 * builds the addresses of the "BaseDamage: %.4f" / "HitDamage: %.3f (%.3f)"
 * format strings (see analysis/find_string_xrefs.py; string addresses here are
 * formed PC-relatively, so a plain literal search finds nothing).
 *
 * Goal: capture the real inputs/outputs of the original damage formula so the
 * reSF2 model can be made 1:1. reSF2 currently hardcodes
 * attribute_multiplier = 1.0 and factor_set_multiplier = 1.0
 * (engine/game/game.cpp ~1840, ~3641).
 *
 * Captured per call: integer + float views of the argument objects and the
 * VFP registers, which is where the float math lands on this ABI.
 *
 * Output: /data/data/com.nekki.shadowfight/damage_trace.txt (JSONL)
 */

var DAMAGE_FUNC = 0x438530;
var MAX_CALLS = 40;

var LOG = null;
try { LOG = new File('/data/data/com.nekki.shadowfight/damage_trace.txt', 'w'); } catch (e) {}
function log(m) {
    console.log(m);
    if (LOG) { try { LOG.write(m + '\n'); LOG.flush(); } catch (e) {} }
}

function findGameRegion() {
    var best = null;
    try {
        var f = new File('/proc/self/maps', 'r');
        var l;
        while ((l = f.readLine()) !== null && l.length > 0) {
            var m = /^([0-9a-f]+)-([0-9a-f]+)\s+(\S{4})\s+([0-9a-f]+)\s+\S+\s+\S+\s*(.*)$/.exec(l.trim());
            if (!m) continue;
            var size = parseInt(m[2], 16) - parseInt(m[1], 16);
            if ((m[5] || '').indexOf('/dev/zero') < 0) continue;
            if (m[3].indexOf('x') < 0) continue;
            if (size < 4 * 1024 * 1024) continue;
            if (!best || size > best.size) best = { start: ptr('0x' + m[1]), size: size };
        }
        f.close();
    } catch (e) { log('[!] ' + e); }
    return best;
}

var REG = findGameRegion();
if (!REG) {
    log('[FAIL] game region not found');
} else {
    log('# game base ' + REG.start);

    function floats(p, count) {
        var out = [];
        for (var i = 0; i < count; i++) {
            try {
                var f = p.add(i * 4).readFloat();
                out.push((isFinite(f) && Math.abs(f) < 1e9) ? Number(f.toFixed(5)) : null);
            } catch (e) { out.push(null); break; }
        }
        return out;
    }
    function words(p, count) {
        var out = [];
        for (var i = 0; i < count; i++) {
            try { out.push('0x' + p.add(i * 4).readU32().toString(16)); }
            catch (e) { break; }
        }
        return out;
    }

    var calls = 0;
    Interceptor.attach(REG.start.add(DAMAGE_FUNC), {
        onEnter: function (args) {
            calls++;
            if (calls > MAX_CALLS) return;
            var rec = {
                call: calls,
                lr: 'game+0x' + this.context.lr.sub(REG.start).toString(16),
                r0: args[0].toString(),
                r1: args[1].toString(),
                r2: args[2].toString(),
                r3: args[3].toString()
            };
            if (!args[0].isNull()) {
                rec.r0_words = words(args[0], 20);
                rec.r0_floats = floats(args[0], 20);
            }
            if (!args[1].isNull()) {
                rec.r1_words = words(args[1], 20);
                rec.r1_floats = floats(args[1], 20);
            }
            // VFP scratch registers hold the intermediate float terms.
            var ctx = this.context;
            var vfp = {};
            ['s0', 's1', 's2', 's3', 'd0', 'd1', 'd2', 'd3'].forEach(function (r) {
                if (ctx[r] !== undefined) vfp[r] = String(ctx[r]);
            });
            if (Object.keys(vfp).length) rec.vfp = vfp;
            this.rec = rec;
        },
        onLeave: function (ret) {
            if (calls > MAX_CALLS || !this.rec) return;
            this.rec.ret = ret.toString();
            try { this.rec.ret_int = ret.toInt32(); } catch (e) {}
            log(JSON.stringify(this.rec));
            if (calls === MAX_CALLS) log('# [DONE] ' + MAX_CALLS + ' calls captured');
        }
    });
    log('# hooked damage func @ ' + REG.start.add(DAMAGE_FUNC));
    log('# >> enter a fight and land hits <<');
}
