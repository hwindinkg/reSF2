#!/usr/bin/env python3
"""Probe modules AFTER resume (the game .so loads late)."""
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
      sendp({msg:'early-modules', list: Process.enumerateModules().map(function(m){return m.name;})});
      setTimeout(function(){
        var mods = Process.enumerateModules();
        sendp({msg:'late-modules', list: mods.map(function(m){return {name:m.name, base:m.base.toString(), size:m.size};})});
        var game = mods.filter(function(m){ return m.size > 3000000 && /\.so$/.test(m.name); });
        game.forEach(function(m){
          try {
            var exps = m.enumerateExports();
            var hits = exps.filter(function(e){ return /s3e|vfs|file|open|read|load|fopen|fread/i.test(e.name); });
            sendp({msg:'exports', mod: m.name, total: exps.length, hits: hits.map(function(e){return e.name;}).slice(0, 300)});
          } catch(e) { sendp({msg:'exports-err', mod: m.name, err: String(e)}); }
        });
        sendp({msg:'done'});
      }, 9000);
    })();
    """
    sc = sess.create_script(code)
    sc.on("message", on_msg)
    sc.load()
    dev.resume(pid)
    time.sleep(15)
    with open("probe_exports3.json", "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1)
    for o in out:
        if o.get("msg") == "early-modules":
            print("EARLY:", [n for n in o["list"] if "nekki" in n or "shadow" in n.lower() or "main" in n or "s3e" in n])
        elif o.get("msg") == "late-modules":
            print("BIG MODULES:", [(m["name"], m["size"]) for m in o["list"] if m["size"] > 3000000])
        elif o.get("msg") == "exports":
            print("=== %s (total %d) ===" % (o["mod"], o["total"]))
            for h in o["hits"]:
                print("   ", h)
        elif o.get("msg") == "done":
            print("done")
    try:
        sess.detach()
    except Exception:
        pass

if __name__ == "__main__":
    main()
