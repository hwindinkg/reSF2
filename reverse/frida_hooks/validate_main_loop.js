'use strict';
/*
 * validate_main_loop.js
 * Confirms the semantics of the real main loop at game+0x64400 before its
 * behaviour is committed to C++.
 *
 * Two constraints learned the hard way:
 *   - Never Interceptor.attach the game+0x6xxx import thunks: they are
 *     PC-relative (ADD IP, PC, #..) and the trampoline corrupts the GOT math.
 *     Hook the 16-byte PLT stubs at the region start instead.
 *   - The loop is already running by the time we attach, so `this` cannot be
 *     captured at function entry. At the yield call site r8 == this
 *     (`LDRD r4, r5, [r8, #8]` reads the frame interval from it).
 *
 * Logs to a file as well as stdout, because buffered stdout is lost if the
 * frida CLI is killed.
 */

var YIELD_PLT = 0x2010;   // s3eDeviceYield
var TIMER_PLT = 0x29F0;   // s3eTimerGetMs
var REG = null;

var LOG = null;
try { LOG = new File('/data/data/com.nekki.shadowfight/mainloop_validate.txt', 'w'); } catch (e) {}
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

function run() {
    REG = findGameRegion();
    if (!REG) { log('[FAIL] no region'); return; }
    var g = function (off) { return REG.start.add(off); };
    log('game base ' + REG.start);

    var dumped = false;
    var yieldSites = {};
    var yieldCalls = 0;
    var frames = 0;

    Interceptor.attach(g(YIELD_PLT), {
        onEnter: function (args) {
            yieldCalls++;
            var ctx = this.context;
            var site = ctx.lr.sub(REG.start).toString(16);
            var ms = args[0].toInt32();

            if (!yieldSites[site]) yieldSites[site] = { n: 0, min: ms, max: ms, sum: 0 };
            var s = yieldSites[site];
            s.n++; s.sum += ms;
            if (ms < s.min) s.min = ms;
            if (ms > s.max) s.max = ms;

            // r8 == this at the frame-limiter call site.
            if (!dumped && ctx.r8 && !ctx.r8.isNull()) {
                try {
                    var self = ctx.r8;
                    var vt = self.readPointer();
                    var lo = self.add(8).readU32();
                    var hi = self.add(12).readU32();
                    if (lo > 0 && lo < 1000) {
                        dumped = true;
                        log('\n=== loop object (r8 at yield site game+0x' + site + ') ===');
                        log('  this            = ' + self);
                        log('  this->vtable    = ' + vt);
                        log('  this+0x08 (lo)  = ' + lo + '   <- frame interval ms');
                        log('  this+0x0C (hi)  = ' + hi);
                        log('  => target fps   = ' + (1000 / lo).toFixed(2));
                        var d = '';
                        for (var i = 0; i < 14; i++) {
                            d += '    +0x' + (i * 4).toString(16) + ' = 0x' +
                                 self.add(i * 4).readU32().toString(16) + '\n';
                        }
                        log('  object dump:\n' + d);
                    }
                } catch (e) {}
            }
        }
    });

    Interceptor.attach(g(TIMER_PLT), {
        onEnter: function () {
            var lr = this.context.lr.sub(REG.start).toInt32();
            // top-of-loop timestamp read returns to game+0x64424
            if (lr === 0x64424) frames++;
        }
    });

    log('>> measuring 15s <<');
    var t = 0;
    var iv = setInterval(function () {
        t += 5;
        log('\n[' + t + 's] loop iterations=' + frames + ' (' + (frames / t).toFixed(1) +
            ' fps)  yield calls=' + yieldCalls);
        Object.keys(yieldSites).forEach(function (site) {
            var s = yieldSites[site];
            log('    yield from game+0x' + site + ' : n=' + s.n + '  ms min=' + s.min +
                ' max=' + s.max + ' avg=' + (s.sum / s.n).toFixed(2));
        });
        if (t >= 15) {
            clearInterval(iv);
            log('\n[DONE] measured fps = ' + (frames / t).toFixed(2));
        }
    }, 5000);
}

var tries = 0;
(function poll() {
    if (findGameRegion()) { run(); return; }
    if (++tries > 120) { log('[TIMEOUT]'); return; }
    setTimeout(poll, 250);
})();
