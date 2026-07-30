'use strict';
// Stalker v2: trace calls until we collect N entries, then stop and report
// Uses setImmediate to avoid REPL timer issues

var gameBase = ptr("0x8f197000");
var gameEnd = gameBase.add(0x82D000); // ~8.2MB

var callCounts = {};
var totalCalls = 0;
var DURATION_MS = 500; // trace for 500ms
var startTime = Date.now();

console.log("[*] Game region: " + gameBase + " - " + gameEnd);
console.log("[*] Stalking for " + DURATION_MS + "ms on thread " + Process.getCurrentThreadId());

function stopAndReport() {
    Stalker.unfollow();
    
    var entries = [];
    for (var key in callCounts) {
        entries.push({ addr: key, count: callCounts[key] });
    }
    entries.sort(function (a, b) { return b.count - a.count; });
    
    console.log("[*] Total game calls: " + totalCalls);
    console.log("[*] Top 40 most-called functions:");
    for (var i = 0; i < Math.min(entries.length, 40); i++) {
        var e = entries[i];
        console.log("  " + e.addr + " -> " + e.count + "x (" + ((e.count / totalCalls) * 100).toFixed(1) + "%)");
    }
    
    if (entries.length === 0) {
        console.log("[!] No game functions called!");
    }
}

Stalker.follow({
    events: {
        call: true
    },
    onReceive: function (events) {
        Stalker.parse(events, {
            onCall: function (address, target) {
                if (target.compare(gameBase) >= 0 && target.compare(gameEnd) < 0) {
                    var offset = target.sub(gameBase).toInt32();
                    var key = "0x" + offset.toString(16);
                    callCounts[key] = (callCounts[key] || 0) + 1;
                    totalCalls++;
                }
            }
        });
        // Check if we've been running long enough
        if (Date.now() - startTime > DURATION_MS) {
            stopAndReport();
        }
    }
});

// Keep checking every event
console.log("[*] Stalker active, waiting for calls...");
