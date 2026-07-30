// auto_capture.js — self-contained trace capture for automation
// Run: frida -U 16605 -l auto_capture.js --no-pause -o trace_dump.json
// Exits automatically after 25 seconds and dumps all traces to stdout.

'use strict';

var S3E_SO        = "libs3e_android.so";
var CAPTURE_MS    = 25000;  // capture for 25 seconds
var MAX_TRACES    = 50000;

var traces       = [];
var sessionStart = Date.now();
var gameBase     = null;
var gameSize     = 0;
var s3eBase      = null;
var captureStart = 0;
var foundBattle  = false;

function nowMs() { return Date.now() - sessionStart; }
function hex(a) { return a ? "0x" + a.toString(16) : "0x0"; }

function readFloatSafe(p, off) {
    try { return p.add(off).readFloat(); } catch (e) { return NaN; }
}
function readU32Safe(p, off) {
    try { return p.add(off).readU32(); } catch (e) { return 0; }
}

function pushTrace(evt) {
    if (traces.length >= MAX_TRACES) return;
    evt.t_ms = nowMs();
    traces.push(evt);
}

function resolveModules() {
    if (!gameBase) {
        try {
            var ranges = Process.enumerateRanges('r-x');
            for (var i = 0; i < ranges.length; i++) {
                var r = ranges[i];
                if (r.size > 5 * 1024 * 1024 && r.size < 10 * 1024 * 1024) {
                    var isSystem = false;
                    try {
                        var mod = Process.findModuleByAddress(r.base);
                        if (mod) {
                            var n = mod.name;
                            if (n.includes("libandroid") || n.includes("libc.") || 
                                n.includes("libart") || n.includes("boot.") ||
                                n.includes("libhidl") || n.includes("libbinder")) {
                                isSystem = true;
                            }
                        }
                    } catch (e) {}
                    if (!isSystem) {
                        gameBase = r.base;
                        gameSize = r.size;
                        console.log("[auto] Game region @ " + hex(gameBase) + 
                                    " size=" + (gameSize/1024/1024).toFixed(1) + "MB");
                        break;
                    }
                }
            }
        } catch (e) { console.log("[auto] scan error: " + e); }
    }
    if (!s3eBase) {
        try {
            var s = Module.findBaseAddress(S3E_SO);
            if (s) { s3eBase = s; console.log("[auto] S3E @ " + hex(s3eBase)); }
        } catch (e) {}
    }
}

function readFighter(p) {
    if (!p || p.isNull() || p.compare(ptr(0x1000)) < 0) return null;
    return {
        ms:  readU32Safe(p, 0x0B4),
        px:  readFloatSafe(p, 0x0C8),
        py:  readFloatSafe(p, 0x0CC),
        fr:  readU32Safe(p, 0x0D0) !== 0,
        blk: readU32Safe(p, 0x120) !== 0,
        hp:  readFloatSafe(p, 0x140),
        hpM: readFloatSafe(p, 0x144),
        aid: readU32Safe(p, 0x160)
    };
}

function hookTick() {
    if (!gameBase) return;
    var addr = gameBase.add(0x0002f0e0);
    try {
        Interceptor.attach(addr, {
            onEnter: function (args) { this.selfPtr = args[0]; },
            onLeave: function () {
                try {
                    var self = this.selfPtr;
                    if (!self) return;
                    var player = self.add(0x08).readPointer();
                    var enemy  = self.add(0x0C).readPointer();
                    var pData = readFighter(player);
                    var eData = readFighter(enemy);
                    
                    // Detect battle start: both fighters have valid health
                    if (pData && eData && pData.hp > 0 && eData.hp > 0 && !foundBattle) {
                        foundBattle = true;
                        captureStart = nowMs();
                        console.log("[auto] BATTLE DETECTED! hp=" + pData.hp + " vs " + eData.hp);
                    }
                    
                    pushTrace({type:"s", d:{p:pData, e:eData}});
                } catch (e) {}
            }
        });
        console.log("[auto] hooked tick @ " + hex(addr));
    } catch (e) { console.log("[auto] hook failed: " + e); }
}

// Auto-capture and exit
function main() {
    console.log("[auto] Loading...");
    resolveModules();
    if (gameBase) {
        hookTick();
        console.log("[auto] Capturing for " + (CAPTURE_MS/1000) + "s...");
    } else {
        // Retry
        setTimeout(function() {
            resolveModules();
            if (gameBase) {
                hookTick();
                console.log("[auto] Capturing for " + (CAPTURE_MS/1000) + "s...");
            }
        }, 3000);
    }
    
    // After CAPTURE_MS, dump traces and exit
    setTimeout(function() {
        console.log("\n=== TRACE_DUMP_START ===");
        console.log(JSON.stringify(traces));
        console.log("=== TRACE_DUMP_END ===");
        console.log("[auto] Done. " + traces.length + " events captured.");
        // Exit by throwing — forces frida to disconnect
        // Or just let the script finish naturally
    }, CAPTURE_MS + 5000);
}

main();
