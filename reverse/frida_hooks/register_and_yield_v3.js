'use strict';

/*
 * register_and_yield_v3.js
 *
 * Hooks s3eDeviceRegister + s3eDeviceYield to trace the FULL call chain.
 * Detects game code region dynamically (anonymous large executable mapping).
 */

function waitForLib(name, callback, retries) {
    if (retries <= 0) {
        console.log("FAILED: " + name + " not found after timeout");
        return;
    }
    var mod = Process.findModuleByName(name);
    if (!mod) {
        setTimeout(function() { waitForLib(name, callback, retries - 1); }, 300);
        return;
    }
    console.log("[" + name + "] loaded at " + mod.base + " (size 0x" + mod.size.toString(16) + ")");
    callback(mod);
}

function labelAddress(addr, gameRanges) {
    if (!addr || addr.isNull()) return "null";
    // Check game ranges first
    if (gameRanges) {
        for (var i = 0; i < gameRanges.length; i++) {
            var r = gameRanges[i];
            if (addr >= r.base && addr < r.base.add(r.size)) {
                return "game+" + addr.sub(r.base).toString(16);
            }
        }
    }
    // Check known modules
    var mod = Process.findModuleByAddress(addr);
    if (mod) {
        return mod.name + "+0x" + addr.sub(mod.base).toString(16);
    }
    return addr.toString();
}

function scanStack(ctx, gameRanges, depth) {
    if (!depth) depth = 96;
    var sp = ctx.sp;
    var results = [];
    for (var i = 0; i < depth; i++) {
        var addr = sp.add(i * 4);
        try {
            var val = addr.readPointer();
            if (!val || val.isNull()) continue;
            var label = labelAddress(val, gameRanges);
            // Only include if we can label it (not just raw pointer to unknown)
            if (label !== val.toString()) {
                results.push({offset: i * 4, addr: val, label: label});
            }
        } catch(e) {
            break;
        }
    }
    return results;
}

