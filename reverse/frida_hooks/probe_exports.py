#!/usr/bin/env python3
"""Probe the game's modules/exports for hookable s3e vfs entry points."""
import frida, sys, time, json

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
      var mods = Process.enumerateModules();
      sendp({msg:'modules', list: mods.map(function(m){return {name:m.name, base:m.base.toString(), size:m.size};})});
      var game = mods.filter(function(m){ return /\.so$/.test(m.name) && !/^lib(art|c|m|dl|log|z|android|GLES|EGL|stdc|gcc|stlport|mali|utils|hardware|expat|png|jpeg|flurry|app|sonivox|OpenSL|vorbis|ogg|yuv|av|cutils|crypto|ssl|base|native|fm|mm|OpenMAX|ui|binder|jni|icu|RSc|RS|jnigraphics|hyphen|mem|netd|pac|sigchain|stdc\+\+|stl|unwind|z\.|webview|sqlite|who|wilhelm|xlog)/.test(m.name);}).map(function(m){return m.name;});
      sendp({msg:'game-modules', list: game});
      game.forEach(function(mn){
        try {
          var m = Process.getModuleByName(mn);
          var exps = m.enumerateExports();
          var hits = exps.filter(function(e){ return /s3e|vfs|file|open|read|load|fopen|fread/i.test(e.name); }).map(function(e){return e.name;});
          sendp({msg:'exports', mod: mn, total: exps.length, hits: hits.slice(0, 200)});
        } catch(e) { sendp({msg:'exports-err', mod: mn, err: String(e)}); }
      });
      sendp({msg:'done'});
    })();
    """
    sc = sess.create_script(code)
    sc.on("message", on_msg)
    sc.load()
    time.sleep(4)
    with open("probe_exports.json", "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1)
    for o in out:
        if o.get("msg") == "game-modules":
            print("GAME MODULES:", o["list"])
        elif o.get("msg") == "exports":
            print("EXPORTS %s (total %d):" % (o["mod"], o["total"]))
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
