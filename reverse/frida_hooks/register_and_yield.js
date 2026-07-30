'use strict';

/*
 * register_and_yield.js
 * 
 * Hooks s3eDeviceRegister + s3eDeviceYield to trace:
 *  - Who registers the S3E_DEVICE_CALLBACK frame callback
 *  - The full call chain from game entry → frame update → s3eDeviceYield
 * 
 * Usage:
 *   frida -U com.nekki.shadowfight -l register_and_yield.js --no-pause
 */

function waitForLib(name, callback, retries) {
    if (retries <= 0) {
        console.log("FAILED: " + name + " not found after " + retries + " retries");
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

function getCallerName(addr) {
    // Try to get function name from debug symbols (only if available)
    try {
        var sym = DebugSymbol.getFunctionName(addr);
        if (sym) return sym;
    } catch(e) {}
    
    // Try to find module info
    var mod = Process.findRangeByAddress(addr);
    if (mod) {
        return mod.pathname + "+0x" + addr.sub(mod.base).toString(16);
    }
    return addr.toString();
}

function dumpBacktrace(limit) {
    if (limit === undefined) limit = 16;
    var bt = Thread.backtrace(this.context, Backtracer.ACCURATE);
    var lines = [];
    for (var i = 0; i < bt.length && i < limit; i++) {
        var addr = bt[i];
        var name = getCallerName(addr);
        lines.push("  #" + i + " 0x" + addr.toString(16) + " " + name);
    }
    return lines.join("\n");
}

function setupHooks(s3eMod) {
    // ─── Find exports ───
    var registerAddr = Module.findExportByName(s3eMod.name, "s3eDeviceRegister");
    var yieldAddr = Module.findExportByName(s3eMod.name, "s3eDeviceYield");
    
    if (!registerAddr) {
        console.log("FAILED: s3eDeviceRegister export not found in " + s3eMod.name);
        // Try alternative lib names (use index loop for ES5 compat)
        var altMods = ["libs3e.so", "libmarmalade.so", "libs3e_android.so"];
        for (var mi = 0; mi < altMods.length; mi++) {
            registerAddr = Module.findExportByName(altMods[mi], "s3eDeviceRegister");
            if (registerAddr) break;
        }
    }
    if (!yieldAddr) {
        console.log("FAILED: s3eDeviceYield export not found");
        yieldAddr = Module.findExportByName(s3eMod.name, "s3eDeviceYield");
    }
    
    if (registerAddr) console.log("s3eDeviceRegister at " + registerAddr);
    if (yieldAddr) console.log("s3eDeviceYield at " + yieldAddr);
    
    // ─── Game code region detection ───
    // The game region is typically the S3E executable loaded in memory
    // On ARM Android, the base is around 0x8F197000 based on prior captures
    // We detect it automatically by looking at the first callers of s3eDeviceRegister
    var gameBase = null;
    var gameSize = 0x0;
    
    function setGameRegion(addr) {
        if (gameBase) return; // already set
        var mod = Process.findRangeByAddress(addr);
        if (mod) {
            var filePath = mod.file ? mod.file.path : null;
            if (filePath && filePath.indexOf("s3e") < 0 && filePath.indexOf("lib") < 0) {
                gameBase = mod.base;
                gameSize = mod.size;
                console.log("Detected game region: " + gameBase + " - " + gameBase.add(gameSize) + " (" + filePath + ")");
            }
        }
    }
    
    var outFile = null;
    try {
        outFile = new File("/data/data/com.nekki.shadowfight/call_chain.txt", "w");
        outFile.write("=== s3eDeviceRegister + Yield Call Chain ===\n");
        outFile.write("Time: " + Date.now() + "\n\n");
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
    var maxRegCalls = 30;
    var maxYieldCalls = 30;
    
    // ─── Hook s3eDeviceRegister ───
    if (registerAddr) {
        Interceptor.attach(registerAddr, {
            onEnter: function(args) {
                regCount++;
                if (regCount > maxRegCalls) return;
                
                // s3eDeviceRegister(int32 type, s3eCallback callback, void* userData)
                var type = args[0].toInt32();
                var callback = args[1];
                var userData = args[2];
                
                if (type !== 0) return; // Only S3E_DEVICE_CALLBACK
                
                var lr = this.context.lr;
                setGameRegion(lr);
                
                var msg = "\n────────── s3eDeviceRegister #" + regCount + " ──────────";
                msg += "\nType: " + type;
                msg += "\nCallback: " + callback;
                if (gameBase && callback >= gameBase && callback < gameBase.add(gameSize)) {
                    msg += " (game+" + callback.sub(gameBase).toString(16) + ")";
                }
                msg += "\nUserData: " + userData;
                msg += "\nLR: " + lr + " (" + getCallerName(lr) + ")";
                msg += "\n── Backtrace ──\n";
                msg += dumpBacktrace.call(this, 20);
                msg += "\n";
                
                log(msg);
                
                if (regCount >= maxRegCalls) {
                    log("\n[DONE] Reached " + maxRegCalls + " s3eDeviceRegister calls.");
                }
            }
        });
        log("s3eDeviceRegister hooked (max " + maxRegCalls + " calls)");
    }
    
    // ─── Hook s3eDeviceYield ───
    if (yieldAddr) {
        Interceptor.attach(yieldAddr, {
            onEnter: function(args) {
                yieldCount++;
                if (yieldCount > maxYieldCalls) return;
                
                // s3eDeviceYield() takes no arguments
                var lr = this.context.lr;
                var sp = this.context.sp;
                setGameRegion(lr);
                
                var msg = "\n────────── s3eDeviceYield #" + yieldCount + " ──────────";
                msg += "\nLR: " + lr + " (" + getCallerName(lr) + ")";
                msg += "\nSP: " + sp;
                if (gameBase && lr >= gameBase && lr < gameBase.add(gameSize)) {
                    msg += " [in game region]";
                }
                msg += "\n── Backtrace ──\n";
                msg += dumpBacktrace.call(this, 24);
                msg += "\n";
                
                log(msg);
                
                if (yieldCount >= maxYieldCalls) {
                    log("\n[DONE] Reached " + maxYieldCalls + " s3eDeviceYield calls.");
                    if (outFile) try { outFile.close(); } catch(e) {}
                }
            }
        });
        log("s3eDeviceYield hooked (max " + maxYieldCalls + " calls)");
    }
    
    if (!registerAddr && !yieldAddr) {
        log("FATAL: Neither s3eDeviceRegister nor s3eDeviceYield found");
    }
}

// ─── Wait for library ───
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
