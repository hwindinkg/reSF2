'use strict';
/*
 * trace_yield_caller.js
 * Finds the real main loop by hooking the game's *own* PLT stub for
 * s3eDeviceYield (inside the rwxs /dev/zero game region), not the libs3e
 * export. LR at that point is genuine game code, which gives the caller
 * chain directly.
 *
 * Earlier attempts hooked libs3e_android.so!s3eDeviceYield, where LR points
 * back into the stub -- one frame short of the useful information.
 *
 * Region is found via /proc/self/maps (Frida's enumerateRanges misses this
 * shared mapping).
 */

var YIELD_PLT_OFFSET = 0x2010;   // PLT#513, from resolve_plt.js
var REG = null;

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
            if (!best || size > best.size) {
                best = { start: ptr('0x' + m[1]), end: ptr('0x' + m[2]), size: size };
            }
        }
        f.close();
    } catch (e) { console.log('[!] ' + e); }
    return best;
}

function inGame(addr) {
    return REG && addr && !addr.isNull() &&
           addr.compare(REG.start) >= 0 && addr.compare(REG.end) < 0;
}

function lbl(addr) {
    if (!addr || addr.isNull()) return 'null';
    if (inGame(addr)) return 'game+0x' + addr.sub(REG.start).toString(16);
    var m = Process.findModuleByAddress(addr);
    if (m) return m.name + '+0x' + addr.sub(m.base).toString(16);
    return addr.toString();
}

function run() {
    REG = findGameRegion();
    if (!REG) { console.log('[FAIL] no game region'); return; }
    console.log('game region ' + REG.start + '-' + REG.end);

    var stub = REG.start.add(YIELD_PLT_OFFSET);
    console.log('yield PLT stub at ' + stub + ' (game+0x' + YIELD_PLT_OFFSET.toString(16) + ')');
    try {
        console.log('  stub words: ' + stub.readU32().toString(16) + ' ' +
                    stub.add(4).readU32().toString(16) + ' -> ' +
                    stub.add(8).readPointer());
    } catch (e) { console.log('  unreadable: ' + e); }

    var seen = {};       // caller -> count
    var calls = 0;
    var reported = 0;

    Interceptor.attach(stub, {
        onEnter: function () {
            calls++;
            var lr = this.context.lr;
            var key = lr.toString();
            if (!seen[key]) {
                seen[key] = 0;
                // New call site: dump the game-code frames from the stack.
                if (reported < 12) {
                    reported++;
                    console.log('\n=== new yield call site #' + reported +
                                ' (call ' + calls + ') ===');
                    console.log('  LR = ' + lbl(lr));
                    var sp = this.context.sp;
                    var frames = [];
                    for (var i = 0; i < 128 && frames.length < 14; i++) {
                        try {
                            var v = sp.add(i * 4).readPointer();
                            if (inGame(v)) frames.push('SP+0x' + (i * 4).toString(16) + ' ' + lbl(v));
                        } catch (e) { break; }
                    }
                    frames.forEach(function (f) { console.log('  ' + f); });
                }
            }
            seen[key]++;
        }
    });

    console.log('\n>> hooked. yield is called from the battle loop; ' +
                'if counts stay at 0, enter a fight. <<');

    var ticks = 0;
    var timer = setInterval(function () {
        ticks++;
        var keys = Object.keys(seen);
        console.log('[' + ticks * 3 + 's] yield calls=' + calls + ' distinct sites=' + keys.length);
        keys.sort(function (a, b) { return seen[b] - seen[a]; });
        keys.slice(0, 6).forEach(function (k) {
            console.log('    ' + lbl(ptr(k)) + '  x' + seen[k]);
        });
        if (ticks >= 20) { clearInterval(timer); console.log('[DONE]'); }
    }, 3000);
}

var tries = 0;
(function poll() {
    if (findGameRegion()) { run(); return; }
    if (++tries > 120) { console.log('[TIMEOUT]'); return; }
    setTimeout(poll, 250);
})();
