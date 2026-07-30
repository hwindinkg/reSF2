'use strict';
/*
 * count_via_exports.js
 * Counts S3E API calls made by the game by hooking the *libs3e_android.so
 * exports* rather than the game's PLT stubs.
 *
 * Why: the game's runtime PLT stubs are 8 bytes of code followed immediately
 * by their literal pool (target pointer at +8). Interceptor.attach writes a
 * trampoline over that pool and silently breaks the stub, so hook counts stay
 * at zero. The exports are ordinary functions and safe to hook.
 *
 * Because the stubs are tail jumps (LDR PC, [IP]) they consume no stack frame,
 * so LR inside the export is still the *game code* return address -- exactly
 * the caller information we want.
 */

var APIS = [
    's3eDeviceYield',
    's3eDeviceYieldUntilEvent',
    's3eTimerGetMs',
    's3eDeviceCheckQuitRequest',
    's3eKeyboardUpdate',
    's3ePointerUpdate',
    's3eSurfaceShow'
];

var LOG = null;
try { LOG = new File('/data/data/com.nekki.shadowfight/api_counts.txt', 'w'); } catch (e) {}
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
log('game region: ' + (REG ? REG.start : 'NOT FOUND'));

function inGame(a) {
    return REG && a && !a.isNull() &&
           a.compare(REG.start) >= 0 && a.compare(REG.start.add(REG.size)) < 0;
}

var counts = {};
var callers = {};
var args0 = {};
var t0 = Date.now();
var reports = 0;
var driver = null;

function report() {
    var dt = (Date.now() - t0) / 1000;
    log('\n[' + dt.toFixed(1) + 's] ---- API call rates ----');
    APIS.forEach(function (n) {
        var c = counts[n] || 0;
        if (!c) { log('   ' + n + ' : 0'); return; }
        var line = '   ' + n + ' : ' + c + '  (' + (c / dt).toFixed(1) + '/s)';
        var a = args0[n];
        if (a && a.n) {
            line += '  arg0 min=' + a.min + ' max=' + a.max +
                    ' avg=' + (a.sum / a.n).toFixed(2);
        }
        log(line);
        var cs = callers[n] || {};
        Object.keys(cs).sort(function (x, y) { return cs[y] - cs[x]; })
            .slice(0, 4).forEach(function (s) {
                log('        from ' + s + ' x' + cs[s]);
            });
    });
}

APIS.forEach(function (name) {
    var addr = Module.findExportByName('libs3e_android.so', name);
    if (!addr) { log('[!] no export ' + name); return; }
    counts[name] = 0;
    callers[name] = {};
    args0[name] = { n: 0, min: 1e9, max: -1e9, sum: 0 };

    Interceptor.attach(addr, {
        onEnter: function (a) {
            counts[name]++;
            var lr = this.context.lr;
            var key = inGame(lr) ? ('game+0x' + lr.sub(REG.start).toString(16))
                                 : (function () {
                                        var m = Process.findModuleByAddress(lr);
                                        return m ? m.name + '+0x' + lr.sub(m.base).toString(16)
                                                 : lr.toString();
                                    })();
            callers[name][key] = (callers[name][key] || 0) + 1;

            var v = a[0].toInt32();
            var s = args0[name];
            s.n++; s.sum += v;
            if (v < s.min) s.min = v;
            if (v > s.max) s.max = v;

            if (driver === null && counts[name] >= 30) driver = name;
            if (name === driver && counts[name] % 60 === 0 && reports < 5) {
                reports++;
                report();
                if (reports === 5) log('\n[DONE]');
            }
        }
    });
    log('hooked ' + name + ' @ ' + addr);
});
