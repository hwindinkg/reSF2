#!/usr/bin/env python3
"""Dump exports of libs3e_android.so (game code lives there in Marmalade builds)."""
import frida, sys, time, json, re

DEV = "684006127d29"
PKG = "com.nekki.shadowfight"

def main():
    dev = frida.get_device(DEV, timeout=10)
    pid = dev.spawn([PKG])
    sess = dev.attach(pid)
    out = []

    def on_msg(m, d):
        if m["type"] == "send":
            out.append(m["payload"])

    code = r"""
    (function(){
      function sendp(o){ send(o); }
      setTimeout(function(){
        var mods = Process.enumerateModules();
        var names = mods.map(function(m){return m.name;});
        sendp({msg:'all-modules', list: names});
        ['libs3e_android.so'].forEach(function(mn){
          try {
            var m = Process.getModuleByName(mn);
            var exps = m.enumerateExports();
            sendp({msg:'exports', mod: mn, total: exps.length, names: exps.map(function(e){return e.name;}),
                   addresses: exps.map(function(e){return e.address.toString();})});
          } catch(e) { sendp({msg:'exports-err', mod: mn, err: String(e)}); }
        });
        sendp({msg:'done'});
      }, 9000);
    })();
    """
    sc = sess.create_script(code)
    sc.on("message", on_msg)
    sc.load()
    dev.resume(pid)
    time.sleep(14)
    with open("probe_s3e.json", "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1)
    for o in out:
        if o.get("msg") == "all-modules":
            print("MODULES:", [n for n in o["list"] if "s3e" in n or "main" in n or "shadow" in n.lower() or "nekki" in n])
        elif o.get("msg") == "exports":
            print("=== %s total=%d ===" % (o["mod"], o["total"]))
            for n in o["names"]:
                print("   ", n)
        elif o.get("msg") == "done":
            print("done")
    try:
        sess.detach()
    except Exception:
        pass

if __name__ == "__main__":
    main()
