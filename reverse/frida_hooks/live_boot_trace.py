#!/usr/bin/env python3
"""SF2 live boot trace driver v2 — spawn + log-only hooks + phase-driven UI drive.

Phases:
  boot   : passive capture; skip intro video (one tap); decline update dialog if any.
  dojo   : reach dojo, then inject move/attack input, log per-action reactions.
  menu   : open menu, visit tabs (Map/Shop/Settings/Profile).
  battle : enter a fight from the map, capture battle boot, short combat.

Log-only hooks; taps gated (only after video/dialog/dojo detected) to avoid the
known load-phase SIGSEGV (base+0x61dc).
"""
import frida
import sys
import time
import os
import re
import subprocess
from PIL import Image

DEVICE_ID = "684006127d29"
PKG = "com.nekki.shadowfight"
HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT_PATH = os.path.join(HERE, "live_boot_trace.js")
SHOT_DIR = os.path.join(os.path.dirname(HERE), "data", "live_shots")

W, H = 1440, 720  # Redmi 6A, game runs LANDSCAPE

def adb(args, binary=False, timeout=20):
    r = subprocess.run(["adb"] + args, capture_output=binary or not binary,
                       timeout=timeout)
    if binary:
        return r.returncode, r.stdout
    return r.returncode, r.stdout.decode("utf-8", "replace")

