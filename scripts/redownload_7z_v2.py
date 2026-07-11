#!/usr/bin/env python3
"""Download sf2.7z via Playwright, copy from temp location BEFORE closing browser."""
import os
import sys
import time
import shutil
import struct
from pathlib import Path

os.environ["DISPLAY"] = ":99"

from playwright.sync_api import sync_playwright

WORK_DIR = Path("/home/z/my-project/work")
OUT_PATH = WORK_DIR / "sf2.7z"


def main() -> int:
    print("=== launching chromium ===", flush=True)
    with sync_playwright() as p:
        browser = p.chromium.launch(
            headless=False,
            args=["--no-sandbox", "--disable-dev-shm-usage",
                  "--disable-blink-features=AutomationControlled"],
        )
        context = browser.new_context(
            accept_downloads=True,
            user_agent="Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                       "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36",
        )
        page = context.new_page()

        # Pass CF on root first
        print("=== step 1: root page ===", flush=True)
        page.goto("https://chat.chobat.ru/", wait_until="domcontentloaded", timeout=30000)
        for i in range(40):
            time.sleep(0.5)
            title = page.title()
            if "Just a moment" not in title:
                print(f"  CF passed at {i*0.5:.1f}s, title={title!r}", flush=True)
                break

        # Download sf2.7z
        print("\n=== step 2: download sf2.7z ===", flush=True)
        if OUT_PATH.exists():
            OUT_PATH.unlink()

        download_holder = {"obj": None}

        def on_download(download):
            print(f"  [event] download started: {download.suggested_filename}", flush=True)
            download_holder["obj"] = download

        page.on("download", on_download)

        # Navigate — raises "Download is starting" which is expected
        try:
            page.goto("https://chat.chobat.ru/sf2.7z", timeout=60000)
        except Exception as e:
            if "Download is starting" in str(e):
                print("  goto raised 'Download is starting' (expected)", flush=True)
            else:
                print(f"  goto error: {e}", flush=True)

        # Wait for download event — can take 30-60s for large files
        print("  waiting for download event (up to 120s)...", flush=True)
        for i in range(120):
            if download_holder["obj"]:
                print(f"  [{i}s] download event received", flush=True)
                break
            time.sleep(1)
        else:
            print("  [!!] no download event received in 120s", flush=True)
            browser.close()
            return 1

        download = download_holder["obj"]
        print(f"  download object acquired, waiting for completion...", flush=True)

        # Poll for completion via download.path() — returns the temp file path
        # once the download finishes. We can't use save_as() because it may
        # hang or fail; instead we copy the temp file ourselves.
        temp_path = None
        for i in range(300):  # up to 5 minutes
            try:
                # failure_track returns "done" or error state
                state = download.failure
                if state:
                    print(f"  [!!] download failed: {state}", flush=True)
                    browser.close()
                    return 1
                # Try to get the path — only available after completion
                p_ = download.path()
                if p_:
                    temp_path = Path(p_)
                    size = temp_path.stat().st_size
                    print(f"  [{i}s] download.path() returned: {temp_path} ({size} bytes)", flush=True)
                    # Wait a bit more to ensure file is fully written
                    time.sleep(2)
                    final_size = temp_path.stat().st_size
                    if final_size == size:
                        print(f"  [{i}s] file stable at {final_size} bytes", flush=True)
                        break
            except Exception as e:
                if i % 10 == 0:
                    print(f"  [{i}s] waiting... ({e})", flush=True)
            time.sleep(1)
        else:
            print("  [!!] download did not complete in 5 minutes", flush=True)
            browser.close()
            return 1

        if not temp_path or not temp_path.exists():
            print("  [!!] temp file not found", flush=True)
            browser.close()
            return 1

        # COPY the file to final destination BEFORE closing browser
        print(f"  copying {temp_path} → {OUT_PATH}", flush=True)
        shutil.copy2(str(temp_path), str(OUT_PATH))
        size = OUT_PATH.stat().st_size
        print(f"  copied: {size} bytes", flush=True)

        # Verify 7z header
        SIG7Z = bytes([0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c])
        with open(OUT_PATH, "rb") as f:
            magic = f.read(6)
            f.seek(12)
            next_off = struct.unpack("<Q", f.read(8))[0]
            next_size = struct.unpack("<Q", f.read(8))[0]
        magic_ok = "OK" if magic == SIG7Z else "BAD"
        needed = 32 + next_off + next_size
        print(f"  magic: {magic.hex()} ({magic_ok})", flush=True)
        print(f"  next_header_offset: {next_off}", flush=True)
        print(f"  file size: {size}, needed: {needed}", flush=True)
        print(f"  VALID: {size >= needed}", flush=True)

        browser.close()
    return 0 if size >= needed else 1


if __name__ == "__main__":
    sys.exit(main())
