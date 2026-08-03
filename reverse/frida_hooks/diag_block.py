#!/usr/bin/env python3
"""Diagnose the frozen game: hook libc network calls to find what blocks the logic loop."""
import frida, sys, time, subprocess

DEV = "684006127d29"
PKG = "com.nekki.shadowfight"

code = r"""
'use strict';
function log(tag, m) { send({ tag: tag, t: Date.now() % 100000, m: m }); }
log('INIT', 'attached');
var cnt = { connect: 0, send: 0, recv: 0, select: 0, poll: 0, epoll: 0, nanosleep: 0, futex: 0 };
function hook(name, fn, argfmt) {
    try {
        var a = Module.findExportByName('libc.so', name);
        if (!a) { log('NOEXPORT', name); return; }
        Interceptor.attach(a, {
            onEnter: function (args) {
                cnt[fn] = (cnt[fn] || 0) + 1;
                if (cnt[fn] <= 6) log(name.toUpperCase(), argfmt(args));
            },
            onLeave: function (retval) {
                if (name === 'connect' && retval.toInt32() < 0 && cnt[fn] <= 6) log('CONNECT-RET', 'rc=' + retval.toInt32());
            }
        });
        log('HOOK', 'hooked ' + name);
    } catch (e) { log('ERR', name + ': ' + e); }
}
hook('connect', 'connect', function (a) { return 'sock=' + a[0].toInt32() + ' addr_ptr=' + a[1]; });
hook('send', 'send', function (a) { return 'sock=' + a[0].toInt32() + ' len=' + a[2].toInt32(); });
hook('sendto', 'send', function (a) { return 'sock=' + a[0].toInt32() + ' len=' + a[2].toInt32(); });
hook('recv', 'recv', function (a) { return 'sock=' + a[0].toInt32() + ' len=' + a[2].toInt32(); });
hook('recvfrom', 'recv', function (a) { return 'sock=' + a[0].toInt32() + ' len=' + a[2].toInt32(); });
hook('poll', 'poll', function (a) { return 'nfds=' + a[1].toInt32() + ' timeout=' + a[2].toInt32(); });
hook('select', 'select', function (a) { return 'nfds=' + a[0].toInt32(); });
hook('nanosleep', 'nanosleep', function (a) { return ''; });
hook('__open', 'open2', function (a) { return ''; });
setInterval(function () {
    var parts = [];
    for (var k in cnt) parts.push(k + '=' + cnt[k]);
    log('STATUS', parts.join(' '));
}, 4000);
"""

def main():
    dev = frida.get_device(DEV, timeout=10)
    r = subprocess.run(["adb", "shell", "pidof", PKG], capture_output=True, text=True)
    pids = r.stdout.strip().split()
    if not pids:
        print("game not running")
        return
    pid = int(pids[0])
    print("attaching to pid", pid)
    sess = dev.attach(pid)
    n = [0]
    def on_msg(m, d):
        if m["type"] == "send":
            n[0] += 1
            print("[%d] %s %s" % (n[0], m["payload"]["tag"], m["payload"]["m"][:120]))
    sc = sess.create_script(code)
    sc.on("message", on_msg)
    sc.load()
    t0 = time.time()
    while time.time() - t0 < 20:
        time.sleep(0.5)
    print("total:", n[0])
    try:
        sess.detach()
    except Exception:
        pass

if __name__ == "__main__":
    main()
