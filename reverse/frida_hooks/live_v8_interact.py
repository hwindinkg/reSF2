#!/usr/bin/env python3
"""SF2 live interaction trace driver v9 — spawn + log-only hooks + gated UI drive.

Known constraint (verified in v7/v8): the logic loop dies ~21-30s after launch.
The dojo is stable at ~21.4s (VFS 'music/menu.mp3'). Input window is therefore
TIGHT: injections are packed between dojo-detection and +8.5s, no screenshots
inside the window (adb screencap ~1.5s would eat the window). Everything is
log-only; taps gated to AFTER dojo detection; nothing touches game state.

Phases:
  dojo   : hold LEFT/RIGHT, tap PUNCH/KICK/JUMP/ROLL, weapon switch (packed).
  menu   : open menu, visit Map/Shop/Settings/Profile tabs.
  battle : menu -> Map -> battle node -> FIGHT -> short combat.
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
SCRIPT_PATH = os.path.join(HERE, "live_v8_interact.js")
SHOT_DIR = os.path.join(os.path.dirname(HERE), "data", "live_shots_v8")
W, H = 1440, 720
WINDOW_END = 8.5  # seconds after dojo-detection to stop injecting


def adb(args, binary=False, timeout=20):
    r = subprocess.run(["adb"] + args, capture_output=True, timeout=timeout)
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
    def __init__(self, phase, out_path, taps, capture_seconds=55):
        self.phase = phase
        self.out_path = out_path
        self.capture_seconds = capture_seconds
        self.taps = list(taps)          # (t_after_dojo, kind, x, y, ms, note)
        self.fired = set()
        self.dojo_at = None
        self.dojo_t0 = None
        self.events = []
        self.out = open(out_path, "w", encoding="utf-8", buffering=1)
        self.t_start = None
        self.t0 = time.time()
        self.shot_seq = 0
        self.next_ui = 0.0
        self.next_dlg = 12.0

    def emit_ui(self, note):
        base = self.t0 if self.t_start is None else self.t_start
        self.out.write("[%7.2f] [UI] %s\n" % (time.time() - base, note))

    def on_message(self, message, data):
        if message["type"] == "send":
            p = message["payload"]
            tag, t, m = p.get("tag"), float(p.get("t", 0)), p.get("m", "")
            self.events.append((t, tag, m))
            self.out.write("[%7.2f] %-10s %s\n" % (t, tag, m))
            if "menu.mp3" in m and self.dojo_at is None:
                self.dojo_at = t
                self.dojo_t0 = time.time()
                self.emit_ui("DOJO-STABLE detected @%.2f (menu.mp3) — injection window open" % t)
        elif message["type"] == "error":
            self.out.write("[JS-ERROR] %s\n" % message.get("stack", message.get("description", "")))
        else:
            self.out.write("[MSG] %s\n" % message)

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

    def shot(self, note):
        try:
            os.makedirs(SHOT_DIR, exist_ok=True)
            p = os.path.join(SHOT_DIR, "v9_%s_%02d_%s.png" % (self.phase, self.shot_seq, note))
            self.shot_seq += 1
            rc, _ = adb(["shell", "screencap -p /sdcard/live_shot.png"], timeout=12)
            if rc != 0:
                self.emit_ui("shot FAILED rc=%d (%s)" % (rc, note))
                return
            r = subprocess.run(["adb", "pull", "/sdcard/live_shot.png", p],
                               capture_output=True, timeout=15)
            if r.returncode != 0 or not os.path.exists(p) or os.path.getsize(p) < 1000:
                self.emit_ui("shot pull FAILED rc=%d (%s)" % (r.returncode, note))
                return
            im = Image.open(p).convert("RGB")
            s = ScreenStats(im)
            self.emit_ui("shot %s %s" % (os.path.basename(p), s.brief()))
        except Exception as e:
            self.emit_ui("shot err: %s" % e)

    def decline_scan(self):
        """In-engine update dialog: uiautomator finds it (GL game, but the dialog is
        a native view) — tap the decline-ish button. Gated: only after 12s, only if
        no dojo yet (dialog only appears during load)."""
        try:
            rc, _ = adb(["shell", "uiautomator dump /sdcard/ui.xml"], timeout=15)
            rc2, xml = adb(["shell", "cat /sdcard/ui.xml"], timeout=15)
            if rc2 != 0 or not xml.strip():
                return
            m2 = re.search(r'text="([^"]*(?:Отмена|Cancel|Not now|not now|decline|Decline|Отклонить|Нет|No)[^"]*)"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', xml, re.I)
            if m2:
                x = (int(m2.group(2)) + int(m2.group(4))) // 2
                y = (int(m2.group(3)) + int(m2.group(5))) // 2
                self.emit_ui("decline dialog '%s' @(%d,%d)" % (m2.group(1), x, y))
                self.tap(x, y, "decline '%s'" % m2.group(1))
            else:
                for kw in ["update", "Update", "обнов", "Обнов", "Загрузить"]:
                    if kw in xml:
                        self.emit_ui("UI-DIALOG keyword present: %s (no decline button text matched)" % kw)
                        break
        except Exception as e:
            self.emit_ui("ui-scan err: %s" % e)

    def run(self, device):
        adb(["shell", "input keyevent 224"])
        adb(["shell", "svc power stayon true"])
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
        self.t_start = time.time()

        while time.time() - self.t_start < self.capture_seconds:
            elapsed = time.time() - self.t_start
            in_window = self.dojo_at is not None and elapsed - (self.dojo_t0 - self.t_start) <= WINDOW_END

            # screenshots outside the injection window only
            if not in_window and elapsed - getattr(self, "last_shot", -99) >= 3.0:
                self.last_shot = elapsed
                self.shot("t%.0f" % elapsed)

            # decline scan: during load only, outside injection window
            if self.dojo_at is None and elapsed >= self.next_dlg:
                self.next_dlg = elapsed + 6.0
                self.decline_scan()

            # heartbeat
            if elapsed - getattr(self, "last_hb", -99) >= 12.0:
                self.last_hb = elapsed
                rc, alive = adb(["shell", "pidof " + PKG])
                self.emit_ui("heartbeat: pidof=%r" % alive.strip())

            # gated taps: packed, no sleep between (window is ~8s)
            if self.dojo_at is not None:
                dtime = elapsed - (self.dojo_t0 - self.t_start)
                if 0 <= dtime <= WINDOW_END:
                    for (at, kind, x, y, ms, note) in self.taps:
                        key = (at, note)
                        if key not in self.fired and dtime >= at:
                            self.fired.add(key)
                            if kind == "tap":
                                self.tap(x, y, note)
                            else:
                                self.hold(x, y, ms, note)
                            if dtime > WINDOW_END - 1.5:
                                break

            time.sleep(0.25)

        self.emit_ui("capture window ended; fired=%d/%d taps" % (len(self.fired), len(self.taps)))
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


def main():
    phase = sys.argv[1] if len(sys.argv) > 1 else "dojo"
    out_path = os.path.join(HERE, "live_v9_%s.txt" % phase)
    print("connecting device...", file=sys.stderr)
    device = frida.get_device(DEVICE_ID, timeout=10)
    print("device ok:", device.id, file=sys.stderr)

    if phase == "dojo":
        taps = [
            (0.3, "hold", 260, 620, 1000, "move_LEFT"),
            (1.5, "hold", 420, 620, 1000, "move_RIGHT"),
            (2.7, "tap", 1180, 540, 0, "PUNCH"),
            (3.9, "tap", 1000, 540, 0, "KICK"),
            (5.1, "tap", 1180, 340, 0, "JUMP"),
            (6.3, "tap", 1000, 340, 0, "ROLL"),
            (7.5, "tap", 700, 600, 0, "weapon_switch"),
        ]
        d = Driver("dojo", out_path, taps, capture_seconds=50)
    elif phase == "menu":
        taps = [
            (0.4, "tap", 60, 120, 0, "open_menu"),
            (2.0, "tap", 360, 240, 0, "tab_MAP"),
            (3.6, "tap", 360, 340, 0, "tab_SHOP"),
            (5.2, "tap", 360, 440, 0, "tab_SETTINGS"),
            (6.8, "tap", 360, 540, 0, "tab_PROFILE"),
        ]
        d = Driver("menu", out_path, taps, capture_seconds=55)
    elif phase == "battle":
        taps = [
            (0.4, "tap", 60, 120, 0, "open_menu"),
            (2.0, "tap", 360, 240, 0, "tab_MAP"),
            (3.6, "tap", 720, 400, 0, "battle_node_center"),
            (5.2, "tap", 720, 620, 0, "FIGHT_confirm"),
            (7.0, "tap", 1180, 540, 0, "combat_PUNCH1"),
            (8.2, "tap", 1000, 540, 0, "combat_KICK1"),
        ]
        d = Driver("battle", out_path, taps, capture_seconds=60)
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

    print("done ->", out_path, file=sys.stderr)
    if d.dojo_at is None:
        print("WARNING: dojo NOT detected (no menu.mp3 in trace)", file=sys.stderr)
    else:
        print("taps fired: %d/%d" % (len(d.fired), len(d.taps)), file=sys.stderr)


if __name__ == "__main__":
    main()
