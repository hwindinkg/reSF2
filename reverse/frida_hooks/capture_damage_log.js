'use strict';
/*
 * capture_damage_log.js
 *
 * The original has a built-in damage tracer. internalSettings.xml already
 * enables it (<Log><Hits><Damage Value="1"/>), and its format strings spell
 * out the exact evaluation order of the damage formula:
 *
 *   Hit / Attack / Attack Interval ID / BaseDamage / Critical /
 *   BlockDamageFactor / Block / DamageFactor / DamageAttribute /
 *   DefenseAttribute / TargetAttributeDifference / Delta.Factor /
 *   AttributeDifference / HitDamage / StyleValueAdd / StyleValue / Style
 *
 * That is ground truth for the reSF2 damage model, which currently hardcodes
 * attribute_multiplier = 1.0 (see the [HEURISTIC-TODO] markers in
 * engine/game/game.cpp around lines 1840 and 3641).
 *
 * Rather than reimplement the formatting, we capture the game's own log:
 *   - hook the logger sink so every formatted line is dumped verbatim
 *   - hook the damage function at game+0x438530 for arguments/return
 *
 * Output: /data/data/com.nekki.shadowfight/damage_log.txt
 */

var DAMAGE_FUNC = 0x438530;

var LOG = null;
try { LOG = new File('/data/data/com.nekki.shadowfight/damage_log.txt', 'w'); } catch (e) {}
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
    log('game base ' + REG.start);

    // ---- 1. capture the game's own formatted log lines ----
    // S3E routes debug output through s3eDebugTraceLine / IwTrace; also catch
    // libc's __android_log_print and vsnprintf-based paths.
    var sinks = [
        ['libs3e_android.so', 's3eDebugTraceLine'],
        ['libs3e_android.so', 's3eDebugTracePrintf'],
        ['liblog.so', '__android_log_print'],
        ['liblog.so', '__android_log_write']
    ];
    var hookedSink = false;
    sinks.forEach(function (pair) {
        var addr = null;
        try { addr = Module.findExportByName(pair[0], pair[1]); } catch (e) {}
        if (!addr) return;
        try {
            Interceptor.attach(addr, {
                onEnter: function (args) {
                    // Scan the first few pointer args for a readable string.
                    for (var i = 0; i < 4; i++) {
                        try {
                            var s = args[i].readUtf8String();
                            if (s && s.length > 2 && /[A-Za-z]/.test(s)) {
                                log('[' + pair[1] + '] ' + s);
                                break;
                            }
                        } catch (e) {}
                    }
                }
            });
            hookedSink = true;
            log('hooked sink ' + pair[0] + '!' + pair[1]);
        } catch (e) {}
    });
    if (!hookedSink) log('[!] no log sink hooked -- lines may not be captured');

    // ---- 2. hook the damage routine itself ----
    var fn = REG.start.add(DAMAGE_FUNC);
    var calls = 0;
    try {
        Interceptor.attach(fn, {
            onEnter: function (args) {
                calls++;
                this.a0 = args[0];
                this.a1 = args[1];
                if (calls <= 20) {
                    log('\n=== damage call #' + calls + ' (game+0x' +
                        DAMAGE_FUNC.toString(16) + ') ===');
                    log('  r0=' + args[0] + ' r1=' + args[1] +
                        ' r2=' + args[2] + ' r3=' + args[3]);
                    // The hit/model objects carry the float state we need.
                    for (var base = 0; base < 2; base++) {
                        var p = base === 0 ? args[0] : args[1];
                        if (!p || p.isNull()) continue;
                        var line = '  [' + (base === 0 ? 'r0' : 'r1') + '] ';
                        for (var i = 0; i < 16; i++) {
                            try {
                                var u = p.add(i * 4).readU32();
                                var f = p.add(i * 4).readFloat();
                                var fs = (isFinite(f) && Math.abs(f) > 1e-6 && Math.abs(f) < 1e6)
                                    ? f.toFixed(4) : '-';
                                line += '+' + (i * 4).toString(16) + '=0x' +
                                        u.toString(16) + '(' + fs + ') ';
                            } catch (e) { break; }
                        }
                        log(line);
                    }
                }
            },
            onLeave: function (ret) {
                if (calls <= 20) {
                    var f = 0;
                    try { f = ret.toInt32(); } catch (e) {}
                    log('  -> ret=' + ret + ' (int ' + f + ')');
                }
            }
        });
        log('hooked damage func @ ' + fn);
    } catch (e) {
        log('[!] cannot hook damage func: ' + e);
    }

    log('\n>> enter a fight and land some hits <<');
}
