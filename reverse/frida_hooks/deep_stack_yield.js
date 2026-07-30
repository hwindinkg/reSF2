'use strict';

/*
 * Simplified deep stack yield scanner.
 * Hooks s3eDeviceYield and dumps SP values with module labels.
 */

var s3e = Process.findModuleByName("libs3e_android.so");
var yieldAddr = Module.findExportByName("libs3e_android.so", "s3eDeviceYield");
console.log("s3e at " + s3e.base + " size=0x" + s3e.size.toString(16));
console.log("yield at " + yieldAddr + " (s3e+" + yieldAddr.sub(s3e.base).toString(16) + ")");

function labelAddr(addr) {
    if (!addr || addr.isNull()) return "null";
    var m = Process.findModuleByAddress(addr);
    if (m) return m.name + "+0x" + addr.sub(m.base).toString(16);
    // Check if it's in the game region (anonymous executable)
    var rg = Process.findRangeByAddress(addr);
    if (rg && rg.file === null && (rg.protection.indexOf('x') >= 0))
        return "anon_rwx+0x" + addr.sub(rg.base).toString(16);
    if (rg) return rg.base + "/" + (rg.file ? rg.file.path : "anon") + "+0x" + addr.sub(rg.base).toString(16);
    return addr.toString();
}

var outFile = null;
try { outFile = new File("/data/data/com.nekki.shadowfight/deep_stack.txt", "w"); } catch(e) {}
function log(msg) { console.log(msg); if (outFile) { try { outFile.write(msg + "\n"); outFile.flush(); } catch(e) {} } }

var callCount = 0;
Interceptor.attach(yieldAddr, {
    onEnter: function(args) {
        callCount++;
        if (callCount > 15) return;
        
        var ctx = this.context;
        var sp = ctx.sp;
        var lr = ctx.lr;
        
        log("\n=== s3eDeviceYield #" + callCount + " ===");
        log("LR=" + lr + " (" + labelAddr(lr) + ")");
        log("SP=" + sp + " FP(R11)=" + ctx.r11);
        
        // ARM frame chain walk
        log("\nARM frame chain (FP-based):");
        var fp = ctx.r11;
        for (var fi = 0; fi < 10; fi++) {
            try {
                if (fp.isNull()) break;
                var next_fp = fp.readPointer();
                var ret_addr = fp.add(4).readPointer();
                log("  Frame #" + fi + ": saveFP=" + next_fp + " ret=" + labelAddr(ret_addr));
                if (next_fp.equals(fp) || next_fp.compare(fp) <= 0 || next_fp.compare(sp.add(0x500)) > 0) break;
                fp = next_fp;
            } catch(e) { log("  Frame walk stopped: " + e.message); break; }
        }
        
        // Deep SP scan - only show executable addresses
        log("\nExecutable return addresses on stack:");
        for (var i = 0; i < 256; i++) {
            try {
                var val = sp.add(i * 4).readPointer();
                if (val.isNull()) continue;
                var m = Process.findModuleByAddress(val);
                var isExec = m && (m.permissions.indexOf('x') >= 0);
                if (!isExec) {
                    var rg = Process.findRangeByAddress(val);
                    if (rg && (rg.protection.indexOf('x') >= 0)) isExec = true;
                }
                if (isExec) {
                    log("  SP+" + ("0x" + (i*4).toString(16)) + " " + labelAddr(val));
                }
            } catch(e) { break; }
        }
        
        // Additionally scan for ANY value pointing to a module with 'shadowfight' or 's3e'
        log("\nAll values pointing to game/S3E modules:");
        for (var i = 0; i < 256; i++) {
            try {
                var val = sp.add(i * 4).readPointer();
                if (val.isNull()) continue;
                var m = Process.findModuleByAddress(val);
                if (m && (m.name.indexOf("shadowfight") >= 0 || m.name.indexOf("s3e") >= 0))
                    log("  SP+" + ("0x" + (i*4).toString(16)) + " " + labelAddr(val));
            } catch(e) { break; }
        }
        
        log("\nRegisters: pc=" + ctx.pc + " lr=" + ctx.lr + " r0=" + ctx.r0 + " r1=" + ctx.r1 + " r2=" + ctx.r2 + " r3=" + ctx.r3);
        
        if (callCount >= 15) log("\n[DONE] captured 15 calls");
    }
});

log(">> Hooked. Enter a battle now <<");
