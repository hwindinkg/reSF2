'use strict';

var gameBase = ptr("0x8f197000");
var gameSize = 0x830000;
var gameEnd = gameBase.add(gameSize);

// List of frame-related functions to try
var targets = [
    "eglSwapBuffers",
    "eglSwapInterval",
    "glClear",
    "glFinish",
    "glFlush",
];

var s3eModule = null;
Process.enumerateModules().forEach(function(m) {
    if (m.path.indexOf("libs3e_android") >= 0) s3eModule = m;
});
console.log("s3e base: " + (s3eModule ? s3eModule.base : "NOT_FOUND"));

// Try hooking each target
var outFile = null;
try {
    outFile = new File("/data/data/com.nekki.shadowfight/frame_trace.txt", "w");
    outFile.write("=== Frame function trace ===\n");
    outFile.flush();
} catch(e) { console.log("File error: " + e.message); }

var hooked = 0;
for (var ti = 0; ti < targets.length; ti++) {
    var name = targets[ti];
    try {
        var addr = Module.findExportByName(null, name);
        if (addr) {
            console.log(name + " at " + addr);
            Interceptor.attach(addr, {
                onEnter: function(args) {
                    var bt = null;
                    try { bt = Thread.backtrace(this.context, Backtracer.ACCURATE); } catch(e) {}
                    if (bt) {
                        var line = "\n[" + name + "]\n";
                        for (var j = 0; j < Math.min(bt.length, 16); j++) {
                            var a = bt[j];
                            var tag = "";
                            if (a >= gameBase && a < gameEnd) tag = " (game+" + a.sub(gameBase).toString(16) + ")";
                            else if (s3eModule && a >= s3eModule.base && a < s3eModule.base.add(s3eModule.size)) 
                                tag = " (s3e+" + a.sub(s3eModule.base).toString(16) + ")";
                            line += "  #" + j + ": " + a + tag + "\n";
                        }
                        if (outFile) { try { outFile.write(line); outFile.flush(); } catch(e) {} }
                    }
                }
            });
            hooked++;
            if (hooked >= 2) break; // Hook at most 2
        } else {
            console.log(name + ": NOT FOUND");
        }
    } catch(e) {
        console.log(name + ": ERROR - " + e.message);
    }
}

console.log("Hooked " + hooked + " functions. Writing to frame_trace.txt");
