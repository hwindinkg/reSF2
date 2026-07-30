'use strict';

/*
 * register_and_yield_v2.js
 *
 * Hooks s3eDeviceRegister + s3eDeviceYield to trace the FULL call chain.
 * Uses manual stack scanning (more reliable than Thread.backtrace)
 * and continuously polls for the game executable region.
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

function getAddrInfo(addr, gameBase, gameSize, s3eMod) {
    if (!addr) return "null";
    // Game code region
    if (gameBase && addr >= gameBase && addr < gameBase.add(gameSize)) {
        return "game+" + addr.sub(gameBase).toString(16);
    }
    // S3E library
    if (addr >= s3eMod.base && addr < s3eMod.base.add(s3eMod.size)) {
        return "s3e+" + addr.sub(s3eMod.base).toString(16);
    }
    // Other known modules
    var mod = Process.findModuleByAddress(addr);
    if (mod) {
        return mod.name + "+0x" + addr.sub(mod.base).toString(16);
    }
    return addr.toString();
}

function scanStackForGameCode(ctx, gameBase, gameSize, s3eMod, depth) {
    if (!depth) depth = 64;
    var sp = ctx.sp;
    var results = [];
    for (var i = 0; i < depth; i++) {
        var addr = sp.add(i * 4);
        try {
            var val = addr.readPointer();
            if (!val) continue;
            // Check if this address looks like return address in valid code
            var info = getAddrInfo(val, gameBase, gameSize, s3eMod);
            // Include all known addresses, not just game
            if (info !== val.toString()) {
                results.push({offset: i * 4, addr: val, info: info});
            }
        } catch(e) {
            break; // invalid memory
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

    // Game region detection — poll continuously until found
    var gameBase = null;
    var gameSize = 0;
    
    function findGameRegion() {
        // Look for the game's executable region.
        // The game process has the S3E .so + the game executable mapped separately
        // The game exe is typically named after the APK or has no file mapping
        var ranges = Process.enumerateRanges('r--');
        for (var ri = 0; ri < ranges.length; ri++) {
            var r = ranges[ri];
            if (!r.file) {
                // Anonymous mapping — could be game code
                if (r.protection.indexOf('x') >= 0) {
                    // Has execute permission, check size
                    if (r.size > 0x100000 && r.size < 0x2000000) {
                        gameBase = r.base;
                        gameSize = r.size;
                        console.log("Detected game executable: " + r.base + " size=0x" + r.size.toString(16));
                        return true;
                    }
                }
            }
        }
        // Fallback: look for the game in /data/app
        for (var ri2 = 0; ri2 < ranges.length; ri2++) {
            var r2 = ranges[ri2];
            if (r2.file && r2.file.path && r2.file.path.indexOf("com.nekki.shadowfight") >= 0
                && r2.protection.indexOf('x') >= 0) {
                gameBase = r2.base;
                gameSize = r2.size;
                console.log("Detected game region: " + r2.base + " (" + r2.file.path + ")");
                return true;
            }
        }
        return false;
    }

    // Try immediately, then poll
    if (!findGameRegion()) {
        console.log("Game region not found yet, will poll every 500ms...");
        var pollTimer = setInterval(function() {
            if (findGameRegion()) {
                clearInterval(pollTimer);
                console.log("Game region found!");
            }
        }, 500);
    }

    var outFile = null;
    try {
        outFile = new File("/data/data/com.nekki.shadowfight/call_chain_v2.txt", "w");
        outFile.write("=== Call Chain Trace v2 ===\nTime: " + Date.now() + "\n\n");
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

    // ─── Hook s3eDeviceRegister ───
    if (registerAddr) {
        Interceptor.attach(registerAddr, {
            onEnter: function(args) {
                regCount++;
                if (regCount > maxCalls) return;
                
                var type = args[0].toInt32();
                var callback = args[1];
                var userData = args[2];

                if (type !== 0) return; // Only S3E_DEVICE_CALLBACK

                var ctx = this.context;
                var lr = ctx.lr;
                var stackInfo = scanStackForGameCode(ctx, gameBase, gameSize, s3eMod, 96);

                var msg = "\n=== s3eDeviceRegister #" + regCount + " ===";
                msg += "\nType: " + type;
                msg += "\nCallback: " + callback + " (" + getAddrInfo(callback, gameBase, gameSize, s3eMod) + ")";
                msg += "\nUserData: " + userData + " (" + getAddrInfo(userData, gameBase, gameSize, s3eMod) + ")";
                msg += "\nLR: " + lr + " (" + getAddrInfo(lr, gameBase, gameSize, s3eMod) + ")";
                msg += "\nSP: " + ctx.sp;
                msg += "\nStack (game+s3e entries only):";
                for (var si = 0; si < stackInfo.length; si++) {
                    msg += "\n  SP+" + stackInfo[si].offset + " -> " + stackInfo[si].info;
                }
                log(msg);

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
                var stackInfo = scanStackForGameCode(ctx, gameBase, gameSize, s3eMod, 128);

                var msg = "\n=== s3eDeviceYield #" + yieldCount + " ===";
                msg += "\nLR: " + lr + " (" + getAddrInfo(lr, gameBase, gameSize, s3eMod) + ")";
                msg += "\nSP: " + ctx.sp;
                msg += "\nStack (game+s3e entries only):";
                for (var si = 0; si < stackInfo.length; si++) {
                    msg += "\n  SP+" + stackInfo[si].offset + " -> " + stackInfo[si].info;
                }
                log(msg);

                if (yieldCount >= maxCalls) {
                    log("\n[DONE] s3eDeviceYield: reached " + maxCalls);
                    if (outFile) try { outFile.close(); } catch(e) {}
                }
            }
        });
        log("s3eDeviceYield hooked");
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
