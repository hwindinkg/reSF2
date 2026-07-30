'use strict';

var yieldAddr = Module.findExportByName("libs3e_android.so", "s3eDeviceYield");
console.log("[*] s3eDeviceYield at " + yieldAddr);

var gameStart = ptr(0);
var gameEnd = ptr(0);
Process.enumerateModules().forEach(function(m) {
    if (m.name.indexOf("com.nekki") >= 0) {
        gameStart = m.base;
        gameEnd = m.base.add(m.size);
        console.log("[*] Game module: " + m.base + " - " + gameEnd);
    }
});
if (gameStart.isNull()) console.log("[-] Game module not found!");

var count = 0;

Interceptor.attach(yieldAddr, {
    onEnter: function(args) {
        count++;
        console.log("---[" + count + "]---");
        console.log("PC=" + this.context.pc + " LR=" + this.context.lr + 
            " SP=" + this.context.sp + " R7=" + (this.context.r7||0));
        console.log("R0-R3: " + this.context.r0 + " " + this.context.r1 + 
            " " + this.context.r2 + " " + this.context.r3);
        console.log("R4=" + (this.context.r4||0) + " R5=" + (this.context.r5||0) + 
            " R6=" + (this.context.r6||0));
        
        // Try to find game code addresses in the backtrace
        try {
            var bt = Thread.backtrace(this.context, Backtracer.ACCURATE);
            console.log("Backtrace depth: " + bt.length);
            for (var i = 0; i < bt.length; i++) {
                var a = bt[i];
                var tag = "";
                if (!gameStart.isNull() && a >= gameStart && a < gameEnd)
                    tag = " GAME+" + a.sub(gameStart).toString(16);
                console.log("  #" + i + ": " + a + tag);
            }
        } catch(e) {
            console.log("Backtrace failed: " + e.message);
        }
        
        // Manual stack walk: read saved LR from stack
        try {
            var sp = this.context.sp;
            console.log("Stack dump (first 16 words):");
            for (var i = 0; i < 16; i++) {
                var val = sp.add(i * 4).readPointer();
                var tag = "";
                if (!gameStart.isNull() && val >= gameStart && val < gameEnd)
                    tag = " GAME+" + val.sub(gameStart).toString(16);
                if (tag) console.log("  SP+" + (i*4) + ": " + val + tag);
            }
        } catch(e) {
            console.log("Stack walk failed: " + e.message);
        }
        
        if (count >= 5) {
            console.log("=== 5 samples collected ===");
        }
    }
});

console.log("[*] Hook set. Waiting for calls...");
