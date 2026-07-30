'use strict';

function start(s3e) {
    var yieldWrapper = s3e.base.add(0x209bc);
    var yieldExport = Module.findExportByName("libs3e_android.so", "s3eDeviceYield");

    console.log("s3e at " + s3e.base + " size=0x" + s3e.size.toString(16));
    console.log("yieldWrapper at " + yieldWrapper);
    console.log("s3eDeviceYield at " + yieldExport);

    function labelAddr(addr) {
        if (!addr || addr.isNull()) return "null";
        var m = Process.findModuleByAddress(addr);
        if (m) return m.name + "+0x" + addr.sub(m.base).toString(16);
        var rg = Process.findRangeByAddress(addr);
        if (rg) return "anon+0x" + addr.sub(rg.base).toString(16);
        return addr.toString();
    }

    var outFile = null;
    try { outFile = new File("/data/data/com.nekki.shadowfight/game_code_trace.txt", "w"); } catch(e) {}
    function log(msg) { console.log(msg); if (outFile) { try { outFile.write(msg + "\n"); outFile.flush(); } catch(e) {} } }

    var callCount = 0;

    Interceptor.attach(yieldWrapper, {
        onEnter: function(args) {
            callCount++;
            if (callCount > 5 && (callCount % 100 !== 0)) return;
            if (callCount > 500) return;

            var ctx = this.context;
            var sp = ctx.sp;
            var lr = ctx.lr;

            log("\n=== Yield wrapper call #" + callCount + " ===");
            log("LR(caller)=" + labelAddr(lr));

            // Quick scan for non-s3e executable addresses
            for (var i = 0; i < 512; i++) {
                try {
                    var val = sp.add(i * 4).readPointer();
                    if (val.isNull()) continue;
                    var mod = Process.findModuleByAddress(val);
                    if (mod) {
                        if (mod.name.indexOf("s3e") < 0 && mod.name.indexOf("libc") < 0 && mod.name.indexOf("linker") < 0) {
                            log("  SP+" + ("0x" + (i*4).toString(16)) + " " + mod.name + "+0x" + val.sub(mod.base).toString(16));
                        }
                    } else {
                        // Check anonymous executable ranges
                        var rg = Process.findRangeByAddress(val);
                        if (rg && (rg.protection.indexOf('x') >= 0)) {
                            log("  SP+" + ("0x" + (i*4).toString(16)) + " anon+0x" + val.sub(rg.base).toString(16));
                        }
                    }
                } catch(e) { break; }
            }

            log("Regs: lr=" + labelAddr(lr));
            if (callCount >= 500) log("\n[DONE]");
        }
    });

    log(">> Hooked yield wrapper. Waiting for calls... <<");
}

// Wait for libs3e_android.so to be loaded
var attempts = 0;
function waitForS3E() {
    var mod = Process.findModuleByName("libs3e_android.so");
    if (mod) {
        start(mod);
    } else if (attempts < 60) {
        attempts++;
        setTimeout(waitForS3E, 500);
    } else {
        console.log("ERROR: libs3e_android.so not found after 30 seconds");
    }
}
waitForS3E();
