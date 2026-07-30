'use strict';
// Saves traces to /data/local/tmp/ on Android, then we pull with adb.

var S3E_SO = "libs3e_android.so";
var MAX    = 50000;
var traces = [];
var start  = Date.now();
var gameBase = null;
var s3eBase  = null;
var done     = false;

function hex(a) { return a ? "0x"+a.toString(16) : "0x0"; }
function r32(p,o) { try{return p.add(o).readU32()}catch(e){return 0} }
function rf(p,o)  { try{return p.add(o).readFloat()}catch(e){return NaN} }

function scan() {
    // Try known base address first (from previous session)
    var knownAddr = ptr("0x8f197000");
    try {
        Memory.readByteArray(knownAddr, 4);
        gameBase = knownAddr;
        console.log("[scan] Using known base: " + gameBase);
    } catch(e) {
        // Fallback: scan for large rx regions
        var rs = Process.enumerateRanges('r-x');
        var candidates = [];
        for (var i = 0; i < rs.length; i++) {
            var r = rs[i];
            if (r.size > 7*1024*1024 && r.size < 10*1024*1024) {
                var mod = "?";
                try { var m = Process.findModuleByAddress(r.base); if(m) mod = m.name; } catch(e) {}
                if (mod && (mod.includes("libc")||mod.includes("libart")||mod.includes("libhidl")||
                    mod.includes("libbinder")||mod.includes("libandroid")||mod.includes("libm."))) continue;
                candidates.push({base:r.base, size:r.size, mod:mod});
            }
        }
        candidates.sort(function(a,b){return b.size - a.size});
        console.log("[scan] Candidates: " + candidates.length);
        for (var i = 0; i < candidates.length; i++) {
            console.log("[scan]  " + candidates[i].base + " " + (candidates[i].size/1024/1024).toFixed(1) + "MB " + candidates[i].mod);
        }
        if (candidates.length > 0) {
            gameBase = candidates[0].base;
            console.log("[scan] Using " + gameBase);
        }
    }
    if (!s3eBase) { try { s3eBase = Module.findBaseAddress(S3E_SO); } catch(e) {} }
}

function hook() {
    if (!gameBase) { console.log("[ERROR] No game base"); return; }
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
                        ms:r32(p,0x0B4), px:rf(p,0x0C8), py:rf(p,0x0CC),
                        fr:r32(p,0x0D0), blk:r32(p,0x120), hp:rf(p,0x140),
                        hpm:rf(p,0x144), aid:r32(p,0x160)
                    };
                    var ed = (!e||e.isNull()||e.compare(ptr(0x1000))<0) ? null : {
                        ms:r32(e,0x0B4), px:rf(e,0x0C8), py:rf(e,0x0CC),
                        fr:r32(e,0x0D0), blk:r32(e,0x120), hp:rf(e,0x140),
                        hpm:rf(e,0x144), aid:r32(e,0x160)
                    };
                    traces.push({p:pd,e:ed,t:Date.now()-start});
                } catch(e){}
            }
        });
        console.log("[hook] OK at " + addr);
    } catch(e) { console.log("[ERROR] hook failed: "+e); }
}

// Save to file on Android
function saveToFile() {
    console.log("[SAVE] Writing " + traces.length + " traces...");
    try {
        var file = new File("/data/local/tmp/frida_traces.json", "w");
        file.write(JSON.stringify(traces));
        file.flush();
        file.close();
        console.log("[SAVE] OK - /data/local/tmp/frida_traces.json");
    } catch(e) {
        console.log("[SAVE] FAILED: " + e);
        // Fallback: log to console
        console.log("===TRACE_BEGIN===");
        console.log(JSON.stringify(traces));
        console.log("===TRACE_END===");
    }
}

scan();
if (gameBase) {
    hook();
    console.log("[OK] Region=" + hex(gameBase) + " S3E=" + hex(s3eBase));
} else {
    console.log("[FAIL] No game region");
}

// 20s capture, then save and exit
setTimeout(function() {
    done = true;
    saveToFile();
    // Frida will stay at REPL but that's OK - we just need the file written
}, 20000);
