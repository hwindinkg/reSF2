'use strict';
/*
 * measure_frame_rate.js
 * Measures the real loop rate and the yield arguments of the main loop at
 * game+0x64400, reporting from inside the hook (setInterval/setTimeout do not
 * get scheduled reliably while a high-frequency hook is installed).
 *
 * Confirms the fixed-timestep model:
 *   interval = this+0x08 = 16 ms  -> ~62.5 fps cap
 *   yield(0)            at game+0x64434 : pump events once per frame
 *   yield(remaining_ms) at game+0x644F0 : sleep out the rest of the frame
 */

var YIELD_PLT = 0x2010;
var REPORT_EVERY = 200;

var LOG = null;
try { LOG = new File('/data/data/com.nekki.shadowfight/frame_rate.txt', 'w'); } catch (e) {}
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

    var t0 = Date.now();
    var sites = {};
    var total = 0;
    var reports = 0;

    Interceptor.attach(REG.start.add(YIELD_PLT), {
        onEnter: function (args) {
            total++;
            var site = this.context.lr.sub(REG.start).toString(16);
            var ms = args[0].toInt32();
            var s = sites[site];
            if (!s) { s = sites[site] = { n: 0, min: ms, max: ms, sum: 0 }; }
            s.n++; s.sum += ms;
            if (ms < s.min) s.min = ms;
            if (ms > s.max) s.max = ms;

            if (total % REPORT_EVERY === 0 && reports < 6) {
                reports++;
                var dt = (Date.now() - t0) / 1000;
                log('\n[' + dt.toFixed(1) + 's] total yield calls=' + total);
                Object.keys(sites).forEach(function (k) {
                    var v = sites[k];
                    log('   game+0x' + k + '  n=' + v.n +
                        '  ms min=' + v.min + ' max=' + v.max +
                        ' avg=' + (v.sum / v.n).toFixed(2) +
                        '  rate=' + (v.n / dt).toFixed(1) + '/s');
                });
                if (reports === 6) log('\n[DONE]');
            }
        }
    });
    log('>> hooked s3eDeviceYield PLT stub <<');
}