class ScreenStats:
    def __init__(self, im):
        self.w, self.h = im.size
        px = list(im.getdata())
        n = len(px)
        self.avg = tuple(sum(c[i] for c in px) // n for i in range(3))
        self.bright = sum(1 for c in px if sum(c) / 3 > 150) / n
        self.sat = sum(1 for c in px if (max(c) - min(c)) > 60) / n
        self.teal = sum(1 for c in px if c[2] > c[0] and c[1] > 100) / n
        self.warm = sum(1 for c in px if c[0] > c[2] and c[0] > 120) / n

    def label(self):
        if self.bright < 0.05 and self.sat < 0.05:
            return "black"
        if self.bright < 0.10:
            return "dark"
        if self.teal > 0.25 and self.teal > self.warm:
            return "dojo/teal"
        if self.warm > 0.20:
            return "warm/panel"
        return "mixed"

    def brief(self):
        return "label=%s avg=%s bright=%.2f sat=%.2f teal=%.2f warm=%.2f" % (
            self.label(), self.avg, self.bright, self.sat, self.teal, self.warm)

class Driver:
    def __init__(self, phase, out_path, capture_seconds, taps=(), shot_every=3.0):
        self.phase = phase
        self.out_path = out_path
        self.capture_seconds = capture_seconds
        self.taps = list(taps)
        self.fired = set()
        self.shot_every = shot_every
        self.next_shot = 0.0
        self.next_ui = 0.0
        self.out = open(out_path, "w", encoding="utf-8", buffering=1)
        self.events = []
        self.video_seen = False
        self.video_tap_done = False
        self.t0 = time.time()

    def on_message(self, message, data):
        if message["type"] == "send":
            p = message["payload"]
            tag, t, m = p.get("tag"), float(p.get("t", 0)), p.get("m", "")
            self.events.append((t, tag, m))
            self.out.write("[%7.2f] %-12s %s\n" % (t, tag, m))
            if tag == "VIDEO" and "PLAY" in m:
                self.video_seen = True
        elif message["type"] == "error":
            self.out.write("[JS-ERROR] %s\n" % message.get("stack", message.get("description", "")))
        else:
            self.out.write("[MSG] %s\n" % message)

    def emit_ui(self, note):
        self.out.write("[%7.2f] [UI] %s\n" % (time.time() - self.t0, note))

    def tap(self, x, y, note):
        self.emit_ui("tap(%d,%d) %s" % (x, y, note))
        rc, _ = adb(["shell", "input tap %d %d" % (x, y)])
        if rc != 0:
            self.emit_ui("tap FAILED rc=%d" % rc)

    def hold(self, x, y, ms, note):
        self.emit_ui("hold(%d,%d,%dms) %s" % (x, y, ms, note))
        rc, _ = adb(["shell", "input swipe %d %d %d %d %d" % (x, y, x, y, ms)])
        if rc != 0:
            self.emit_ui("hold FAILED rc=%d" % rc)

    def run(self, device):
        adb(["shell", "input keyevent 224"])  # wake screen
        adb(["shell", "svc power stayon true"])  # keep awake
        adb(["shell", "am force-stop " + PKG])
        time.sleep(2.0)
        pid = device.spawn([PKG])
        self.emit_ui("spawned pid=%d" % pid)
        session = device.attach(pid)
        script = session.create_script(open(SCRIPT_PATH, encoding="utf-8").read())
        script.on("message", self.on_message)
        script.load()
        device.resume(pid)
        self.emit_ui("resumed")
        t_start = time.time()

        while time.time() - t_start < self.capture_seconds:
            elapsed = time.time() - t_start
            for (at, x, y, note) in self.taps:
                if at not in self.fired and elapsed >= at:
                    self.fired.add(at)
                    self.tap(x, y, note)
            if self.video_seen and not self.video_tap_done and elapsed > 3.0:
                self.video_tap_done = True
                self.tap(W // 2, H // 2, "skip intro video")
            if elapsed >= self.next_shot:
                self.next_shot = elapsed + self.shot_every
                self.shot(elapsed)
            if elapsed >= self.next_ui:
                self.next_ui = elapsed + 6.0
                self.ui_scan()
            time.sleep(0.5)

        self.emit_ui("capture window ended")
        rc, alive = adb(["shell", "pidof " + PKG])
        self.emit_ui("pidof after capture: %r" % alive.strip())
        try:
            script.unload()
        except Exception:
            pass
        try:
            session.detach()
        except Exception:
            pass
        self.out.close()
        return pid

    def shot(self, sec):
        try:
            os.makedirs(SHOT_DIR, exist_ok=True)
            p = os.path.join(SHOT_DIR, "run%s_t%04.1f.png" % (self.phase, sec))
            rc, _ = adb(["shell", "screencap -p /sdcard/live_shot.png"], timeout=12)
            if rc != 0:
                self.emit_ui("shot FAILED rc=%d" % rc)
                return
            r = subprocess.run(["adb", "pull", "/sdcard/live_shot.png", p],
                               capture_output=True, timeout=15)
            if r.returncode != 0 or not os.path.exists(p) or os.path.getsize(p) < 1000:
                self.emit_ui("shot pull FAILED rc=%d" % r.returncode)
                return
            im = Image.open(p).convert("RGB")
            s = ScreenStats(im)
            self.emit_ui("shot %s %s" % (os.path.basename(p), s.brief()))
        except Exception as e:
            self.emit_ui("shot err: %s" % e)

    def ui_scan(self):
        try:
            rc, _ = adb(["shell", "uiautomator dump /sdcard/ui.xml"], timeout=15)
            rc2, xml = adb(["shell", "cat /sdcard/ui.xml"], timeout=15)
            if rc2 != 0 or not xml.strip():
                return
            hits = []
            for kw in ["update", "Update", "обнов", "Обнов", "Отмена", "Cancel",
                       "Not now", "not now", "decline", "Decline", "Отклонить"]:
                if kw in xml:
                    hits.append(kw)
            if hits:
                self.emit_ui("UI-DIALOG keywords: %s" % hits)
                m = re.search(r'text="([^"]*)"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"',
                              xml[hits and xml.find(hits[0]) or 0:], re.I)
                # find the decline-ish button: prefer text with Отмена/Cancel/Not now
                m2 = re.search(r'text="([^"]*(?:Отмена|Cancel|Not now|not now|decline|Отклонить)[^"]*)"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', xml, re.I)
                if m2:
                    x = (int(m2.group(2)) + int(m2.group(4))) // 2
                    y = (int(m2.group(3)) + int(m2.group(5))) // 2
                    self.tap(x, y, "decline dialog '%s'" % m2.group(1))
        except Exception as e:
            self.emit_ui("ui-scan err: %s" % e)

def summary(events):
    out = []
    last = None
    for (t, tag, m) in events:
        if tag in ("FILE", "VFS"):
            if m != last:
                out.append((t, tag, m))
                last = m
    return out

def main():
    phase = sys.argv[1] if len(sys.argv) > 1 else "boot"
    out_path = os.path.join(HERE, "live_v7_%s.txt" % phase)
    print("connecting device...", file=sys.stderr)
    device = frida.get_device(DEVICE_ID, timeout=10)
    print("device ok:", device.id, file=sys.stderr)

    if phase == "boot":
        d = Driver("boot", out_path, capture_seconds=50)
    elif phase == "dojo":
        # Landscape 1440x720. SF2 dojo controls: bottom-left = move arrows,
        # bottom-right = punch/kick, right-top = jump/roll.
        taps = [
            (24, 260, 620, "hold LEFT (move back) 2s"),
            (27, 420, 620, "hold RIGHT (move fwd) 2s"),
            (30, 1180, 540, "tap PUNCH"),
            (32, 1000, 540, "tap KICK"),
            (34, 1180, 340, "tap JUMP"),
            (36, 1000, 340, "tap ROLL"),
        ]
        d = Driver("dojo", out_path, capture_seconds=45, taps=taps)
    elif phase == "menu":
        taps = [
            (29, 60, 120, "tap menu button (top-left)"),
            (32, 360, 640, "tap MAP tab"),
            (35, 360, 800, "tap SHOP tab"),
            (38, 360, 960, "tap SETTINGS tab"),
            (41, 360, 1120, "tap PROFILE tab"),
        ]
        d = Driver("menu", out_path, capture_seconds=45, taps=taps)
    elif phase == "battle":
        taps = [
            (29, 60, 120, "tap menu button (top-left)"),
            (32, 360, 640, "tap MAP tab"),
            (35, 360, 900, "tap first battle node"),
            (37, 360, 720, "tap FIGHT/GO (battle confirm)"),
        ]
        d = Driver("battle", out_path, capture_seconds=45, taps=taps)
    else:
        print("unknown phase", phase)
        sys.exit(1)

    try:
        d.run(device)
    except Exception as e:
        d.out.write("[DRIVER-ERROR] %r\n" % e)
        d.out.close()
        print("FAILED: %r" % e, file=sys.stderr)
        sys.exit(1)

    s = summary(d.events)
    print("=== unique FILE/VFS opens (chronological) ===")
    for (t, tag, m) in s:
        print("%7.2f %-4s %s" % (t, tag, m))
    print("done ->", out_path)

if __name__ == "__main__":
    main()
