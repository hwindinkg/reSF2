'use strict';
/*
 * capture_trace_sink.js
 *
 * Captures the ORIGINAL game's own debug log by hooking its single trace sink
 * at game+0x6A7EF8.
 *
 * How the sink was found: the damage tracer's format strings are built
 * PC-relatively (`LDR r0,[pc,#x]` + `ADD r0,pc,r0`), and the instruction right
 * after every such pair is `BL 0x8F6FEEF8`. So one hook there yields every
 * formatted line -- damage, tactics, animations -- without needing to know any
 * object layout.
 *
 * This is the highest-value capture for a 1:1 port, because
 * internalSettings.xml already enables the tracer:
 *   <Log Value="1"><Hits><Damage Value="1"/><Style Value="1"/></Hits>
 *                  <Tactics Value="1"/><Animations Value="1"/></Log>
 * and the game prints every intermediate value of the damage and AI formulas.
 *
 * The first argument is a printf-style format string. We record the format
 * verbatim plus the raw integer/float register+stack arguments, so the values
 * can be reassembled offline (implementing full printf here would be fragile).
 *
 * Output: /data/data/com.nekki.shadowfight/trace_sink.jsonl
 */

var SINK = 0x6A7EF8;
var MAX_LINES = 4000;

var LOG = null;
try { LOG = new File('/data/data/com.nekki.shadowfight/trace_sink.jsonl', 'w'); } catch (e) {}
function out(m) { if (LOG) { try { LOG.write(m + '\n'); LOG.flush(); } catch (e) {} } }
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
    out('# trace sink capture v1; base=' + REG.start);

    var lines = 0;
    var seenFormats = {};

    Interceptor.attach(REG.start.add(SINK), {
        onEnter: function (args) {
            if (lines >= MAX_LINES) return;

            var fmt = null;
            try { fmt = args[0].readUtf8String(); } catch (e) {}
            if (!fmt) {
                try { fmt = args[0].readCString(); } catch (e) {}
            }
            if (!fmt || fmt.length === 0) return;

            lines++;
            seenFormats[fmt] = (seenFormats[fmt] || 0) + 1;

            var rec = { n: lines, fmt: fmt };

            // Only bother collecting varargs when the format takes any.
            if (fmt.indexOf('%') >= 0) {
                var ctx = this.context;
                rec.regs = {
                    r1: ctx.r1.toString(),
                    r2: ctx.r2.toString(),
                    r3: ctx.r3.toString()
                };
                // Integer-ish and float-ish views of r1..r3.
                rec.ints = [ctx.r1.toInt32(), ctx.r2.toInt32(), ctx.r3.toInt32()];

                // Variadic doubles land on the stack (and in VFP for some ABIs);
                // grab a window of the stack as both ints and doubles.
                var sp = ctx.sp;
                var iw = [], dw = [];
                for (var i = 0; i < 12; i++) {
                    try { iw.push(sp.add(i * 4).readS32()); } catch (e) { break; }
                }
                for (var j = 0; j < 6; j++) {
                    try {
                        var v = sp.add(j * 8).readDouble();
                        dw.push((isFinite(v) && Math.abs(v) < 1e12) ? Number(v.toFixed(6)) : null);
                    } catch (e) { break; }
                }
                rec.stack_i32 = iw;
                rec.stack_f64 = dw;

                // Any pointer arg that resolves to a string is usually a name
                // (animation, attribute, style), which is what %s prints.
                var strs = [];
                [ctx.r1, ctx.r2, ctx.r3].forEach(function (p) {
                    try {
                        var s = p.readUtf8String();
                        if (s && s.length > 0 && s.length < 64 && /^[\x20-\x7E]+$/.test(s)) {
                            strs.push(s);
                        }
                    } catch (e) {}
                });
                if (strs.length) rec.strings = strs;
            }

            out(JSON.stringify(rec));
            if (lines === MAX_LINES) {
                log('[DONE] ' + MAX_LINES + ' lines captured');
                out('# distinct formats: ' + Object.keys(seenFormats).length);
            }
        }
    });

    log('hooked trace sink @ ' + REG.start.add(SINK));
    log('>> play: enter a fight, land hits, let the AI act <<');
}
