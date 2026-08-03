#!/usr/bin/env python3
"""Probe game's own modules for s3e vfs exports (fixed filter)."""
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
      var game = mods.filter(function(m){
        return /^lib(main|shadow|game|s3e|sf2|app|nekki|droid|marmalade)/i.test(m.name) || /\.so$/.test(m.name) && m.size < 20000000 && !/^lib(android|art|c|m|dl|log|z|stdc|gcc|GLES|EGL|OpenSL|vorbis|ogg|jpeg|png|expat|flurry|sonivox|mali|IMG|srv|usc|gpu|ion|dpframework|gralloc|hwui|skia|stagefright|gui|input|sensor|binder|utils|cutils|hardware|hidl|backtrace|lzma|exif|heif|ft2|pdfium|vulkan|selinux|audioclient|soundpool|vcodec|SwJpg|JpgDec|JpgEnc|pq_|bwc|drm|lz4|libc|libdl|libm|libz|tinyxml|protobuf|synergy|sync|media|powermanager|audiomanager|harfbuzz|memtrack|usbhost|webview|sqlite|who|wilhelm|xlog|fm|mm|pcre|dng|piex|graphics2d|gralloc_extra_sys|gralloc_extra|speexresampler|fred|sonivox|OpenMAX|RS|jnigraphics|hyphen|netd|pac|sigchain|stlport|unwind|base64|processgroup|vndksupport|vintf|amzn|bml|common|cutils|camera|compiler_rt|debuggerd|dex|dvm|emoji|icu|jni|nativehelper|nativeloader|openjdk|soundtouch|stagefright)/.test(m.name);
      }).map(function(m){return m.name;});
      sendp({msg:'game-modules', list: game});
      game.forEach(function(mn){
        try {
          var m = Process.getModuleByName(mn);
          var exps = m.enumerateExports();
          sendp({msg:'exports', mod: mn, total: exps.length, names: exps.map(function(e){return e.name;})});
        } catch(e) { sendp({msg:'exports-err', mod: mn, err: String(e)}); }
      });
      sendp({msg:'done'});
    })();
    """
    sc = sess.create_script(code)
    sc.on("message", on_msg)
    sc.load()
    time.sleep(4)
    with open("probe_exports2.json", "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1)
    for o in out:
        if o.get("msg") == "game-modules":
            print("GAME MODULES:", o["list"])
        elif o.get("msg") == "exports":
            print("=== %s (total %d) ===" % (o["mod"], o["total"]))
            import re
            hits = [n for n in o["names"] if re.search(r"s3e|vfs|file|open|read|load|fopen|fread|iofile", n, re.I)]
            for h in hits[:150]:
                print("   ", h)
        elif o.get("msg") == "done":
            print("done")
    try:
        sess.detach()
    except Exception:
        pass

if __name__ == "__main__":
    main()
