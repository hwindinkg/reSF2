#!/usr/bin/env python3
"""Check if the game reads touch state (getters) after injected taps."""
import frida, sys, time, subprocess

DEV = "684006127d29"
PKG = "com.nekki.shadowfight"

code = r"""
'use strict';
function log(tag, m) { send({ tag: tag, t: Date.now() % 100000, m: m }); }
log('INIT', 'attached');
var gtx = 0, gty = 0, gts = 0, lastX = -999, lastY = -999;
try {
    Interceptor.attach(Module.findExportByName('libs3e_android.so', 's3ePointerGetTouchX'), {
        onLeave: function (retval) {
            gtx++;
            var v = retval.toInt32();
            if (gtx <= 20) log('GETX', '#' + gtx + ' = ' + v);
        }
    });
    Interceptor.attach(Module.findExportByName('libs3e_android.so', 's3ePointerGetTouchY'), {
        onLeave: function (retval) {
            gty++;
            var v = retval.toInt32();
            if (gty <= 20) log('GETY', '#' + gty + ' = ' + v);
        }
    });
    Interceptor.attach(Module.findExportByName('libs3e_android.so', 's3ePointerGetTouchState'), {
        onLeave: function (retval) {
            gts++;
            var v = retval.toInt32();
            if (gts <= 20) log('GETSTATE', '#' + gts + ' = ' + v);
        }
    });
    log('HOOK', 'getters hooked');
} catch (e) { log('ERR', String(e)); }
setInterval(function () { log('STATUS', 'gtx=' + gtx + ' gty=' + gty + ' gts=' + gts); }, 4000);
"""

def main():
    dev = frida.get_device(DEV, timeout=10)
    pid = int(subprocess.run(["adb", "shell", "pidof", PKG], capture_output=True, text=True).stdout.strip())
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
    subprocess.run(["adb", "shell", "input tap 900 520"], timeout=10)
    print("tapped 900,520")
    time.sleep(3)
    subprocess.run(["adb", "shell", "input swipe 500 500 500 500 500"], timeout=10)
    print("held 500,500 500ms")
    time.sleep(3)
    t0 = time.time()
    while time.time() - t0 < 5:
        time.sleep(0.5)
    print("total messages:", n[0])
    try:
        sess.detach()
    except Exception:
        pass

if __name__ == "__main__":
    main()
