'use strict';
// Quick auto-capture: runs 15 seconds, dumps ALL traces, exits.
// Usage: frida -U 16605 -l auto_capture2.js -o dump.txt

var S3E_SO = "libs3e_android.so";
var MAX    = 30000;
var traces = [];
var start  = Date.now();
var gameBase = null;
var s3eBase  = null;
var done      = false;
var battleStarted = false;
var battleFrames  = 0;

function hex(a) { return a ? "0x"+a.toString(16) : "0x0"; }
function r32(p,o) { try{return p.add(o).readU32()}catch(e){return 0} }
function rf(p,o)  { try{return p.add(o).readFloat()}catch(e){return NaN} }

// Scan for game region
function scan() {
    if (!gameBase) {
        var rs = Process.enumerateRanges('r-x');
        for (var i = 0; i < rs.length; i++) {
            var r = rs[i];
            if (r.size > 5*1024*1024 && r.size < 10*1024*1024) {
                var mod = Process.findModuleByAddress(r.base);
                if (!mod || !(mod.name.includes("libc")||mod.name.includes("libart")||mod.name.includes("libhidl")||mod.name.includes("libbinder"))) {
                    gameBase = r.base;
                    break;
                }
            }
        }
    }
    if (!s3eBase) {
        try { s3eBase = Module.findBaseAddress(S3E_SO); } catch(e) {}
    }
}

// Hook Model::tick
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
                    var pd = (!p||p.isNull()||p.compare(ptr(0x1000))<0) ? null : {ms:r32(p,0x0B4),px:rf(p,0x0C8),py:rf(p,0x0CC),fr:r32(p,0x0D0),blk:r32(p,0x120),hp:rf(p,0x140),hpm:rf(p,0x144),aid:r32(p,0x160)};
                    var ed = (!e||e.isNull()||e.compare(ptr(0x1000))<0) ? null : {ms:r32(e,0x0B4),px:rf(e,0x0C8),py:rf(e,0x0CC),fr:r32(e,0x0D0),blk:r32(e,0x120),hp:rf(e,0x140),hpm:rf(e,0x144),aid:r32(e,0x160)};
                    
                    if (pd && ed && pd.hp > 0 && ed.hp > 0) {
                        if (!battleStarted) {
                            battleStarted = true;
                            console.log("[CAPTURE] Battle started! frames=" + traces.length);
                        }
                        battleFrames++;
                    }
                    
                    traces.push({p:pd,e:ed,t:Date.now()-start});
                } catch(e){}
            }
        });
    } catch(e) { console.log("[ERROR] hook: "+e); }
}

// Main
scan();
if (gameBase) {
    hook();
    console.log("[CAPTURE] OK. Region=" + hex(gameBase) + " S3E=" + hex(s3eBase));
} else {
    console.log("[ERROR] No game region found");
}

// Exit after 20s
setTimeout(function() {
    done = true;
    var dump = JSON.stringify(traces);
    console.log("===TRACE_JSON_BEGIN===");
    console.log(dump);
    console.log("===TRACE_JSON_END===");
    console.log("[CAPTURE] Total frames=" + traces.length + " battle_frames=" + battleFrames);
}, 20000);
