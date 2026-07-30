'use strict';
/*
 * resolve_plt.js
 * Resolves every runtime PLT stub in the S3E game-code region to a real
 * exported symbol name, and writes a JSON map for Ghidra.
 *
 * Runtime PLT layout (16 bytes per entry, at region code start):
 *   +0x0  LDR R12, [PC, #0]   E59FC000
 *   +0x4  LDR PC,  [R12]      E59FF000
 *   +0x8  <pointer to real function in an extension .so>
 *   +0xC  <common s3e fixup pointer>
 *
 * Output: /data/data/com.nekki.shadowfight/plt_map.json
 */

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
                best = { start: ptr('0x' + m[1]), size: size,
                         fileOff: parseInt(m[4], 16) };
            }
        }
        f.close();
    } catch (e) { console.log('[!] ' + e); }
    return best;
}

// Build address -> name index from every loaded module's exports.
function buildSymbolIndex() {
    var index = {};
    var mods = Process.enumerateModules();
    for (var i = 0; i < mods.length; i++) {
        var m = mods[i];
        // Only the s3e core + extension libs matter; skip the huge system ones.
        if (!/s3e|nekki|smartfox|gamepad|Input|Amazon|swscale|avutil/i.test(m.name)) continue;
        var exps;
        try { exps = m.enumerateExports(); } catch (e) { continue; }
        for (var j = 0; j < exps.length; j++) {
            index[exps[j].address.toString()] = { name: exps[j].name, module: m.name };
        }
        var syms;
        try { syms = m.enumerateSymbols(); } catch (e) { syms = []; }
        for (var k = 0; k < syms.length; k++) {
            var s = syms[k];
            if (!s.address || s.address.isNull()) continue;
            var key = s.address.toString();
            if (!index[key] && s.name) index[key] = { name: s.name, module: m.name };
        }
    }
    return index;
}

function label(addr, index) {
    if (!addr || addr.isNull()) return null;
    var hit = index[addr.toString()];
    if (hit) return hit;
    var m = Process.findModuleByAddress(addr);
    if (m) {
        // Nearest preceding export within the same module.
        return { name: null, module: m.name, offset: addr.sub(m.base).toString() };
    }
    return null;
}

function run() {
    var r = findGameRegion();
    if (!r) { console.log('[FAIL] region not found'); return; }
    console.log('region ' + r.start + ' size 0x' + r.size.toString(16));

    var index = buildSymbolIndex();
    console.log('symbol index: ' + Object.keys(index).length + ' addresses');

    var entries = [];
    var named = 0, unnamed = 0;
    var p = r.start;
    for (var i = 0; ; i++) {
        var stub = p.add(i * 16);
        var w0, w1, target;
        try {
            w0 = stub.readU32();
            w1 = stub.add(4).readU32();
            target = stub.add(8).readPointer();
        } catch (e) { break; }
        if (w0 !== 0xE59FC000 || w1 !== 0xE59FF000) break;   // end of table

        var info = label(target, index);
        var rec = {
            index: i,
            stub: stub.toString(),
            stubOffset: '0x' + (i * 16).toString(16),
            target: target.toString(),
            name: info && info.name ? info.name : null,
            module: info ? info.module : null
        };
        if (info && info.offset) rec.moduleOffset = info.offset;
        if (rec.name) named++; else unnamed++;
        entries.push(rec);
    }

    console.log('PLT entries: ' + entries.length + '  named=' + named + '  unnamed=' + unnamed);
    console.log('table spans ' + r.start + ' - ' + r.start.add(entries.length * 16));

    console.log('\n=== first 40 resolved ===');
    for (var q = 0; q < Math.min(40, entries.length); q++) {
        var e = entries[q];
        console.log('  PLT#' + e.index + '  ' + e.stub + ' -> ' +
            (e.name || ('(' + e.module + '+' + (e.moduleOffset || '?') + ')')));
    }

    // Everything the notes care about: which PLT index is s3eDeviceYield/Register
    console.log('\n=== key S3E entry points ===');
    entries.forEach(function (e) {
        if (e.name && /s3eDeviceYield|s3eDeviceRegister|s3eDeviceExit|s3eSurfaceShow|s3ePointer|s3eKeyboard/.test(e.name)) {
            console.log('  PLT#' + e.index + ' (' + e.stubOffset + ')  ' + e.stub + '  ' + e.name);
        }
    });

    var outPath = '/data/data/com.nekki.shadowfight/plt_map.json';
    try {
        var out = new File(outPath, 'w');
        out.write(JSON.stringify({
            regionStart: r.start.toString(),
            imageBase: r.start.sub(r.fileOff).toString(),
            entryCount: entries.length,
            entrySize: 16,
            named: named,
            entries: entries
        }, null, 1));
        out.flush();
        out.close();
        console.log('\nwrote ' + outPath);
    } catch (e) {
        console.log('[!] write failed: ' + e);
    }
    send({ type: 'plt', count: entries.length, named: named, path: outPath });
}

var tries = 0;
(function poll() {
    if (findGameRegion()) { run(); return; }
    if (++tries > 120) { console.log('[TIMEOUT]'); return; }
    setTimeout(poll, 250);
})();
