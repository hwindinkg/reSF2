#!/usr/bin/env python3
"""Attach to the RUNNING game, hook s3ePointerUpdate, then inject taps to see if input registers."""
import frida, sys, time, subprocess

DEV = "684006127d29"
PKG = "com.nekki.shadowfight"

code = r"""
'use strict';
function log(tag, m) { send({ tag: tag, t: Date.now() % 100000, m: m }); }
log('INIT', 'attached');
var cnt = 0;
try {
    var a = Module.findExportByName('libs3e_android.so', 's3ePointerUpdate');
    Interceptor.attach(a, { onEnter: function () {
        cnt++;
        if (cnt <= 8 || cnt % 60 === 0) {
            try {
                var tx = Module.findExportByName('libs3e_android.so', 's3ePointerGetTouchX');
                var ty = Module.findExportByName('libs3e_android.so', 's3ePointerGetTouchY');
                log('POINTER', 'update #' + cnt);
            } catch (e) { log('POINTER', 'update #' + cnt + ' err ' + e); }
        }
    }});
    log('HOOK', 's3ePointerUpdate hooked @ ' + a);
} catch (e) { log('ERR', String(e)); }
setInterval(function () { log('STATUS', 'pointer-calls=' + cnt); }, 4000);
"""

def main():
    dev = frida.get_device(DEV, timeout=10)
    pid = int(dev.enumerate_processes().get if False else subprocess.run(
        ["adb", "shell", "pidof", PKG], capture_output=True, text=True).stdout.strip())
    print("attaching to pid", pid)
    sess = dev.attach(pid)
    n = [0]
    def on_msg(m, d):
        if m["type"] == "send":
            n[0] += 1
            print("[%d] %s %s" % (n[0], m["payload"]["tag"], m["payload"]["m"][:110]))
    sc = sess.create_script(code)
    sc.on("message", on_msg)
    sc.load()
    time.sleep(2)
    # inject taps
    for (x, y) in [(900, 520), (950, 520), (432, 240), (720, 360)]:
        subprocess.run(["adb", "shell", "input tap %d %d" % (x, y)], timeout=10)
        print("tapped", x, y)
        time.sleep(2.5)
    t0 = time.time()
    while time.time() - t0 < 6:
        time.sleep(0.5)
    print("total messages:", n[0])
    try:
        sess.detach()
    except Exception:
        pass

if __name__ == "__main__":
    main()
