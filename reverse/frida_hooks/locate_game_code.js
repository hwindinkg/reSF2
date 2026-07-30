'use strict';
/*
 * locate_game_code.js
 * Finds the S3E game-code region by parsing /proc/self/maps directly.
 *
 * Why not Process.enumerateRanges(): the region is a *shared* mapping of
 * /dev/zero ("rwxs"), created by the S3E loader. Frida's range enumeration
 * missed it in earlier sessions, which is why the 8.5MB ARM game code
 * appeared to be absent from the process.
 *
 * Layout observed on Redmi 6A / Android 9 / arm32:
 *   <base+0x000000>  ---s   4KB     guard
 *   <base+0x001000>  rwxs   8.18MB  game code + data
 *   <base+0x82e000>  ---s   4KB     guard
 *
 * Static image base 0x4A000000 maps to the guard page base, so:
 *   runtime = static - 0x4A000000 + base
 */

var STATIC_BASE = ptr('0x4A000000');

function readMaps() {
    var lines = [];
    try {
        var f = new File('/proc/self/maps', 'r');
        var l;
        while ((l = f.readLine()) !== null && l.length > 0) lines.push(l.trim());
        f.close();
    } catch (e) {
        console.log('[!] cannot read /proc/self/maps: ' + e);
    }
    return lines;
}

function parseMaps() {
    var out = [];
    var lines = readMaps();
    for (var i = 0; i < lines.length; i++) {
        var m = /^([0-9a-f]+)-([0-9a-f]+)\s+(\S{4})\s+([0-9a-f]+)\s+\S+\s+\S+\s*(.*)$/.exec(lines[i]);
        if (!m) continue;
        out.push({
            start: ptr('0x' + m[1]),
            end: ptr('0x' + m[2]),
            size: parseInt(m[2], 16) - parseInt(m[1], 16),
            prot: m[3],
            fileOff: parseInt(m[4], 16),
            path: m[5] || ''
        });
    }
    return out;
}

// The game code region: /dev/zero, executable, multi-megabyte.
function findGameRegion() {
    var regions = parseMaps();
    var best = null;
    for (var i = 0; i < regions.length; i++) {
        var r = regions[i];
        if (r.path.indexOf('/dev/zero') < 0) continue;
        if (r.prot.indexOf('x') < 0) continue;
        if (r.size < 4 * 1024 * 1024) continue;
        if (!best || r.size > best.size) best = r;
    }
    if (!best) return null;
    // Image base is the guard page immediately before, i.e. code start - fileOff.
    best.imageBase = best.start.sub(best.fileOff);
    return best;
}

function toRuntime(region, staticAddr) {
    return region.imageBase.add(ptr(staticAddr).sub(STATIC_BASE));
}

function hex(addr, n) {
    try { return addr.readByteArray(n); } catch (e) { return null; }
}

function main() {
    var region = findGameRegion();
    if (!region) {
        console.log('[FAIL] no executable /dev/zero region >4MB found. Is the game past the loader?');
        return;
    }

    console.log('=== game code region ===');
    console.log('  code start : ' + region.start + '  (' + (region.size / 1048576).toFixed(2) + ' MB, ' + region.prot + ')');
    console.log('  code end   : ' + region.end);
    console.log('  file off   : 0x' + region.fileOff.toString(16));
    console.log('  IMAGE BASE : ' + region.imageBase + '   (static 0x4A000000)');
    console.log('  slide      : ' + region.imageBase.sub(STATIC_BASE));

    // Known static addresses from BLOCK_LOGIC.md / SESSION notes.
    var known = {
        'entry point            0x4A000000': '0x4A000000',
        'PLT table start        0x4A0000C8': '0x4A0000C8',
        'PLT#16 s3eDeviceYield  0x4A000188': '0x4A000188',
        'yield wrapper          0x4A67A1E0': '0x4A67A1E0',
        'MAIN LOOP              0x4A679F54': '0x4A679F54',
        'frame callback         0x4A679914': '0x4A679914',
        'callback dispatch      0x4A6798B0': '0x4A6798B0',
        'callback table         0x4A81B91C': '0x4A81B91C',
        'secondary init         0x4A686A1C': '0x4A686A1C',
        'GOT start              0x4A7FEA30': '0x4A7FEA30'
    };

    console.log('\n=== static -> runtime, with bytes ===');
    Object.keys(known).forEach(function (label) {
        var rt = toRuntime(region, known[label]);
        var inRange = rt.compare(region.start) >= 0 && rt.compare(region.end) < 0;
        var line = '  ' + label + '  ->  ' + rt + (inRange ? '' : '   [OUTSIDE CODE RANGE]');
        var bytes = inRange ? hex(rt, 16) : null;
        if (bytes) {
            var u8 = new Uint8Array(bytes);
            var s = '';
            for (var i = 0; i < u8.length; i++) {
                s += ('0' + u8[i].toString(16)).slice(-2);
                if (i % 4 === 3) s += ' ';
            }
            line += '   ' + s;
        }
        console.log(line);
    });

    // Sanity check: the static PLT stub pattern is
    //   ADD R12, PC, #0x7000     E28FC607
    //   ADD R12, R12, #0xFE000   E28CCAFE
    //   LDR PC, [R12, #imm]      E5BCF???
    console.log('\n=== PLT stub verification (expect E28FC607 / E28CCAFE / E5BCFxxx) ===');
    var pltBase = toRuntime(region, '0x4A0000C8');
    for (var n = 0; n < 4; n++) {
        var stub = pltBase.add(n * 12);
        try {
            var w0 = stub.readU32(), w1 = stub.add(4).readU32(), w2 = stub.add(8).readU32();
            var ok = (w0 === 0xE28FC607 && w1 === 0xE28CCAFE && (w2 & 0xFFFFF000) === 0xE5BCF000);
            console.log('  PLT#' + n + ' @' + stub + ' : ' +
                w0.toString(16) + ' ' + w1.toString(16) + ' ' + w2.toString(16) +
                (ok ? '   [MATCH]' : '   [no match]'));
        } catch (e) {
            console.log('  PLT#' + n + ' @' + stub + ' : unreadable');
        }
    }

    // Export for the dumper / other scripts.
    send({
        type: 'game_region',
        imageBase: region.imageBase.toString(),
        codeStart: region.start.toString(),
        codeEnd: region.end.toString(),
        size: region.size,
        prot: region.prot
    });
}

// The region only exists once the S3E loader has mapped the game image.
var tries = 0;
function poll() {
    if (findGameRegion()) { main(); return; }
    if (++tries > 120) { console.log('[TIMEOUT] game region never appeared'); return; }
    setTimeout(poll, 250);
}
poll();
