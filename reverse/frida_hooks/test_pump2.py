#!/usr/bin/env python3
"""Pump test 2: heartbeat + post-resume send() delivery."""
import frida, sys, time

DEV = "684006127d29"
PKG = "com.nekki.shadowfight"
code = r"""
'use strict';
function log(tag, m) { send({ tag: tag, t: Date.now() % 100000, m: m }); }
log('INIT', 'loaded');
var n = 0;
setInterval(function () {
    n++;
    log('HEARTBEAT', 'hb ' + n);
    try {
        var a = Module.findExportByName('libc.so', 'open');
        var exps = Process.getModuleByName('libs3e_android.so');
        log('STATUS', 's3e loaded=' + (exps ? 'yes' : 'no'));
    } catch (e) { log('STATUS', 's3e err ' + e); }
}, 2000);
try {
    var a = Module.findExportByName('libc.so', 'open');
    Interceptor.attach(a, { onEnter: function (args) {
        try { var p = args[0].readUtf8String(200); if (p && p.indexOf('/') !== -1) log('FILE', p); } catch (e) {}
    }});
    log('HOOK', 'open hooked');
} catch (e) { log('ERR', String(e)); }
"""

def main():
    dev = frida.get_device(DEV, timeout=10)
    pid = dev.spawn([PKG])
    sess = dev.attach(pid)
    n = [0]
    def on_msg(m, d):
        if m["type"] == "send":
            n[0] += 1
            print("[%d] %s %s" % (n[0], m["payload"]["tag"], str(m["payload"]["m"])[:100]))
    sc = sess.create_script(code)
    sc.on("message", on_msg)
    sc.load()
    dev.resume(pid)
    t0 = time.time()
    while time.time() - t0 < 15:
        time.sleep(0.5)
    print("total messages:", n[0])
    try:
        sess.detach()
    except Exception:
        pass

if __name__ == "__main__":
    main()
