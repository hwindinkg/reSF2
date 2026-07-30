'use strict';

/*
 * register_capture_spawn.js
 * Spawn the game and capture s3eDeviceRegister calls at startup.
 * This catches the callback registration chain that the S3E loader does.
 * Uses waitForS3E to handle the timing issue.
 */

function labelAddr(addr) {
    if (!addr || addr.isNull()) return "null";
    var m = Process.findModuleByAddress(addr);
    if (m) return m.name + "+0x" + addr.sub(m.base).toString(16);
    var rg = Process.findRangeByAddress(addr);
    if (rg && rg.file === null) return "anon_rwx+0x" + addr.sub(rg.base).toString(16);
    if (rg) return rg.base.toString() + "+0x" + addr.sub(rg.base).toString(16);
    return addr.toString();
}

var outFile = null;
try { outFile = new File("/data/data/com.nekki.shadowfight/register_spawn.txt", "w"); } catch(e) {}
function log(msg) { console.log(msg); if (outFile) { try { outFile.write(msg + "\n"); outFile.flush(); } catch(e) {} } }

function start(s3e) {
    log("s3e at " + s3e.base + " size=0x" + s3e.size.toString(16));
    
    var registerAddr = Module.findExportByName("libs3e_android.so", "s3eDeviceRegister");
    log("s3eDeviceRegister at " + labelAddr(registerAddr));
    
    // Also hook s3eDeviceYield for comparison
    var yieldAddr = Module.findExportByName("libs3e_android.so", "s3eDeviceYield");
    log("s3eDeviceYield at " + labelAddr(yieldAddr));
    
    var regCount = 0;
    var yieldCount = 0;
    
    // Hook register - this is what we really want
    if (registerAddr) {
        Interceptor.attach(registerAddr, {
            onEnter: function(args) {
                regCount++;
                var type = args[0].toInt32();
                if (type !== 0) return; // Only S3E_DEVICE_CALLBACK
                
                var callback = args[1];
                var userData = args[2];
                var ctx = this.context;
                var lr = ctx.lr;
                var sp = ctx.sp;
                
                log("\n=== s3eDeviceRegister #" + regCount + " type=" + type + " ===");
                log("Callback: " + callback + " (" + labelAddr(callback) + ")");
                log("UserData: " + userData);
                log("LR: " + labelAddr(lr));
                
                // Deep stack scan
                for (var i = 0; i < 256; i++) {
                    try {
                        var val = sp.add(i * 4).readPointer();
                        if (val.isNull()) continue;
                        var label = labelAddr(val);
                        if (label !== val.toString()) {
                            log("  SP+" + ("0x" + (i*4).toString(16)) + " " + label);
                        }
                    } catch(e) { break; }
                }
                
                // First 5 register calls should catch the important ones
                if (regCount >= 5) {
                    log("\n[DONE] captured 5 register calls");
                    Interceptor.detachAll();
                }
            }
        });
        log("s3eDeviceRegister hooked");
    }
    
    // Also hook yield to see if it fires after register
    if (yieldAddr) {
        Interceptor.attach(yieldAddr, {
            onEnter: function(args) {
                yieldCount++;
                if (yieldCount > 3) return;
                var ctx = this.context;
                log("\n=== s3eDeviceYield #" + yieldCount + " ===");
                log("LR: " + labelAddr(ctx.lr));
                var sp = ctx.sp;
                for (var i = 0; i < 64; i++) {
                    try {
                        var val = sp.add(i * 4).readPointer();
                        if (val.isNull()) continue;
                        var label = labelAddr(val);
                        if (label !== val.toString()) {
                            log("  SP+" + ("0x" + (i*4).toString(16)) + " " + label);
                        }
                    } catch(e) { break; }
                }
            }
        });
        log("s3eDeviceYield hooked");
    }
    
    log("\n>> Both hooks installed, waiting for calls... <<");
    log("(Resuming process if spawned...)");
}

// Wait for module
var attempts = 0;
function waitForS3E() {
    var mod = Process.findModuleByName("libs3e_android.so");
    if (mod) {
        start(mod);
    } else if (attempts < 120) {
        attempts++;
        setTimeout(waitForS3E, 500);
    } else {
        console.log("ERROR: libs3e_android.so not found after 60 seconds");
    }
}
waitForS3E();
