#!/usr/bin/env python3
"""Live capture: network enabled. Taps gated on video/dialog; wait for dojo, then try combat controls."""
import frida, sys, time, os, re, subprocess
from PIL import Image

DEV = "684006127d29"
PKG = "com.nekki.shadowfight"
HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT_PATH = os.path.join(HERE, "live_boot_trace.js")
SHOT_DIR = os.path.join(os.path.dirname(HERE), "data", "live_shots")
W, H = 1440, 720

def adb(*args, timeout=25):
    return subprocess.run(["adb"] + list(args), capture_output=True, timeout=timeout)

class Driver:
    def __init__(self, phase, out_path, capture_seconds):
        self.phase = phase
        self.out_path = out_path
        self.capture_seconds = capture_seconds
        self.out = open(out_path, "w", encoding="utf-8", buffering=1)
        self.events = []
        self.video_seen = False
        self.video_tap_done = False
        self.dojo_seen = False
        self.t0 = time.time()
        self.next_shot = 0.0

    def on_message(self, message, data):
        if message["type"] == "send":
            p = message["payload"]
            tag, t, m = p.get("tag"), float(p.get("t", 0)), p.get("m", "")
            self.events.append((t, tag, m))
            self.out.write("[%7.2f] %-12s %s\n" % (t, tag, m))
            if tag == "VIDEO" and "PLAY" in m:
                self.video_seen = True
            if tag == "VFS" and "menu.mp3" in m:
                self.dojo_seen = True
        elif message["type"] == "error":
            self.out.write("[JS-ERROR] %s\n" % message.get("stack", message.get("description", "")))

    def emit(self, note):
        self.out.write("[%7.2f] [UI] %s\n" % (time.time() - self.t0, note))

    def tap(self, x, y, note):
        self.emit("tap(%d,%d) %s" % (x, y, note))
        adb("shell", "input tap %d %d" % (x, y))

    def hold(self, x, y, ms, note):
        self.emit("hold(%d,%d,%dms) %s" % (x, y, ms, note))
        adb("shell", "input swipe %d %d %d %d %d" % (x, y, x, y, ms))

    def shot(self, sec):
        try:
            adb("shell", "screencap -p /sdcard/live_shot.png", timeout=12)
            p = os.path.join(SHOT_DIR, "run%s_t%04.1f.png" % (self.phase, sec))
            r = subprocess.run(["adb", "pull", "/sdcard/live_shot.png", p], capture_output=True, timeout=15)
            if r.returncode == 0 and os.path.getsize(p) > 1000:
                self.emit("shot %s saved" % os.path.basename(p))
        except Exception as e:
            self.emit("shot err: %s" % e)

    def run(self, device):
        pid = device.spawn([PKG])
        self.emit("spawned pid=%d" % pid)
        session = device.attach(pid)
        script = session.create_script(open(SCRIPT_PATH, encoding="utf-8").read())
        script.on("message", self.on_message)
        script.load()
        device.resume(pid)
        self.emit("resumed")
        t_start = time.time()
        combat_done = False

        while time.time() - t_start < self.capture_seconds:
            elapsed = time.time() - t_start
            if self.video_seen and not self.video_tap_done and elapsed > 3.0:
                self.video_tap_done = True
                self.tap(W // 2, H // 2, "skip intro video")
            if elapsed >= self.next_shot:
                self.next_shot = elapsed + 4.0
                self.shot(elapsed)
            # after dojo loads (menu.mp3), wait 6s then drive combat controls
            if self.dojo_seen and not combat_done and elapsed > 6.0:
                combat_done = True
                self.emit("DOJO detected — driving combat controls")
                self.hold(260, 620, 1500, "hold LEFT (move back)")
                time.sleep(1)
                self.hold(420, 620, 1500, "hold RIGHT (move fwd)")
                time.sleep(1)
                self.tap(1180, 540, "tap PUNCH")
                time.sleep(1.2)
                self.tap(1000, 540, "tap KICK")
                time.sleep(1.2)
                self.tap(1180, 340, "tap JUMP")
                time.sleep(1.2)
                self.tap(1000, 340, "tap ROLL")
                time.sleep(1.2)
                self.emit("combat drive done")
            time.sleep(0.4)

        self.emit("capture window ended")
        r = adb("shell", "pidof " + PKG)
        self.emit("pidof: %r" % r.stdout.decode().strip())
        try:
            script.unload(); session.detach()
        except Exception:
            pass
        self.out.close()

def main():
    out_path = os.path.join(HERE, "live_v7_net.txt")
    device = frida.get_device(DEV, timeout=10)
    d = Driver("net", out_path, capture_seconds=60)
    try:
        d.run(device)
    except Exception as e:
        d.out.write("[DRIVER-ERROR] %r\n" % e)
        d.out.close()
        print("FAILED: %r" % e, file=sys.stderr)
        sys.exit(1)
    print("done ->", out_path)

if __name__ == "__main__":
    main()
