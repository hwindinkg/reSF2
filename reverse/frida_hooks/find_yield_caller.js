'use strict';
// Hook s3eDeviceYield and show call stack to trace the game loop
// This is the Marmalade frame synchronization function

var gameBase = ptr("0x8f197000");
var gameEnd = gameBase.add(0x82D000);
var foundTicks = {};

console.log("[*] Looking for s3eDeviceYield...");

// Try to find s3eDeviceYield in libs3e_android.so
var mod = Process.findModuleByName("libs3e_android.so");
console.log("[*] libs3e_android.so base: " + mod.base + " size: 0x" + mod.size.toString(16));

// Use DebugSymbol for s3eDeviceYield
var yieldAddr = DebugSymbol.fromName("s3eDeviceYield");
if (yieldAddr) {
    console.log("[*] s3eDeviceYield found at " + yieldAddr.address);
} else {
    // Try to find it by name
    var exports = Module.enumerateExports("libs3e_android.so");
    for (var i = 0; i < exports.length; i++) {
        if (exports[i].name.indexOf("s3eDeviceYield") >= 0 || 
            exports[i].name.indexOf("DeviceYield") >= 0) {
            console.log("[*] Found: " + exports[i].name + " at " + exports[i].address);
            yieldAddr = { address: exports[i].address };
        }
    }
}

if (yieldAddr) {
    var sampleCount = 0;
    Interceptor.attach(yieldAddr.address, {
        onEnter: function () {
            sampleCount++;
            if (sampleCount <= 20) {
                console.log("[" + sampleCount + "] s3eDeviceYield called");
                // Print backtrace
                var bt = Thread.backtrace(this.context, Backtracer.ACCURATE);
                for (var j = 0; j < Math.min(bt.length, 8); j++) {
                    var addr = bt[j];
                    var offset = "?";
                    if (addr.compare(gameBase) >= 0 && addr.compare(gameEnd) < 0) {
                        offset = " (game+" + addr.sub(gameBase).toString(16) + ")";
                    }
                    var sym = DebugSymbol.fromAddress(addr);
                    console.log("    #" + j + ": " + addr + offset + " " + sym.name);
                }
                
                // Count which game function calls yield
                if (bt.length >= 2) {
                    var caller = bt[1];
                    if (caller.compare(gameBase) >= 0 && caller.compare(gameEnd) < 0) {
                        var offset = caller.sub(gameBase).toInt32();
                        var key = "0x" + offset.toString(16);
                        foundTicks[key] = (foundTicks[key] || 0) + 1;
                    }
                }
            } else if (sampleCount === 30) {
                // Report findings
                console.log("\n[*] Functions calling s3eDeviceYield from game code:");
                var sorted = [];
                for (var k in foundTicks) sorted.push({ addr: k, count: foundTicks[k] });
                sorted.sort(function(a,b) { return b.count - a.count; });
                for (var i = 0; i < sorted.length; i++) {
                    console.log("  game+" + sorted[i].addr + " called " + sorted[i].count + "x");
                }
            }
        }
    });
    console.log("[*] Hooked s3eDeviceYield. Sampling first 30 calls...");
} else {
    console.log("[!] Could not find s3eDeviceYield");
}
