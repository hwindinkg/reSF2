'use strict';
/*
 * record_golden_trace.js
 *
 * Records a "golden trace" from the ORIGINAL game: the exact per-frame timing
 * contract, so the reSF2 engine can be tested against real behaviour instead
 * of assumptions.
 *
 * What it captures, per frame of the real main loop (game+0x64400):
 *   - frame index
 *   - s3eTimerGetMs at the top of the frame (t0)
 *   - the frame interval field this+0x08 (int64 ms)
 *   - the argument passed to s3eDeviceYield at each of the two call sites
 *   - how many times the inner wait loop spun for that frame
 *   - the elapsed ms the loop measured
 *
 * The output is consumed by tests/golden/ in the C++ test suite; see
 * reverse/analysis/GOLDEN_TESTS.md for the contract each field pins down.
 *
 * Implementation constraints (learned the hard way, see RUNTIME_MAP.md §4):
 *   - never hook the game+0x6xxx PC-relative import thunks
 *   - never hook the 16-byte PLT stubs (literal pool at +8 gets clobbered)
 *   => hook the libs3e_android.so exports; LR there is still game code
 *      because the stubs are tail jumps.
 *
 * Output: /data/data/com.nekki.shadowfight/golden_trace.jsonl
 */

// Return addresses inside the loop (LR values), from RUNTIME_MAP.md §5.
var LR_TIMER_FRAME_TOP = 0x64424;   // after BL s3eTimerGetMs at frame top
var LR_TIMER_WAIT      = 0x644C4;   // after BL s3eTimerGetMs in wait loop
var LR_YIELD_PUMP      = 0x64434;   // after BL s3eDeviceYield(0)
var LR_YIELD_SLEEP     = 0x644F0;   // after BL s3eDeviceYield(remaining)

var MAX_FRAMES = 600;

var LOG = null;
try { LOG = new File('/data/data/com.nekki.shadowfight/golden_trace.jsonl', 'w'); } catch (e) {}
function out(m) {
    if (LOG) { try { LOG.write(m + '\n'); LOG.flush(); } catch (e) {} }
}
function log(m) { console.log(m); out('# ' + m); }

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
    log('game base ' + REG.start);
    out('# golden trace v1; base=' + REG.start);

    var END = REG.start.add(REG.size);
    function lrOff(lr) {
        if (lr.compare(REG.start) >= 0 && lr.compare(END) < 0) {
            return lr.sub(REG.start).toInt32();
        }
        return -1;
    }

    // Call s3eTimerGetMs ourselves instead of hooking it -- one hook total.
    var timerAddr = Module.findExportByName('libs3e_android.so', 's3eTimerGetMs');
    var getMs = timerAddr ? new NativeFunction(timerAddr, 'uint32', []) : null;
    log('s3eTimerGetMs @ ' + timerAddr);

    var YIELD_PLT = 0x2010;          // PLT #513
    var LR_YIELD_PUMP  = 0x64434;    // return addr of yield(0)
    var LR_YIELD_SLEEP = 0x644F0;    // return addr of yield(remaining)

    var frame = 0;
    var cur = null;
    var spins = 0;
    var done = false;

    function flush() {
        if (!cur) return;
        cur.spins = spins;
        out(JSON.stringify(cur));
        cur = null;
        spins = 0;
    }

    Interceptor.attach(REG.start.add(YIELD_PLT), {
        onEnter: function (args) {
            if (done) return;
            var off = lrOff(this.context.lr);
            var ms = args[0].toInt32();
            var now = getMs ? getMs() : 0;

            if (off === LR_YIELD_PUMP) {
                // yield(0) marks the top of a new frame
                flush();
                frame++;
                if (frame > MAX_FRAMES) {
                    done = true;
                    log('[DONE] ' + MAX_FRAMES + ' frames recorded');
                    return;
                }
                cur = { frame: frame, t0: now, yield_pump: ms, yields: [] };
                try {
                    var self = this.context.r8;      // r8 == this at both sites
                    if (self && !self.isNull()) {
                        var lo = self.add(8).readU32();
                        var hi = self.add(12).readU32();
                        if (lo > 0 && lo < 10000 && hi === 0) cur.interval_ms = lo;
                    }
                } catch (e) {}
            } else if (cur) {
                // wait-loop yields; each one is a spin of the inner loop
                spins++;
                cur.yields.push(ms);
                cur.elapsed = now - cur.t0;
                if (off !== LR_YIELD_SLEEP) {
                    cur.unexpected_site = '0x' + off.toString(16);
                }
            }
        }
    });

    log('hooked yield stub @ ' + REG.start.add(YIELD_PLT));
    log('>> recording up to ' + MAX_FRAMES + ' frames <<');
}
