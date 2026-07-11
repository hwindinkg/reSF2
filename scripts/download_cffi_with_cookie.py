#!/usr/bin/env python3
"""Download SF2 files: combine curl_cffi (Chrome TLS fingerprint) with
cf_clearance cookie from the agent-browser session.

This is the belt-and-suspenders approach:
  - curl_cffi provides the correct JA3/JA4 TLS fingerprint (Cloudflare's
    primary bot detection signal)
  - cf_clearance cookie proves we already solved the JS challenge
  - Together they should bypass Cloudflare's "Under Attack" mode
"""
import os
import subprocess
import sys
import time

from curl_cffi import requests

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
    raise SystemExit("could not find cf_clearance cookie — make sure browser "
                     "has passed CF challenge on https://chat.chobat.ru/")


def download(name: str, url: str, cf_clearance: str) -> bool:
    out_path = os.path.join(WORK_DIR, name)
    tmp_path = out_path + ".part"
    print(f"[..] downloading {name} from {url}", flush=True)
    try:
        cookies = {"cf_clearance": cf_clearance}
        headers = {
            "User-Agent": UA,
            "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,"
                      "image/avif,image/webp,*/*;q=0.8",
            "Accept-Language": "en-US,en;q=0.9",
        }
        resp = requests.get(url, impersonate="chrome131",
                            cookies=cookies, headers=headers,
                            stream=True, timeout=120)
        if resp.status_code != 200:
            print(f"[!!] {name}: HTTP {resp.status_code}", flush=True)
            ct = resp.headers.get("content-type", "?")
            print(f"     content-type: {ct}", flush=True)
            # Read a bit of the body to see if it's CF challenge
            body = resp.content[:500] if not "stream" else b""
            if body:
                print(f"     body: {body!r}", flush=True)
            return False
        cl = resp.headers.get("content-length", "?")
        ct = resp.headers.get("content-type", "?")
        print(f"[ok] {name}: HTTP 200, content-length={cl}, content-type={ct}",
              flush=True)
        if "text/html" in ct:
            print(f"[!!] {name}: got HTML (CF not bypassed)", flush=True)
            return False
        written = 0
        t0 = time.time()
        with open(tmp_path, "wb") as f:
            for chunk in resp.iter_content(chunk_size=1024 * 1024):
                if chunk:
                    f.write(chunk)
                    written += len(chunk)
                    if written % (10 * 1024 * 1024) < 1024 * 1024:
                        elapsed = time.time() - t0
                        speed = written / elapsed if elapsed > 0 else 0
                        print(f"    {name}: {written/1024/1024:.1f} MB "
                              f"({speed/1024/1024:.1f} MB/s)", flush=True)
        os.rename(tmp_path, out_path)
        elapsed = time.time() - t0
        print(f"[done] {name}: {written} bytes in {elapsed:.1f}s", flush=True)
        return True
    except Exception as e:
        print(f"[!!] {name}: error: {e}", flush=True)
        if os.path.exists(tmp_path):
            os.unlink(tmp_path)
        return False


def main() -> int:
    os.makedirs(WORK_DIR, exist_ok=True)
    cf = get_cf_clearance()
    print(f"[ok] cf_clearance: {cf[:30]}...{cf[-10:]}", flush=True)
    rc = 0
    for name, url in URLS:
        if not download(name, url, cf):
            rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
