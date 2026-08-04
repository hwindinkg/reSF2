#!/usr/bin/env python3
"""SF2 live interaction trace driver v10 — ATTACH to a live process (not spawn).

Key discovery (v9/spawn3): `adb input tap` events NEVER reach the game window on
this device (dispatchTouchEvent not called), but physical `sendevent` touches DO
reach it (kernel-level). Verified: display(x,y) -> raw(y, 1439-x) on mtk-tpd
(/dev/input/event2). The update dialog ("Обновить/Отмена") was dismissed via a
sendevent tap on "Отмена" — the game process survived.

This driver: attach to the given PID (or find it), re-install log-only hooks,
then inject dojo/menu/battle actions via sendevent, gated on dojo detection
(menu.mp3) — same tight window logic as v9 but with WORKING input delivery.
"""
import frida
import sys
import time
import os
import subprocess
from PIL import Image

DEVICE_ID = "684006127d29"
PKG = "com.nekki.shadowfight"
HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT_PATH = os.path.join(HERE, "live_v8_interact.js")
SHOT_DIR = os.path.join(os.path.dirname(HERE), "data", "live_shots_v8")
W, H = 1440, 720
EV = "/dev/input/event2"
WINDOW_END = 9.0


def adb(args, binary=False, timeout=20):
    r = subprocess.run(["adb"] + args, capture_output=True, timeout=timeout)
    if binary:
        return r.returncode, r.stdout
    return r.returncode, r.stdout.decode("utf-8", "replace")


def sendevent(x_disp, y_disp, hold_ms=60, slot=3):
    """Physical tap in DISPLAY coordinates (landscape 1440x720).
    Conversion (verified on mtk-tpd): raw_x = y_disp, raw_y = 1439 - x_disp."""
    rx, ry = y_disp, 1439 - x_disp
    cmd = ("su -c 'sendevent %s 3 47 %d; sendevent %s 3 53 %d; sendevent %s 3 54 %d; "
           "sendevent %s 1 330 1; sendevent %s 0 0 0; sleep 0.%d; "
           "sendevent %s 1 330 0; sendevent %s 3 47 -1; sendevent %s 0 0 0'"
           % (EV, slot, EV, rx, EV, ry, EV, EV, max(5, hold_ms), EV, EV, EV))
    rc, out = adb(["shell", cmd], timeout=20)
    return rc, out


