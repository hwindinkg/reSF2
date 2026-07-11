#!/usr/bin/env python3
"""Download SF2 APK and game data via cloudscraper (bypasses Cloudflare challenge)."""
import sys
import os
import time

import cloudscraper

WORK_DIR = "/home/z/my-project/work"
URLS = [
    ("sf2.apk", "https://chat.chobat.ru/Shadow+Fight+2_1.9.21.apk"),
    ("sf2.7z", "https://chat.chobat.ru/sf2.7z"),
]


def download(name: str, url: str, scraper) -> bool:
    out_path = os.path.join(WORK_DIR, name)
    tmp_path = out_path + ".part"
    print(f"[..] downloading {name} from {url}", flush=True)
    try:
        # cloudscraper handles the Cloudflare challenge transparently on
        # the first request; subsequent requests reuse the session cookies.
        resp = scraper.get(url, stream=True, timeout=60)
        if resp.status_code != 200:
            print(f"[!!] {name}: HTTP {resp.status_code}", flush=True)
            print(f"     content-type: {resp.headers.get('content-type')}", flush=True)
            print(f"     first 200 chars: {resp.text[:200]!r}", flush=True)
            return False
        cl = resp.headers.get("content-length", "?")
        ct = resp.headers.get("content-type", "?")
        print(f"[ok] {name}: HTTP 200, content-length={cl}, content-type={ct}", flush=True)
        if "text/html" in ct:
            print(f"[!!] {name}: got HTML instead of binary (Cloudflare not bypassed)", flush=True)
            return False
        written = 0
        t0 = time.time()
        with open(tmp_path, "wb") as f:
            for chunk in resp.iter_content(chunk_size=1024 * 1024):
                if chunk:
                    f.write(chunk)
                    written += len(chunk)
                    if written % (10 * 1024 * 1024) == 0 or written < 1024 * 1024:
                        elapsed = time.time() - t0
                        speed = written / elapsed if elapsed > 0 else 0
                        print(f"    {name}: {written / 1024 / 1024:.1f} MB "
                              f"({speed / 1024 / 1024:.1f} MB/s)", flush=True)
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
    # Create a single scraper session — cloudscraper solves the CF challenge
    # on the first request and reuses the cf_clearance cookie for subsequent
    # requests to the same domain.
    scraper = cloudscraper.create_scraper(
        browser={"browser": "chrome", "platform": "windows", "mobile": False}
    )
    rc = 0
    for name, url in URLS:
        if not download(name, url, scraper):
            rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
