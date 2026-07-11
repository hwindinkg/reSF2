#!/usr/bin/env python3
"""Download SF2 APK and game data using cf_clearance cookie obtained from browser.

Usage:
  First pass the cf_clearance cookie value. We read it from the agent-browser
  session by calling `agent-browser cookies` and extracting cf_clearance.
"""
import os
import sys
import subprocess
import time

WORK_DIR = "/home/z/my-project/work"
URLS = [
    ("sf2.apk", "https://chat.chobat.ru/Shadow+Fight+2_1.9.21.apk"),
    ("sf2.7z", "https://chat.chobat.ru/sf2.7z"),
]
UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"


def get_cf_clearance() -> str:
    """Extract cf_clearance cookie from agent-browser session."""
    result = subprocess.run(
        ["agent-browser", "cookies"],
        capture_output=True, text=True, timeout=30
    )
    for line in result.stdout.splitlines():
        line = line.strip()
        if line.startswith("cf_clearance="):
            return line.split("=", 1)[1]
    raise SystemExit("could not find cf_clearance cookie in browser session")


def download(name: str, url: str, cf_clearance: str) -> bool:
    out_path = os.path.join(WORK_DIR, name)
    tmp_path = out_path + ".part"
    print(f"[..] downloading {name} from {url}", flush=True)
    # Use curl with the cf_clearance cookie and realistic headers.
    # -L: follow redirects
    # -s: silent (we print our own progress)
    # -S: show errors
    # --retry 3: retry on transient failures
    rc = subprocess.run([
        "curl", "-L", "-s", "-S",
        "--retry", "3",
        "--retry-delay", "5",
        "-H", f"User-Agent: {UA}",
        "-H", "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
        "-H", "Accept-Language: en-US,en;q=0.9",
        "-H", f"Cookie: cf_clearance={cf_clearance}",
        "-o", tmp_path,
        url
    ], timeout=600).returncode
    if rc != 0:
        print(f"[!!] {name}: curl exited {rc}", flush=True)
        if os.path.exists(tmp_path):
            os.unlink(tmp_path)
        return False
    size = os.path.getsize(tmp_path)
    # Verify it's not an HTML error page
    with open(tmp_path, "rb") as f:
        head = f.read(16)
    if head[:5] == b"<!DOC" or head[:5] == b"<html" or head[:1] == b"\n":
        print(f"[!!] {name}: got HTML instead of binary (CF cookie expired?)", flush=True)
        with open(tmp_path, "r", errors="replace") as f:
            print(f"     first 300 chars: {f.read(300)!r}", flush=True)
        os.unlink(tmp_path)
        return False
    os.rename(tmp_path, out_path)
    print(f"[ok] {name}: {size} bytes ({size/1024/1024:.1f} MB)", flush=True)
    return True


def main() -> int:
    os.makedirs(WORK_DIR, exist_ok=True)
    cf = get_cf_clearance()
    print(f"[ok] cf_clearance cookie: {cf[:30]}...{cf[-10:]}", flush=True)
    rc = 0
    for name, url in URLS:
        if not download(name, url, cf):
            rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
