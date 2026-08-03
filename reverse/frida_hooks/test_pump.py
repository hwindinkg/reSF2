#!/usr/bin/env python3
"""Minimal test: do libc-open events arrive during a sleep-only loop?"""
import frida, sys, time

DEV = "684006127d29"
PKG = "com.nekki.shadowfight"
code = r"""
'use strict';
var t0 = Date.now();
function log(tag, m) { send({ tag: tag, t: ((Date.now()-t0)/1000).toFixed(2), m: m }); }
try {
    var a = Module.findExportByName('libc.so', 'open');
    Interceptor.attach(a, { onEnter: function (args) {
        try { var p = args[0].readUtf8String(200); if (p && p.indexOf('/') !== -1) log('FILE', p); } catch (e) {}
    }});
    log('HOOK', 'open hooked @ ' + a);
} catch (e) { log('ERR', String(e)); }
"""

def main():
    dev = frida.get_device(DEV, timeout=10)
    adb_sleep = len(sys.argv) > 1
    dev_impl = dev
    pid = dev.spawn([PKG])
    sess = dev.attach(pid)
    n = [0]
    def on_msg(m, d):
        if m["type"] == "send":
            n[0] += 1
            if n[0] < 40:
                print("[%s] %s %s" % (m["payload"]["t"], m["payload"]["tag"], m["payload"]["m"][:120]))
    sc = sess.create_script(code)
    sc.on("message", on_msg)
    sc.load()
    dev.resume(pid)
    t0 = time.time()
    while time.time() - t0 < 12:
        time.sleep(0.3)
    print("total messages:", n[0])
    try:
        sess.detach()
    except Exception:
        pass

if __name__ == "__main__":
    main()
