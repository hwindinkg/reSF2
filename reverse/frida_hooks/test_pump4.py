#!/usr/bin/env python3
"""Pump test 4: diagnose why readUtf8String fails on open() args."""
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
        var p0 = args[0];
        var s = null, err = null;
        try { s = p0.readUtf8String(200); } catch (e) { err = String(e); }
        if (cnt <= 6) log('OPEN', 'ptr=' + p0 + ' str=' + (s === null ? 'NULL' : JSON.stringify(s.slice(0, 90))) + ' err=' + err);
    }});
    log('HOOK', 'open hooked @ ' + a);
} catch (e) { log('ERR', String(e)); }
setInterval(function () { log('STATUS', 'open-calls=' + cnt); }, 4000);
"""

def main():
    dev = frida.get_device(DEV, timeout=10)
    pid = dev.spawn([PKG])
    sess = dev.attach(pid)
    dev.resume(pid)
    time.sleep(4.0)
    n = [0]
    def on_msg(m, d):
        if m["type"] == "send":
            n[0] += 1
            print("[%d] %s %s" % (n[0], m["payload"]["tag"], str(m["payload"]["m"])[:140]))
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
