'use strict';
/*
 * dump_game_region.js
 * Dumps the live S3E game-code region (rwxs /dev/zero mapping, ~8.18MB)
 * to disk so it can be loaded into Ghidra at its true runtime base.
 *
 * Region is located by parsing /proc/self/maps -- Process.enumerateRanges()
 * does not report it because it is a *shared* mapping.
 *
 * Output: /data/data/com.nekki.shadowfight/game_region.bin
 * Pull with: adb shell su -c 'cat /data/data/com.nekki.shadowfight/game_region.bin' > local.bin
 */

function findGameRegion() {
    var best = null;
    try {
        var f = new File('/proc/self/maps', 'r');
        var l;
        while ((l = f.readLine()) !== null && l.length > 0) {
            var m = /^([0-9a-f]+)-([0-9a-f]+)\s+(\S{4})\s+([0-9a-f]+)\s+\S+\s+\S+\s*(.*)$/.exec(l.trim());
            if (!m) continue;
            var path = m[5] || '';
            var prot = m[3];
            var size = parseInt(m[2], 16) - parseInt(m[1], 16);
            if (path.indexOf('/dev/zero') < 0) continue;
            if (prot.indexOf('x') < 0) continue;
            if (size < 4 * 1024 * 1024) continue;
            if (!best || size > best.size) {
                best = {
                    start: ptr('0x' + m[1]),
                    end: ptr('0x' + m[2]),
                    size: size,
                    prot: prot,
                    fileOff: parseInt(m[4], 16)
                };
            }
        }
        f.close();
    } catch (e) {
        console.log('[!] maps read failed: ' + e);
    }
    return best;
}

function dump() {
    var r = findGameRegion();
    if (!r) { console.log('[FAIL] region not found'); return; }

    r.imageBase = r.start.sub(r.fileOff);
    console.log('region ' + r.start + '-' + r.end + ' (' + (r.size / 1048576).toFixed(2) + 'MB) ' + r.prot);
    console.log('image base ' + r.imageBase);

    var outPath = '/data/data/com.nekki.shadowfight/game_region.bin';
    var out;
    try {
        out = new File(outPath, 'wb');
    } catch (e) {
        console.log('[FAIL] cannot open ' + outPath + ': ' + e);
        return;
    }

    var CHUNK = 64 * 1024;
    var written = 0, failed = 0;
    for (var off = 0; off < r.size; off += CHUNK) {
        var n = Math.min(CHUNK, r.size - off);
        var buf = null;
        try {
            buf = r.start.add(off).readByteArray(n);
        } catch (e) {
            buf = null;
        }
        if (buf) {
            out.write(buf);
            written += n;
        } else {
            // keep file offsets aligned with memory offsets
            out.write(new Uint8Array(n));
            failed += n;
        }
    }
    out.flush();
    out.close();

    console.log('wrote ' + written + ' bytes, ' + failed + ' unreadable (zero-filled)');
    console.log('output: ' + outPath);
    send({ type: 'dumped', path: outPath, imageBase: r.imageBase.toString(),
           codeStart: r.start.toString(), size: r.size, unreadable: failed });
}

var tries = 0;
(function poll() {
    if (findGameRegion()) { dump(); return; }
    if (++tries > 120) { console.log('[TIMEOUT]'); return; }
    setTimeout(poll, 250);
})();
