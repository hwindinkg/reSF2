'use strict';
/*
 * profile_game_thread.js
 * PC-samples only the game-loop thread (the one whose PC lands in the game
 * region) and reports a histogram of hot game functions.
 *
 * This is deliberately sampling-based: the runtime PLT stubs cannot be hooked
 * (their literal pool sits at stub+8 and Interceptor overwrites it), and the
 * libs3e exports showed no traffic in the current game state, so sampling is
 * the reliable way to see what the loop is doing.
 *
 * Function starts are resolved by walking back to the nearest
 * PUSH {..., lr} prologue, matching find_callers.py.
 */

var SAMPLES = 40;
var SLEEP = 0.001;

var LOG = null;
try { LOG = new File('/data/data/com.nekki.shadowfight/profile.txt', 'w'); } catch (e) {}
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
if (!REG) { log('[FAIL] no region'); } else {

var END = REG.start.add(REG.size);
function inGame(a) {
    return a && !a.isNull() && a.compare(REG.start) >= 0 && a.compare(END) < 0;
}

// nearest preceding PUSH {..., lr}
function funcStart(addr, limit) {
    limit = limit || 0x800;
    for (var back = 0; back < limit; back += 4) {
        var p = addr.sub(back);
        if (p.compare(REG.start) < 0) break;
        var w;
        try { w = p.readU32(); } catch (e) { break; }
        if ((w & 0x0FFF0000) === 0x092D0000 && (w & 0x4000)) return p;
    }
    return null;
}

log('game region ' + REG.start + ' - ' + END);

// Find the game thread: the one seen executing game code.
var gameTid = null;
for (var probe = 0; probe < 200 && gameTid === null; probe++) {
    Process.enumerateThreads().forEach(function (t) {
        if (gameTid === null && inGame(t.context.pc)) gameTid = t.id;
    });
    if (gameTid === null) Thread.sleep(0.01);
}
log('game thread: ' + (gameTid !== null ? gameTid : 'not caught (loop may be idle)'));

var pcHist = {};      // function start -> count
var rawHist = {};     // exact pc -> count
var inGameCount = 0, outCount = 0;
var outHist = {};

for (var i = 0; i < SAMPLES; i++) {
    var threads = Process.enumerateThreads();
    for (var j = 0; j < threads.length; j++) {
        var t = threads[j];
        if (gameTid !== null && t.id !== gameTid) continue;
        var pc = t.context.pc;
        if (inGame(pc)) {
            inGameCount++;
            var raw = 'game+0x' + pc.sub(REG.start).toString(16);
            rawHist[raw] = (rawHist[raw] || 0) + 1;
            var fs = funcStart(pc);
            var key = fs ? 'game+0x' + fs.sub(REG.start).toString(16) : raw;
            pcHist[key] = (pcHist[key] || 0) + 1;
        } else {
            outCount++;
            var m = Process.findModuleByAddress(pc);
            var k = m ? m.name + '+0x' + pc.sub(m.base).toString(16) : pc.toString();
            outHist[k] = (outHist[k] || 0) + 1;
        }
    }
    Thread.sleep(SLEEP);
}

log('\nsamples in game code: ' + inGameCount + ' / outside: ' + outCount);

log('\n=== hot game functions (by prologue) ===');
Object.keys(pcHist).sort(function (a, b) { return pcHist[b] - pcHist[a]; })
    .slice(0, 25).forEach(function (k) {
        log('  ' + k + '  x' + pcHist[k] +
            '  (' + (100 * pcHist[k] / Math.max(inGameCount, 1)).toFixed(1) + '%)');
    });

log('\n=== hot exact PCs ===');
Object.keys(rawHist).sort(function (a, b) { return rawHist[b] - rawHist[a]; })
    .slice(0, 15).forEach(function (k) { log('  ' + k + '  x' + rawHist[k]); });

log('\n=== where it waits outside game code ===');
Object.keys(outHist).sort(function (a, b) { return outHist[b] - outHist[a]; })
    .slice(0, 10).forEach(function (k) { log('  ' + k + '  x' + outHist[k]); });

log('\n[DONE]');
}