class Driver:
    def __init__(self, phase, out_path, taps, capture_seconds=60):
        self.phase = phase
        self.out_path = out_path
        self.capture_seconds = capture_seconds
        self.taps = list(taps)
        self.fired = set()
        self.dojo_at = None
        self.dojo_t0 = None
        self.events = []
        self.out = open(out_path, "w", encoding="utf-8", buffering=1)
        self.t_start = None
        self.t0 = time.time()
        self.shot_seq = 0

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
                self.emit_ui("DOJO-STABLE detected @%.2f (menu.mp3) — window open" % t)
        elif message["type"] == "error":
            self.out.write("[JS-ERROR] %s\n" % message.get("stack", message.get("description", "")))
        else:
            self.out.write("[MSG] %s\n" % message)

    def tap(self, x, y, note):
        self.emit_ui("sendevent-tap(%d,%d) %s" % (x, y, note))
        rc, _ = sendevent(x, y, 60)
        if rc != 0:
            self.emit_ui("sendevent FAILED rc=%d" % rc)

    def hold(self, x, y, ms, note):
        self.emit_ui("sendevent-hold(%d,%d,%dms) %s" % (x, y, ms, note))
        rc, _ = sendevent(x, y, ms)
        if rc != 0:
            self.emit_ui("sendevent FAILED rc=%d" % rc)

    def shot(self, note):
        try:
            os.makedirs(SHOT_DIR, exist_ok=True)
            p = os.path.join(SHOT_DIR, "v10_%s_%02d_%s.png" % (self.phase, self.shot_seq, note))
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
            px = list(im.getdata())
            n = len(px)
            avg = tuple(sum(c[i] for c in px) // n for i in range(3))
            bright = sum(1 for c in px if sum(c) / 3 > 150) / n
            sat = sum(1 for c in px if (max(c) - min(c)) > 60) / n
            warm = sum(1 for c in px if c[0] > c[2] and c[0] > 120) / n
            lab = "black" if (bright < 0.05 and sat < 0.05) else "dark" if bright < 0.10 \
                else "warm/panel" if warm > 0.20 else "mixed"
            self.emit_ui("shot %s label=%s avg=%s bright=%.2f sat=%.2f warm=%.2f" % (
                os.path.basename(p), lab, avg, bright, sat, warm))
        except Exception as e:
            self.emit_ui("shot err: %s" % e)

    def run(self, device, pid):
        adb(["shell", "input keyevent 224"])
        adb(["shell", "svc power stayon true"])
        session = device.attach(pid)
        script = session.create_script(open(SCRIPT_PATH, encoding="utf-8").read())
        script.on("message", self.on_message)
        script.load()
        self.emit_ui("attached to pid=%d, hooks live" % pid)
        self.t_start = time.time()
        self.shot("attach")

        while time.time() - self.t_start < self.capture_seconds:
            elapsed = time.time() - self.t_start

            if elapsed - getattr(self, "last_shot", -99) >= 3.0:
                self.last_shot = elapsed
                self.shot("t%.0f" % elapsed)

            if elapsed - getattr(self, "last_hb", -99) >= 12.0:
                self.last_hb = elapsed
                rc, alive = adb(["shell", "pidof " + PKG])
                self.emit_ui("heartbeat: pidof=%r" % alive.strip())

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
            time.sleep(0.25)

        self.emit_ui("capture window ended; fired=%d/%d" % (len(self.fired), len(self.taps)))
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
    out_path = os.path.join(HERE, "live_v10_%s.txt" % phase)
    print("connecting device...", file=sys.stderr)
    device = frida.get_device(DEVICE_ID, timeout=10)
    rc, out = adb(["shell", "pidof " + PKG])
    pid = int(out.strip().split()[0]) if out.strip() else None
    if not pid:
        print("game not running — start it first (it must reach the update dialog, then decline via sendevent)", file=sys.stderr)
        sys.exit(1)
    print("game pid:", pid, file=sys.stderr)

    if phase == "dojo":
        taps = [
            (0.5, "hold", 260, 620, 900, "move_LEFT"),
            (1.9, "hold", 420, 620, 900, "move_RIGHT"),
            (3.3, "tap", 1180, 540, 0, "PUNCH"),
            (4.5, "tap", 1000, 540, 0, "KICK"),
            (5.7, "tap", 1180, 340, 0, "JUMP"),
            (6.9, "tap", 1000, 340, 0, "ROLL"),
            (8.1, "tap", 700, 600, 0, "weapon_switch"),
        ]
        d = Driver("dojo", out_path, taps, capture_seconds=60)
    elif phase == "menu":
        taps = [
            (0.5, "tap", 60, 120, 0, "open_menu"),
            (2.0, "tap", 360, 240, 0, "tab_MAP"),
            (3.5, "tap", 360, 340, 0, "tab_SHOP"),
            (5.0, "tap", 360, 440, 0, "tab_SETTINGS"),
            (6.5, "tap", 360, 540, 0, "tab_PROFILE"),
        ]
        d = Driver("menu", out_path, taps, capture_seconds=60)
    elif phase == "battle":
        taps = [
            (0.5, "tap", 60, 120, 0, "open_menu"),
            (2.0, "tap", 360, 240, 0, "tab_MAP"),
            (3.5, "tap", 720, 400, 0, "battle_node_center"),
            (5.0, "tap", 720, 620, 0, "FIGHT_confirm"),
            (6.8, "tap", 1180, 540, 0, "combat_PUNCH1"),
            (8.0, "tap", 1000, 540, 0, "combat_KICK1"),
        ]
        d = Driver("battle", out_path, taps, capture_seconds=60)
    else:
        print("unknown phase", phase)
        sys.exit(1)

    try:
        d.run(device, pid)
    except Exception as e:
        d.out.write("[DRIVER-ERROR] %r\n" % e)
        d.out.close()
        print("FAILED: %r" % e, file=sys.stderr)
        sys.exit(1)

    print("done ->", out_path, file=sys.stderr)
    if d.dojo_at is None:
        print("WARNING: dojo NOT detected (no menu.mp3 in trace)", file=sys.stderr)


if __name__ == "__main__":
    main()
