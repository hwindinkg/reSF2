'use strict';

var yieldAddr = Module.findExportByName("libs3e_android.so", "s3eDeviceYield");
if (!yieldAddr) {
    console.log("[-] s3eDeviceYield NOT found");
    Process.exit(1);
}
console.log("[*] s3eDeviceYield at " + yieldAddr);

var gameModule = null;
var s3eModule = null;
Process.enumerateModules().forEach(function(m) {
    if (m.name.indexOf("com.nekki") >= 0) gameModule = m;
    if (m.name.indexOf("libs3e_android") >= 0) s3eModule = m;
});

if (!s3eModule) {
    // Find by path
    Process.enumerateModules().forEach(function(m) {
        if (m.path.indexOf("libs3e_android") >= 0) s3eModule = m;
    });
}
console.log("[*] s3e base: " + (s3eModule ? s3eModule.base + " size:" + s3eModule.size : "NOT FOUND"));
if (gameModule) console.log("[*] game base: " + gameModule.base + " size:" + gameModule.size);

var count = 0;
var MAX_SAMPLES = 10;

Interceptor.attach(yieldAddr, {
    onEnter: function(args) {
        count++;
        if (count > MAX_SAMPLES) return;
        console.log("[" + count + "] s3eDeviceYield RA=" + this.returnAddress);

        try {
            var bt = Thread.backtrace(this.context, Backtracer.ACCURATE);
            console.log("    backtrace depth: " + bt.length);
            for (var j = 0; j < Math.min(bt.length, 12); j++) {
                var addr = bt[j];
                var info = "    #" + j + ": " + addr;
                if (gameModule) {
                    var offset = addr.sub(gameModule.base);
                    if (offset > 0 && offset < gameModule.size) {
                        info += " (game+" + offset.toString(16) + ")";
                    }
                }
                if (s3eModule) {
                    var s3eOffset = addr.sub(s3eModule.base);
                    if (s3eOffset > 0 && s3eOffset < s3eModule.size) {
                        info += " (s3e+" + s3eOffset.toString(16) + ")";
                    }
                }
                console.log(info);
            }
        } catch (e) {
            console.log("    BACKTRACE ERROR: " + e.message);
            console.log("    context PC=" + this.context.pc + " LR=" + this.context.lr);
        }

        if (count >= MAX_SAMPLES) {
            console.log("=== ENOUGH SAMPLES ===");
        }
    }
});

console.log("[*] Hooked. Sampling first " + MAX_SAMPLES + " calls...");
