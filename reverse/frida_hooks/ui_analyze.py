#!/usr/bin/env python3
"""UI analysis helper: pull uiautomator dump + screenshot, classify."""
import subprocess, sys, re, os
from PIL import Image

def adb(*args):
    return subprocess.run(["adb"] + list(args), capture_output=True, timeout=25)

def main():
    # uiautomator dump
    adb("shell", "uiautomator dump /sdcard/ui.xml")
    r = adb("shell", "cat /sdcard/ui.xml")
    xml = r.stdout.decode("utf-8", "replace")
    print("ui.xml len:", len(xml))
    seen = set()
    for m in re.finditer(r'text="([^"]{1,80})"[^>]*bounds="(\[[0-9,\[\]]+\])"', xml):
        t = m.group(1).strip()
        if t and t not in seen:
            seen.add(t)
            print("TEXT:", repr(t), m.group(2))
    # clickable nodes
    cn = 0
    for m in re.finditer(r'clickable="true"[^>]*bounds="(\[[0-9,\[\]]+\])"', xml):
        cn += 1
        if cn <= 12:
            print("CLICKABLE:", m.group(1))
    print("clickable count:", cn)

if __name__ == "__main__":
    main()
