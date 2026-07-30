'use strict';
// Captures traces for 20 seconds then SENDS all data via send() for us to capture.
// No REPL interaction needed - just let it run and capture stdout.
// Usage: frida -U 16605 -l auto_capture4.js -q > output.txt

var MAX    = 50000;
var traces = [];
var start  = Date.now();
var gameBase = null;
var done     = false;

function hex(a) { return a ? "0x"+a.toString(16) : "0x0"; }
function r32(p,o) { try{return p.add(o).readU32()}catch(e){return 0} }
function rf(p,o)  { try{return p.add(o).readFloat()}catch(e){return NaN} }

function scan() {
    var knownAddr = ptr("0x8f197000");
    try {
        Memory.readByteArray(knownAddr, 4);
        gameBase = knownAddr;
    } catch(e) {
        var rs = Process.enumerateRanges('r-x');
        for (var i = 0; i < rs.length; i++) {
            var r = rs[i];
            if (r.size > 7*1024*1024 && r.size < 10*1024*1024) {
                var mod = null;
                try { mod = Process.findModuleByAddress(r.base); } catch(ex) {}
                if (!mod || !(mod.name.includes("libc")||mod.name.includes("libart")||mod.name.includes("libhidl")||mod.name.includes("libbinder")||mod.name.includes("libandroid"))) {
                    gameBase = r.base; break;
                }
            }
        }
        if (!gameBase) { console.error("[FAIL] No game region"); return; }
    }
}

function hook() {
    if (!gameBase) return;
    var addr = gameBase.add(0x0002f0e0);
    try {
        Interceptor.attach(addr, {
            onEnter: function(a){this.self=a[0]},
            onLeave: function() {
                if (done || traces.length >= MAX) return;
                try {
                    var s = this.self; if (!s) return;
                    var p = s.add(0x08).readPointer();
                    var e = s.add(0x0C).readPointer();
                    var pd = (!p||p.isNull()||p.compare(ptr(0x1000))<0) ? null : {
                        ms:r32(p,0x0B4),px:rf(p,0x0C8),py:rf(p,0x0CC),
                        fr:r32(p,0x0D0),blk:r32(p,0x120),hp:rf(p,0x140),
                        hpm:rf(p,0x144),aid:r32(p,0x160)
                    };
                    var ed = (!e||e.isNull()||e.compare(ptr(0x1000))<0) ? null : {
                        ms:r32(e,0x0B4),px:rf(e,0x0C8),py:rf(e,0x0CC),
                        fr:r32(e,0x0D0),blk:r32(e,0x120),hp:rf(e,0x140),
                        hpm:rf(e,0x144),aid:r32(e,0x160)
                    };
                    traces.push({p:pd,e:ed,t:Date.now()-start});
                } catch(ex){}
            }
        });
    } catch(e) { console.error("[ERR] hook: "+e); }
}

scan();
hook();

console.log("[OK] Region=" + hex(gameBase) + " | Hook installed, capturing 20s...");

// Use send() instead of console.log — it flushes through the Frida protocol immediately
setTimeout(function() {
    done = true;
    console.log("[DONE] Captured " + traces.length + " frames");
    send(JSON.stringify(traces));
}, 20000);
