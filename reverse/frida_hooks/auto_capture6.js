'use strict';
// Writes traces to /data/local/tmp/ line-by-line during capture.
// No timers needed — each frame is written immediately.
// After 20s, signal by checking time.

var CAPTURE_MS = 20000;
var MAX_FRAMES = 100000;
var start      = Date.now();
var frameCount = 0;
var gameBase   = null;
var done       = false;
var file       = null;

function hex(a) { return a ? "0x"+a.toString(16) : "0x0"; }
function r32(p,o) { try{return p.add(o).readU32()}catch(e){return 0} }
function rf(p,o)  { try{return p.add(o).readFloat()}catch(e){return NaN} }

function scan() {
    var knownAddr = ptr("0x8f197000");
    try { Memory.readByteArray(knownAddr, 4); gameBase = knownAddr; return; } catch(e) {}
    var rs = Process.enumerateRanges('r-x');
    for (var i = 0; i < rs.length; i++) {
        var r = rs[i];
        if (r.size > 7*1024*1024 && r.size < 10*1024*1024) {
            var mod = null;
            try { mod = Process.findModuleByAddress(r.base); } catch(ex) {}
            if (!mod || !(mod.name.includes("libc")||mod.name.includes("libart")||
                mod.name.includes("libhidl")||mod.name.includes("libbinder")||
                mod.name.includes("libandroid"))) {
                gameBase = r.base; break;
            }
        }
    }
}

function hook() {
    if (!gameBase) return;
    var addr = gameBase.add(0x0002f0e0);
    try {
        Interceptor.attach(addr, {
            onEnter: function(a){this.self=a[0]},
            onLeave: function() {
                if (done) return;
                if (Date.now() - start > CAPTURE_MS) {
                    done = true;
                    if (file) { file.flush(); file.close(); file = null; }
                    console.log("[DONE] " + frameCount + " frames written");
                    return;
                }
                if (frameCount >= MAX_FRAMES) { done = true; return; }
                frameCount++;
                try {
                    var s = this.self; if (!s) return;
                    var p = s.add(0x08).readPointer();
                    var e = s.add(0x0C).readPointer();
                    var pd = (!p||p.isNull()||p.compare(ptr(0x1000))<0) ? null :
                        JSON.stringify({ms:r32(p,0x0B4),px:rf(p,0x0C8),py:rf(p,0x0CC),
                            fr:r32(p,0x0D0),blk:r32(p,0x120),hp:rf(p,0x140),
                            hpm:rf(p,0x144),aid:r32(p,0x160)});
                    var ed = (!e||e.isNull()||e.compare(ptr(0x1000))<0) ? null :
                        JSON.stringify({ms:r32(e,0x0B4),px:rf(e,0x0C8),py:rf(e,0x0CC),
                            fr:r32(e,0x0D0),blk:r32(e,0x120),hp:rf(e,0x140),
                            hpm:rf(e,0x144),aid:r32(e,0x160)});
                    var line = '{"t":' + (Date.now()-start) + ',"p":' + pd + ',"e":' + ed + '}\n';
                    if (file) file.write(line);
                } catch(ex){}
            }
        });
    } catch(e) { console.log("[ERR] hook: "+e); }
}

function main() {
    scan();
    if (!gameBase) { 
        console.log("[FAIL] No game region"); 
        return;
    }
    
    try {
        file = new File("/data/data/com.nekki.shadowfight/frida_traces.jsonl", "w");
        console.log("[OK] File opened at /data/data/com.nekki.shadowfight/frida_traces.jsonl");
    } catch(e) { 
        // Try sdcard as fallback
        try {
            file = new File("/sdcard/frida_traces.jsonl", "w");
            console.log("[OK] File opened at /sdcard/frida_traces.jsonl");
        } catch(e2) {
            console.log("[ERR] File both paths: " + e + " | " + e2); 
            return;
        }
    }
    
    hook();
    console.log("[OK] Hooked @" + hex(gameBase.add(0x0002f0e0)) + " | capturing...");
}

main();
