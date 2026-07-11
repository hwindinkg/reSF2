#!/usr/bin/env python3
"""Download ONLY sf2.7z with generous timeout and robust completion check."""
import os
import sys
import time
import struct
from pathlib import Path

os.environ["DISPLAY"] = ":99"

from playwright.sync_api import sync_playwright

WORK_DIR = Path("/home/z/my-project/work")
OUT_PATH = WORK_DIR / "sf2.7z"


def main() -> int:
    if OUT_PATH.exists():
        size = OUT_PATH.stat().st_size
        if size > 90_000_000:
            print(f"[skip] {OUT_PATH} already exists ({size} bytes)", flush=True)
            return 0
        OUT_PATH.unlink()

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

        # Download sf2.7z — use expect_download context manager
        print("\n=== step 2: download sf2.7z ===", flush=True)
        try:
            with page.expect_download(timeout=120000) as dl_info:
                try:
                    page.goto("https://chat.chobat.ru/sf2.7z", timeout=120000)
                except Exception as e:
                    if "Download is starting" in str(e):
                        pass  # expected
                    else:
                        raise
            download = dl_info.value
            print(f"  download started: {download.suggested_filename}", flush=True)
        except Exception as e:
            print(f"  [!!] expect_download failed: {e}", flush=True)
            browser.close()
            return 1

        # Wait for the download to complete and get the temp path
        print("  waiting for download to complete (up to 5 min)...", flush=True)
        temp_path = None
        last_size = 0
        stable_count = 0
        for i in range(300):
            try:
                # download.path() returns the path once download completes,
                # or None if still in progress
                p_ = download.path()
                if p_:
                    temp_path = Path(p_)
                    cur_size = temp_path.stat().st_size
                    if cur_size == last_size and cur_size > 0:
                        stable_count += 1
                        if stable_count >= 3:
                            print(f"  [{i}s] download stable at {cur_size} bytes", flush=True)
                            break
                    else:
                        stable_count = 0
                        last_size = cur_size
                        if i % 10 == 0:
                            print(f"  [{i}s] downloading... {cur_size} bytes", flush=True)
                else:
                    if i % 15 == 0:
                        print(f"  [{i}s] download.path()=None (still in progress)", flush=True)
            except Exception as e:
                if i % 15 == 0:
                    print(f"  [{i}s] waiting... ({e})", flush=True)
            time.sleep(1)

        if not temp_path or not temp_path.exists():
            print("  [!!] download did not complete", flush=True)
            browser.close()
            return 1

        # Copy BEFORE closing browser
        import shutil
        print(f"  copying {temp_path} → {OUT_PATH}", flush=True)
        shutil.copy2(str(temp_path), str(OUT_PATH))
        size = OUT_PATH.stat().st_size
        print(f"  copied: {size} bytes", flush=True)

        browser.close()

    # Verify 7z header
    SIG7Z = bytes([0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c])
    with open(OUT_PATH, "rb") as f:
        magic = f.read(6)
        f.seek(12)
        next_off = struct.unpack("<Q", f.read(8))[0]
        next_size = struct.unpack("<Q", f.read(8))[0]
    magic_ok = "OK" if magic == SIG7Z else "BAD"
    needed = 32 + next_off + next_size
    print(f"\n=== verification ===", flush=True)
    print(f"  magic: {magic.hex()} ({magic_ok})", flush=True)
    print(f"  next_header_offset: {next_off}", flush=True)
    print(f"  next_header_size: {next_size}", flush=True)
    print(f"  file size: {size}", flush=True)
    print(f"  needed: {needed}", flush=True)
    print(f"  VALID: {size >= needed}", flush=True)
    return 0 if size >= needed else 1


if __name__ == "__main__":
    sys.exit(main())
