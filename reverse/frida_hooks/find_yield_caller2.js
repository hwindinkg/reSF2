'use strict';
// Hook s3eDeviceYield, collect backtrace, report all at once
var gameBase = ptr("0x8f197000");
var gameEnd = gameBase.add(0x82D000);
var frames = [];

var yieldAddr = ptr("0x961df974");
var startTime = Date.now();

Interceptor.attach(yieldAddr, {
    onEnter: function (args) {
        var elapsed = Date.now() - startTime;
        if (elapsed > 8000) return; // stop collecting after 8s
        
        var bt = Thread.backtrace(this.context, Backtracer.ACCURATE);
        var frame = [];
        for (var j = 0; j < Math.min(bt.length, 5); j++) {
            var addr = bt[j];
            if (addr.compare(gameBase) >= 0 && addr.compare(gameEnd) < 0) {
                frame.push("G+" + addr.sub(gameBase).toString(16));
            } else {
                var sym = DebugSymbol.fromAddress(addr);
                var n = sym.name || "";
                frame.push(addr.toString(16).substr(0,10) + (n ? ":" + n : ""));
            }
        }
        frames.push(frame);
    }
});

// Wait 10s then report
var check = setImmediate(function check() {
    if (Date.now() - startTime > 10000 || frames.length >= 10) {
        console.log("[*] Collected " + frames.length + " samples of s3eDeviceYield callers:\n");
        for (var i = 0; i < frames.length; i++) {
            console.log("Sample " + i + ": " + frames[i].join(" <- "));
        }
        console.log("\n[*] Done");
    } else {
        setImmediate(check, 500);
    }
});
