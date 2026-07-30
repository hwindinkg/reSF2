'use strict';
/*
 * count_loop_stubs.js
 * Counts hits on each S3E stub that the loop at game+0x64400 calls, to
 * determine which one actually ticks once per rendered frame.
 *
 * Motivation: hooking s3eDeviceYield showed only ~2 calls while the game was
 * visibly rendering, so yield is NOT the per-frame driver in this state (the
 * old notes claimed it only runs in battle). Counting every loop stub settles
 * which call is the real frame boundary.
 *
 * Reporting happens inside a hook: setInterval is not scheduled reliably while
 * high-frequency interceptors are installed.
 */

var STUBS = {
    's3eDeviceYield':            0x2010,
    's3eDeviceYieldUntilEvent':  0x2020,
    's3eTimerGetMs':             0x29F0,
    's3eDeviceCheckQuitRequest': 0x1F10,
    's3eKeyboardUpdate':         0x2470,
    's3ePointerUpdate':          0x2630
};

var LOG = null;
try { LOG = new File('/data/data/com.nekki.shadowfight/loop_stubs.txt', 'w'); } catch (e) {}
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
    log('[FAIL] region not found');
} else {
    log('game base ' + REG.start);
    var counts = {};
    var callers = {};
    var t0 = Date.now();
    var reports = 0;
    var driver = null;

    Object.keys(STUBS).forEach(function (name) {
        counts[name] = 0;
        callers[name] = {};
        var addr = REG.start.add(STUBS[name]);
        try {
            Interceptor.attach(addr, {
                onEnter: function () {
                    counts[name]++;
                    var site = this.context.lr.sub(REG.start).toString(16);
                    callers[name][site] = (callers[name][site] || 0) + 1;

                    // Report from whichever stub ticks fastest.
                    if (driver === null && counts[name] >= 60) driver = name;
                    if (name !== driver) return;
                    if (counts[name] % 120 !== 0) return;
                    if (reports >= 5) return;
                    reports++;
                    var dt = (Date.now() - t0) / 1000;
                    log('\n[' + dt.toFixed(1) + 's] ---- rates ----');
                    Object.keys(STUBS).forEach(function (n) {
                        if (!counts[n]) { log('   ' + n + ' : 0'); return; }
                        log('   ' + n + ' : ' + counts[n] +
                            '  (' + (counts[n] / dt).toFixed(1) + '/s)');
                        Object.keys(callers[n]).sort(function (a, b) {
                            return callers[n][b] - callers[n][a];
                        }).slice(0, 3).forEach(function (s) {
                            log('        from game+0x' + s + ' x' + callers[n][s]);
                        });
                    });
                    if (reports === 5) log('\n[DONE]');
                }
            });
            log('hooked ' + name + ' @ ' + addr);
        } catch (e) {
            log('[!] cannot hook ' + name + ': ' + e);
        }
    });
}
