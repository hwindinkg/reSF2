#!/usr/bin/env python3
"""SF2 live evidence driver v2 — spawn + capture + best-effort UI drive to a fight."""
import frida
import sys
import time
import os
import subprocess

DEVICE_ID = "684006127d29"
PKG = "com.nekki.shadowfight"
HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT_PATH = os.path.join(HERE, "live_capture.js")
OUT_PATH = os.path.join(HERE, "live_capture_out4.txt")
CAPTURE_SECONDS = 60.0
UI_TAPS = []  # taps crash the game during load (SIGSEGV base+0x61dc) — no input injection

out = open(OUT_PATH, "w", buffering=1)

def on_message(message, data):
    if message["type"] == "send":
        out.write("[SEND] %s\n" % message["payload"])
    elif message["type"] == "error":
        out.write("[JS-ERROR] %s\n" % message.get("stack", message.get("description", "")))
    else:
        out.write("[MSG] %s\n" % message)

def adb(*args):
    return subprocess.run(["adb"] + list(args), capture_output=True, text=True, timeout=30)

def main():
    print("connecting device...", file=sys.stderr)
    device = frida.get_device(DEVICE_ID, timeout=10)
    print("device ok:", device.id, file=sys.stderr)

    adb("shell", "am force-stop " + PKG)
    time.sleep(2.0)

    pid = device.spawn([PKG])
    print("spawned pid:", pid, file=sys.stderr)
    session = device.attach(pid)
    script = session.create_script(open(SCRIPT_PATH, encoding="utf-8").read())
    script.on("message", on_message)
    script.load()
    device.resume(pid)
    print("resumed. capturing %.0fs total" % CAPTURE_SECONDS, file=sys.stderr)

    t0 = time.time()
    fired = set()
    while time.time() - t0 < CAPTURE_SECONDS:
        elapsed = time.time() - t0
        for (at, x, y, note) in UI_TAPS:
            if at not in fired and elapsed >= at:
                fired.add(at)
                out.write("[UI] tap (%d,%d) at t=%.0fs: %s\n" % (x, y, elapsed, note))
                r = adb("shell", "input tap %d %d" % (x, y))
                if r.returncode != 0:
                    out.write("[UI] tap FAILED: %s\n" % r.stderr)
        time.sleep(0.5)

    out.write("[DRIVER] capture window ended\n")
    try:
        script.unload(); session.detach()
    except Exception:
        pass
    out.close()
    print("done ->", OUT_PATH, file=sys.stderr)

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        out.write("[DRIVER-ERROR] %r\n" % e)
        out.close()
        print("FAILED: %r" % e, file=sys.stderr)
        sys.exit(1)
