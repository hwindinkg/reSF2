#!/usr/bin/env python3
"""Download SF2 files using curl_cffi — impersonates Chrome's TLS fingerprint
to bypass Cloudflare's bot detection.

cloudscraper failed because Cloudflare now uses TLS fingerprinting (JA3/JA4)
in addition to JS challenges. curl_cffi uses the real libcurl with BoringSSL
patches to mimic Chrome's exact TLS handshake, which Cloudflare accepts.
"""
import os
import sys
import time

from curl_cffi import requests

WORK_DIR = "/home/z/my-project/work"
URLS = [
    ("sf2.apk", "https://chat.chobat.ru/Shadow+Fight+2_1.9.21.apk"),
    ("sf2.7z", "https://chat.chobat.ru/sf2.7z"),
]


def download(name: str, url: str) -> bool:
    out_path = os.path.join(WORK_DIR, name)
    tmp_path = out_path + ".part"
    print(f"[..] downloading {name} from {url}", flush=True)
    try:
        # impersonate="chrome131" makes curl_cffi use Chrome 131's exact
        # TLS fingerprint (JA3/JA4) and HTTP/2 settings. This is what
        # Cloudflare checks to distinguish real browsers from bots.
        resp = requests.get(url, impersonate="chrome131", stream=True, timeout=120)
        if resp.status_code != 200:
            print(f"[!!] {name}: HTTP {resp.status_code}", flush=True)
            ct = resp.headers.get("content-type", "?")
            print(f"     content-type: {ct}", flush=True)
            if "text/html" in ct:
                text = resp.text[:300]
                print(f"     body: {text!r}", flush=True)
            return False
        cl = resp.headers.get("content-length", "?")
        ct = resp.headers.get("content-type", "?")
        print(f"[ok] {name}: HTTP 200, content-length={cl}, content-type={ct}", flush=True)
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
    rc = 0
    for name, url in URLS:
        if not download(name, url):
            rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
