'use strict';

function waitForLib(retries) {
    if (retries <= 0) { console.log("FAILED: libs3e_android not found"); return; }
    var mod = Process.findModuleByName("libs3e_android.so");
    if (!mod) {
        setTimeout(function() { waitForLib(retries - 1); }, 200);
        return;
    }
    console.log("libs3e_android loaded at " + mod.base);
    setupHooks(mod);
}

function setupHooks(s3eMod) {
    var yieldAddr = Module.findExportByName("libs3e_android.so", "s3eDeviceYield");
    if (!yieldAddr) { console.log("FAILED: s3eDeviceYield export not found"); return; }
    console.log("s3eDeviceYield at " + yieldAddr);

    var gameBase = ptr("0x8f197000");
    var gameSize = 0x830000;

    // Try to open file; may fail if dir not writable
    var outFile = null;
    try {
        outFile = new File("/data/data/com.nekki.shadowfight/yield_trace.txt", "w");
        outFile.write("=== s3eDeviceYield Trace ===\n");
        outFile.write("Spawned fresh, time: " + Date.now() + "\n");
        outFile.write("Game region: " + gameBase + " - " + gameBase.add(gameSize) + "\n");
        outFile.flush();
    } catch(e) {
        console.log("FILE OPEN FAILED: " + e.message);
    }

    var count = 0;

    Interceptor.attach(yieldAddr, {
        onEnter: function(args) {
            count++;
            var lr = this.context.lr;
            var sp = this.context.sp;
            var line = "\n[" + count + "] LR=" + lr + " SP=" + sp + "\n";

            if (lr >= gameBase && lr < gameBase.add(gameSize)) {
                line += ">>> LR IS GAME CODE: game+" + lr.sub(gameBase).toString(16) + "\n";
            } else if (lr >= s3eMod.base && lr < s3eMod.base.add(s3eMod.size)) {
                line += ">>> LR is s3e+" + lr.sub(s3eMod.base).toString(16) + "\n";
            }

            line += "Stack:\n";
            try {
                var found = 0;
                for (var i = 0; i < 128 && found < 15; i++) {
                    var val = sp.add(i * 4).readPointer();
                    if (val >= gameBase && val < gameBase.add(gameSize)) {
                        line += "  SP+" + (i*4) + " -> game+" + val.sub(gameBase).toString(16) + "\n";
                        found++;
                    } else if (val >= s3eMod.base && val < s3eMod.base.add(s3eMod.size)) {
                        var off = val.sub(s3eMod.base).toInt32();
                        if (off > 0x1000) {
                            line += "  SP+" + (i*4) + " -> s3e+" + (off).toString(16) + "\n";
                            found++;
                        }
                    }
                }
                if (found === 0) line += "  (none in 128 words)\n";
            } catch(e) {
                line += "  Error: " + e.message + "\n";
            }

            if (outFile) {
                try { outFile.write(line); outFile.flush(); } catch(e) {}
            }

            if (count >= 15) {
                if (outFile) {
                    try { outFile.write("\n=== DONE - 15 samples ===\n"); outFile.close(); } catch(e) {}
                }
                console.log("DONE: 15 samples written");
            }
        }
    });

    console.log("Hooked. Writing to yield_trace.txt. Enter a battle!");
}

// Start waiting for libs3e_android (try 50 times = ~10 seconds)
waitForLib(50);
