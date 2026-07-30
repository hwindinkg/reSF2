'use strict';
// Stalker: trace ALL function calls in the game region for ~100ms
// to find Model::tick by identifying the most-called function

var gameBase = ptr("0x8f197000");
var gameSize = 0x9C4000 - 0x197000; // ~8.2MB
var gameEnd = gameBase.add(gameSize);

var callCounts = {};
var totalCalls = 0;

console.log("[*] Game region: " + gameBase + " - " + gameEnd);

var tid = Process.getCurrentThreadId();
console.log("[*] Stalking thread " + tid);

// Very short stalk: collect all call/return addresses
Stalker.follow({
    events: {
        call: true
    },
    onReceive: function (events) {
        Stalker.parse(events, {
            onCall: function (address, target) {
                if (target.compare(gameBase) >= 0 && target.compare(gameEnd) < 0) {
                    // It's our game code
                    var offset = target.sub(gameBase).toInt32();
                    var key = "0x" + offset.toString(16);
                    if (callCounts[key] === undefined) {
                        callCounts[key] = 0;
                    }
                    callCounts[key]++;
                    totalCalls++;
                }
            }
        });
    }
});

// Stalk for 100ms then stop and report
setTimeout(function () {
    Stalker.unfollow();
    
    // Convert to array and sort
    var entries = [];
    for (var key in callCounts) {
        entries.push({ addr: key, count: callCounts[key] });
    }
    entries.sort(function (a, b) { return b.count - a.count; });
    
    console.log("[*] Total game calls traced: " + totalCalls);
    console.log("[*] Top 30 most-called functions:");
    for (var i = 0; i < Math.min(entries.length, 30); i++) {
        var e = entries[i];
        console.log("  " + e.addr + " -> called " + e.count + " times (" + ((e.count / totalCalls) * 100).toFixed(1) + "%)");
    }
    
    if (entries.length === 0) {
        console.log("[!] No game functions called! The game might not be executing code.");
    }
}, 100);
