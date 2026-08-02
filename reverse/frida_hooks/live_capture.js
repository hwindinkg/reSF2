/*
 * SF2 live evidence v6 (log-only, read-only). Final attempt.
 * t=8s: probe ASLR base, scan ALL range types for game string tables.
 */
'use strict';

function log(msg) { console.log('[cap] ' + msg); }

setTimeout(function () {
    log('--- INIT t=8s ---');

    // probe crash-derived base candidates
    [ptr('0x8da00000'), ptr('0x8db00000'), ptr('0x8dc00000'), ptr('0x8f35f000')].forEach(function (b) {
        try {
            var first = Array.from(new Uint8Array(b.readByteArray(8))).map(function (x) { return ('0' + x.toString(16)).slice(-2); }).join(' ');
            log('probe ' + b + ' = ' + first);
        } catch (e) { log('probe ' + b + ' unmapped'); }
    });

    // scan every range type > 2MB for markers
    var all = Process.enumerateRanges('---').concat(Process.enumerateRanges('r-x')).concat(Process.enumerateRanges('r--')).concat(Process.enumerateRanges('rw-'));
    var seen = {};
    var markers = ['weapon_', 'armor_', 'helm_', 'moves.xml', 'files.dz', 'punching_bag'];
    markers.forEach(function (pat) {
        var hex = '';
        for (var j = 0; j < pat.length; j++) hex += ('0' + pat.charCodeAt(j).toString(16)).slice(-2);
        hex += '00';
        var total = 0, samples = [];
        all.forEach(function (r) {
            var key = r.base.toString();
            if (seen[key]) return; seen[key] = true;
            if (r.size < 2000000) return;
            try {
                var hits = Memory.scanSync(r.base, r.size, hex);
                total += hits.length;
                hits.slice(0, 4).forEach(function (h) {
                    var str = '';
                    try { str = h.address.readUtf8String(64); } catch (e) { }
                    samples.push('[' + r.protection + ']' + h.address + ':"' + str + '"');
                });
            } catch (e) { }
        });
        log('STR ' + pat + ' x' + total + (samples.length ? ' :: ' + samples.join(' | ') : ''));
    });
}, 8000);

setInterval(function () { log('HEARTBEAT'); }, 8000);
