#!/usr/bin/env python3
"""Pump test 3: attach AFTER resume + delay, then hook."""
import frida, sys, time

DEV = "684006127d29"
PKG = "com.nekki.shadowfight"
code = r"""
'use strict';
function log(tag, m) { send({ tag: tag, t: Date.now() % 100000, m: m }); }
log('INIT', 'loaded');
var cnt = 0;
try {
    var a = Module.findExportByName('libc.so', 'open');
    Interceptor.attach(a, { onEnter: function (args) {
        cnt++;
        try { var p = args[0].readUtf8String(200); if (p && p.indexOf('/') !== -1) log('FILE', p); } catch (e) {}
    }});
    log('HOOK', 'open hooked @ ' + a);
} catch (e) { log('ERR', String(e)); }
setInterval(function () { log('STATUS', 'open-calls=' + cnt); }, 3000);
"""

def main():
    dev = frida.get_device(DEV, timeout=10)
    pid = dev.spawn([PKG])
    sess = dev.attach(pid)
    dev.resume(pid)
    time.sleep(4.0)   # let the game boot a bit, THEN attach hooks
    n = [0]
    def on_msg(m, d):
        if m["type"] == "send":
            n[0] += 1
            print("[%d] %s %s" % (n[0], m["payload"]["tag"], str(m["payload"]["m"])[:110]))
    sc = sess.create_script(code)
    sc.on("message", on_msg)
    sc.load()
    t0 = time.time()
    while time.time() - t0 < 14:
        time.sleep(0.5)
    print("total messages:", n[0])
    try:
        sess.detach()
    except Exception:
        pass

if __name__ == "__main__":
    main()
