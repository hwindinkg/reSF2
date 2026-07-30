'use strict';
/*
 * sample_game_thread.js
 * Identifies the thread that actually runs the game loop and samples its PC.
 *
 * Finding: the S3E main loop does NOT run on the process main thread. On this
 * device the busy thread is "Thread-2" (557 CPU ticks / 4s vs 17 on main), so
 * hooks that appeared to record "zero calls" were simply never reached by the
 * code path being observed.
 *
 * This lists every thread with its PC resolved against the game region, then
 * hooks s3eDeviceYield filtered to the game thread only.
 */

var LOG = null;
try { LOG = new File('/data/data/com.nekki.shadowfight/thread_sample.txt', 'w'); } catch (e) {}
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
log('game region: ' + (REG ? REG.start + ' size 0x' + REG.size.toString(16) : 'NOT FOUND'));

function lbl(a) {
    if (!a || a.isNull()) return 'null';
    if (REG && a.compare(REG.start) >= 0 && a.compare(REG.start.add(REG.size)) < 0) {
        return 'game+0x' + a.sub(REG.start).toString(16);
    }
    var m = Process.findModuleByAddress(a);
    return m ? m.name + '+0x' + a.sub(m.base).toString(16) : a.toString();
}

// ---- 1. thread inventory with PC samples ----
log('\n=== threads (PC sampled ' + 5 + 'x) ===');
var hits = {};          // tid -> { name, pcs: {label: count} }
for (var round = 0; round < 5; round++) {
    Process.enumerateThreads().forEach(function (t) {
        if (!hits[t.id]) hits[t.id] = { pcs: {} };
        var l = lbl(t.context.pc);
        hits[t.id].pcs[l] = (hits[t.id].pcs[l] || 0) + 1;
    });
    Thread.sleep(0.2);
}
Object.keys(hits).forEach(function (tid) {
    var pcs = hits[tid].pcs;
    var keys = Object.keys(pcs).sort(function (a, b) { return pcs[b] - pcs[a]; });
    var gameHits = keys.filter(function (k) { return k.indexOf('game+') === 0; });
    var tag = gameHits.length ? '   <== IN GAME CODE' : '';
    log('  tid ' + tid + tag);
    keys.slice(0, 4).forEach(function (k) { log('      ' + k + ' x' + pcs[k]); });
});

// ---- 2. yield hook, reporting which thread it came from ----
var y = Module.findExportByName('libs3e_android.so', 's3eDeviceYield');
var perThread = {};
var total = 0;
var reports = 0;
var t0 = Date.now();

if (y) {
    Interceptor.attach(y, {
        onEnter: function (a) {
            total++;
            var tid = this.threadId;
            if (!perThread[tid]) perThread[tid] = { n: 0, sites: {} };
            var p = perThread[tid];
            p.n++;
            var s = lbl(this.context.lr);
            p.sites[s] = (p.sites[s] || 0) + 1;

            if (total % 100 === 0 && reports < 5) {
                reports++;
                var dt = (Date.now() - t0) / 1000;
                log('\n[' + dt.toFixed(1) + 's] s3eDeviceYield total=' + total +
                    ' (' + (total / dt).toFixed(1) + '/s)');
                Object.keys(perThread).forEach(function (tid2) {
                    var q = perThread[tid2];
                    log('   tid ' + tid2 + ' : ' + q.n + ' calls');
                    Object.keys(q.sites).sort(function (x, z) { return q.sites[z] - q.sites[x]; })
                        .slice(0, 4).forEach(function (k) {
                            log('       from ' + k + ' x' + q.sites[k]);
                        });
                });
                if (reports === 5) log('\n[DONE]');
            }
        }
    });
    log('\nhooked s3eDeviceYield @ ' + y + ' -- waiting for calls');
} else {
    log('[!] s3eDeviceYield export not found');
}
