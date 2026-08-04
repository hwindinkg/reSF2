#!/usr/bin/env python3
"""SF2 live interaction trace driver v12 вЂ” RETRY loop + network diagnostics.

Boot stalling is RANDOM (~50-80% of spawns): the game stops opening files after
the intro (~4.5s) even though the render loop keeps running at 61fps вЂ” looks
like a blocking network step (connect timeout) in the game-logic thread.

This driver:
- sets wifi sleep policy NEVER (MIUI can sleep wifi on the background)
- hooks libc socket/connect/getaddrinfo/recv in the instrument to NAME the
  stalled network endpoint (log-only)
- spawns up to N times; aborts a spawn if no packs.xml VFS open appears by
  +13s (stalled boot), force-stops and respawns
- once loading runs: decline scans (uiautomator + sendevent), dojo detection
  (menu.mp3), then sendevent taps (packed window) вЂ” all log-only
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
SCRIPT_PATH = os.path.join(HERE, "live_v12_net.js")
SHOT_DIR = os.path.join(os.path.dirname(HERE), "data", "live_shots_v8")
EV = "/dev/input/event2"
WINDOW_END = 9.0
DLG_KEYWORDS = ["РћС‚РјРµРЅР°", "Cancel", "Not now", "not now", "РџРѕР·Р¶Рµ", "РџРѕР·РґРЅРµРµ", "decline", "Decline", "РћС‚РєР»РѕРЅРёС‚СЊ", "РќРµС‚"]


def adb(args, binary=False, timeout=20):
    r = subprocess.run(["adb"] + args, capture_output=True, timeout=timeout)
    if binary:
        return r.returncode, r.stdout
    return r.returncode, r.stdout.decode("utf-8", "replace")


def sendevent(x_disp, y_disp, hold_ms=60, slot=6):
    rx, ry = y_disp, 1439 - x_disp
    cmd = ("su -c 'sendevent %s 3 47 %d; sendevent %s 3 53 %d; sendevent %s 3 54 %d; "
           "sendevent %s 1 330 1; sendevent %s 0 0 0; sleep 0.%d; "
           "sendevent %s 1 330 0; sendevent %s 3 47 -1; sendevent %s 0 0 0'"
           % (EV, slot, EV, rx, EV, ry, EV, EV, max(5, hold_ms), EV, EV, EV))
    rc, out = adb(["shell", cmd], timeout=20)
    return rc, out


class Driver:
    def __init__(self, phase, out_path, taps, max_spawns=3, capture_seconds=75):
        self.phase = phase
        self.out_path = out_path
        self.capture_seconds = capture_seconds
        self.max_spawns = max_spawns
        self.taps = list(taps)
        self.fired = set()
        self.dojo_at = None
        self.dojo_t0 = None
        self.events = []
        self.out = open(out_path, "w", encoding="utf-8", buffering=1)
        self.t_start = None
        self.t0 = time.time()
        self.shot_seq = 0
        self.next_dlg = 8.0
        self.dlg_tries = 0
        self.loading_started = False

    def emit_ui(self, note):
        base = self.t0 if self.t_start is None else self.t_start
        self.out.write("[%7.2f] [UI] %s\n" % (time.time() - base, note))

    def on_message(self, message, data):
        if message["type"] == "send":
            p = message["payload"]
            tag, t, m = p.get("tag"), float(p.get("t", 0)), p.get("m", "")
            self.events.append((t, tag, m))
            self.out.write("[%7.2f] %-10s %s\n" % (t, tag, m))
            if tag == "VFS" and "packs.xml" in m and not self.loading_started:
                self.loading_started = True
                self.emit_ui("LOADING-STARTED (packs.xml) @%.2f" % t)
            if "menu.mp3" in m and self.dojo_at is None:
                self.dojo_at = t
                self.dojo_t0 = time.time()
                self.emit_ui("DOJO-STABLE detected @%.2f (menu.mp3) вЂ” window open" % t)
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
            p = os.path.join(SHOT_DIR, "v12_%s_%02d_%s.png" % (self.phase, self.shot_seq, note))
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

    def decline_scan(self):
        try:
            rc, _ = adb(["shell", "uiautomator dump /sdcard/ui.xml"], timeout=20)
            rc2, xml = adb(["shell", "cat /sdcard/ui.xml"], timeout=20)
            if rc2 != 0 or not xml.strip():
                return
            for kw in DLG_KEYWORDS:
                m = re.search(r'text="([^"]*' + re.escape(kw) + r'[^"]*)"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', xml)
                if m:
                    x = (int(m.group(2)) + int(m.group(4))) // 2
                    y = (int(m.group(3)) + int(m.group(5))) // 2
                    self.dlg_tries += 1
                    self.emit_ui("dialog '%s' @(%d,%d) try=%d вЂ” sendevent decline" % (m.group(1), x, y, self.dlg_tries))
                    self.tap(x, y, "decline '%s'" % m.group(1))
                    return
        except Exception as e:
            self.emit_ui("ui-scan err: %s" % e)

    def run(self, device):
        adb(["shell", "input keyevent 224"])
        adb(["shell", "svc power stayon true"])
        adb(["shell", "settings put global wifi_sleep_policy 2"])
        self.t_start = time.time()

        for attempt in range(1, self.max_spawns + 1):
            if self.dojo_at is not None:
                break
            self.emit_ui("=== attempt %d/%d ===" % (attempt, self.max_spawns))
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
            attempt_t0 = time.time()
            self.loading_started = False

            while True:
                elapsed = time.time() - attempt_t0
                # stalled boot? -> abort this attempt
                if not self.loading_started and not self.dojo_at and elapsed > 25.0:
                    self.emit_ui("STALLED (no packs.xml by 13s) вЂ” aborting attempt")
                    self.shot("stalled")
                    break
                # hard limit for a single attempt (decline can hold it for a while)
                if self.dojo_at is None and elapsed > self.capture_seconds:
                    self.emit_ui("attempt timeout (no dojo) вЂ” aborting")
                    break
                # success condition: window over
                if self.dojo_at is not None and elapsed - (self.dojo_t0 - attempt_t0) > WINDOW_END + 4.0:
                    break

                in_window = self.dojo_at is not None and elapsed - (self.dojo_t0 - attempt_t0) <= WINDOW_END
                if not in_window and elapsed - getattr(self, "last_shot", -99) >= 4.0:
                    self.last_shot = elapsed
                    self.shot("a%d_t%.0f" % (attempt, elapsed))

                if self.dojo_at is None and elapsed >= self.next_dlg and self.dlg_tries < 8:
                    self.next_dlg = elapsed + 5.0
                    self.decline_scan()

                if self.dojo_at is not None:
                    dtime = elapsed - (self.dojo_t0 - attempt_t0)
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

            try:
                script.unload()
            except Exception:
                pass
            try:
                session.detach()
            except Exception:
                pass
            adb(["shell", "am force-stop " + PKG])
            time.sleep(1.0)

        self.emit_ui("done; fired=%d/%d dlg_tries=%d" % (len(self.fired), len(self.taps), self.dlg_tries))
        self.out.close()
        return pid


def main():
    phase = sys.argv[1] if len(sys.argv) > 1 else "dojo"
    out_path = os.path.join(HERE, "live_v12_%s.txt" % phase)
    print("connecting device...", file=sys.stderr)
    device = frida.get_device(DEVICE_ID, timeout=10)
    print("device ok:", device.id, file=sys.stderr)

    if phase == "dojo":
        taps = [
            (12.5, "hold", 260, 620, 900, "move_LEFT"),
            (13.9, "hold", 420, 620, 900, "move_RIGHT"),
            (15.3, "tap", 1180, 540, 0, "PUNCH"),
            (16.5, "tap", 1000, 540, 0, "KICK"),
            (17.7, "tap", 1180, 340, 0, "JUMP"),
            (18.9, "tap", 1000, 340, 0, "ROLL"),
            (20.1, "tap", 700, 600, 0, "weapon_switch"),
        ]
        d = Driver("dojo", out_path, taps, max_spawns=2)
    elif phase == "menu":
        taps = [
            (0.5, "tap", 60, 120, 0, "open_menu"),
            (2.0, "tap", 360, 240, 0, "tab_MAP"),
            (3.5, "tap", 360, 340, 0, "tab_SHOP"),
            (5.0, "tap", 360, 440, 0, "tab_SETTINGS"),
            (6.5, "tap", 360, 540, 0, "tab_PROFILE"),
        ]
        d = Driver("menu", out_path, taps, max_spawns=2)
    elif phase == "battle":
        taps = [
            (0.5, "tap", 60, 120, 0, "open_menu"),
            (2.0, "tap", 360, 240, 0, "tab_MAP"),
            (3.5, "tap", 720, 400, 0, "battle_node_center"),
            (5.0, "tap", 720, 620, 0, "FIGHT_confirm"),
            (6.8, "tap", 1180, 540, 0, "combat_PUNCH1"),
            (8.0, "tap", 1000, 540, 0, "combat_KICK1"),
        ]
        d = Driver("battle", out_path, taps, max_spawns=2)
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
        print("WARNING: dojo NOT reached in any attempt", file=sys.stderr)
    else:
        print("taps fired: %d/%d" % (len(d.fired), len(d.taps)), file=sys.stderr)


if __name__ == "__main__":
    main()

