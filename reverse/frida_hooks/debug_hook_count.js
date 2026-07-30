'use strict';
// Debug: count hook invocations, test file writing
var gameBase = ptr("0x8f197000");
var cnt = 0;
var logMod = 100;
var file = null;

try {
    file = new File("/data/data/com.nekki.shadowfight/debug_test.txt", "w");
    file.write("Starting hook test...\n");
    file.flush();
    
    var addr = gameBase.add(0x0002f0e0);
    Interceptor.attach(addr, {
        onEnter: function(a) {
            this.selfPtr = a[0];
        },
        onLeave: function() {
            cnt++;
            if (cnt % logMod === 0) {
                try {
                    var s = this.selfPtr;
                    file.write("Frame " + cnt + ": self=" + s + "\n");
                    if (s && !s.isNull() && s.compare(ptr(0x1000)) >= 0) {
                        var p = s.add(0x08).readPointer();
                        var e = s.add(0x0C).readPointer();
                        file.write("  player=" + p + " enemy=" + e + "\n");
                        if (p && !p.isNull()) file.write("  player.hp=" + p.add(0x140).readFloat() + "\n");
                        if (e && !e.isNull()) file.write("  enemy.hp=" + e.add(0x140).readFloat() + "\n");
                    }
                    file.flush();
                } catch(e) {
                    file.write("  error: " + e + "\n");
                    file.flush();
                }
            }
        }
    });
    console.log("[OK] Hook installed. Logging every " + logMod + " frames to debug_test.txt");
} catch(e) {
    console.log("[ERR] " + e);
}