// ─── Main setup ───
function setupHooks(s3eMod) {
    var registerAddr = Module.findExportByName(s3eMod.name, "s3eDeviceRegister");
    var yieldAddr = Module.findExportByName(s3eMod.name, "s3eDeviceYield");

    if (registerAddr) console.log("s3eDeviceRegister at " + registerAddr);
    if (yieldAddr) console.log("s3eDeviceYield at " + yieldAddr);

    // ─── Detect game code region ───
    // The game binary is loaded by libs3e_android into an anonymous mapping
    // It should be a large RWX or R-X region with no backing file or /dev/zero
    var gameRanges = [];
    
    function findGameRegion() {
        gameRanges = [];
        var ranges = Process.enumerateRanges('--x');
        for (var ri = 0; ri < ranges.length; ri++) {
            var r = ranges[ri];
            if (!r.file || r.file.path.indexOf('zero') >= 0) {
                // Anonymous or /dev/zero mapping with execute
                if (r.size > 0x400000 && r.size < 0x2000000) {
                    gameRanges.push({base: r.base, size: r.size});
                    console.log("Game region: " + r.base + " size=0x" + r.size.toString(16) + " prot=" + r.protection + " " + (r.file ? r.file.path : "anonymous"));
                }
            }
        }
        // Fallback: look in /data/app for S3E extension libraries
        if (gameRanges.length === 0) {
            var ranges2 = Process.enumerateRanges('--x');
            for (var ri2 = 0; ri2 < ranges2.length; ri2++) {
                var r2 = ranges2[ri2];
                if (r2.file && r2.file.path && r2.file.path.indexOf("com.nekki.shadowfight") >= 0) {
                    gameRanges.push({base: r2.base, size: r2.size});
                    console.log("Game region (fallback): " + r2.base + " size=0x" + r2.size.toString(16) + " " + r2.file.path);
                }
            }
        }
        console.log("Found " + gameRanges.length + " game region(s)");
        return gameRanges.length > 0;
    }

    if (!findGameRegion()) {
        console.log("Game region not found, will poll...");
        var pollTimer = setInterval(function() {
            if (findGameRegion()) clearInterval(pollTimer);
        }, 500);
    }

    var outFile = null;
    try {
        outFile = new File("/data/data/com.nekki.shadowfight/call_chain_v3.txt", "w");
        outFile.write("=== Call Chain Trace v3 ===\nTime: " + Date.now() + "\n\n");
        outFile.flush();
    } catch(e) {
        console.log("FILE OPEN FAILED (non-fatal): " + e.message);
    }

    function log(msg) {
        console.log(msg);
        if (outFile) {
            try { outFile.write(msg + "\n"); outFile.flush(); } catch(e) {}
        }
    }

    var regCount = 0;
    var yieldCount = 0;
    var maxCalls = 30;
    var firstRegCallback = null;

    // ─── Hook s3eDeviceRegister ───
    if (registerAddr) {
        Interceptor.attach(registerAddr, {
            onEnter: function(args) {
                regCount++;
                if (regCount > maxCalls) return;

                var type = args[0].toInt32();
                if (type !== 0) return; // Only S3E_DEVICE_CALLBACK

                var callback = args[1];
                var userData = args[2];
                var ctx = this.context;
                var lr = ctx.lr;
                var stack = scanStack(ctx, gameRanges, 96);

                var msg = "\n=== s3eDeviceRegister #" + regCount + " type=" + type + " ===";
                msg += "\nCallback: " + callback + " (" + labelAddress(callback, gameRanges) + ")";
                msg += "\nUserData: " + userData;
                msg += "\nLR: " + lr + " (" + labelAddress(lr, gameRanges) + ")";
                for (var si = 0; si < stack.length; si++) {
                    msg += "\n  " + stack[si].label;
                }
                log(msg);

                // Save first registered callback for comparison
                if (firstRegCallback === null) {
                    firstRegCallback = callback;
                }

                if (regCount >= maxCalls) log("\n[DONE] s3eDeviceRegister: reached " + maxCalls);
            }
        });
        log("s3eDeviceRegister hooked");
    }

    // ─── Hook s3eDeviceYield ───
    if (yieldAddr) {
        Interceptor.attach(yieldAddr, {
            onEnter: function(args) {
                yieldCount++;
                if (yieldCount > maxCalls) return;

                var ctx = this.context;
                var lr = ctx.lr;
                var stack = scanStack(ctx, gameRanges, 128);

                var msg = "\n=== s3eDeviceYield #" + yieldCount + " ===";
                msg += "\nLR: " + lr + " (" + labelAddress(lr, gameRanges) + ")";
                msg += "\nSP: " + ctx.sp;
                msg += "\nFP: " + ctx.r11;  // ARM frame pointer
                msg += "\nStack (labelled entries):";
                for (var si = 0; si < stack.length; si++) {
                    msg += "\n  SP+" + ("0x" + stack[si].offset.toString(16)) + " " + stack[si].label;
                }
                // Log first 4 registers too for context
                msg += "\nR0-R3: " + ctx.r0 + " " + ctx.r1 + " " + ctx.r2 + " " + ctx.r3;
                log(msg);

                if (yieldCount >= maxCalls) {
                    log("\n[DONE] s3eDeviceYield: reached " + maxCalls);
                    if (outFile) try { outFile.close(); } catch(e) {}
                }
            }
        });
        log("s3eDeviceYield hooked");
    }

    // ─── Also dump all game code symbols if we found the region ───
    if (gameRanges.length > 0) {
        log("\n=== Starting trace (enter a battle to see calls) ===");
    }
}

// ─── Start ───
var libsToTry = ["libs3e_android.so", "libs3e.so", "libmarmalade.so"];

function tryNextLib(idx) {
    if (idx >= libsToTry.length) {
        console.log("FATAL: None of the expected S3E libraries found");
        return;
    }
    waitForLib(libsToTry[idx], function(mod) {
        setupHooks(mod);
    }, 40);
}

tryNextLib(0);
