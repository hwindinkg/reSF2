#!/usr/bin/env python3
"""Re-download sf2.7z properly — handle 'Download is starting' exception."""
import os
import sys
import time
import struct
from pathlib import Path

os.environ["DISPLAY"] = ":99"

from playwright.sync_api import sync_playwright

WORK_DIR = Path("/home/z/my-project/work")


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
        old = WORK_DIR / "sf2.7z"
        if old.exists():
            old.unlink()

        # Set up download handler that saves to file when download completes
        out_path = WORK_DIR / "sf2.7z"
        download_done = {"obj": None, "error": None}

        def on_download(download):
            print(f"  [event] download started: {download.suggested_filename}", flush=True)
            download_done["obj"] = download
            try:
                # save_as blocks until download completes
                download.save_as(str(out_path))
                print(f"  [event] save_as completed: {out_path.stat().st_size} bytes", flush=True)
            except Exception as e:
                download_done["error"] = e
                print(f"  [event] save_as error: {e}", flush=True)

        page.on("download", on_download)

        # Navigate — this raises "Download is starting" which is expected
        try:
            page.goto("https://chat.chobat.ru/sf2.7z", timeout=60000)
            print("  goto returned normally (unexpected)", flush=True)
        except Exception as e:
            if "Download is starting" in str(e):
                print("  goto raised 'Download is starting' (expected)", flush=True)
            else:
                print(f"  goto error: {e}", flush=True)

        # Wait for download to complete (up to 5 minutes for 46MB)
        print("  waiting for download to complete (up to 5 min)...", flush=True)
        for i in range(300):
            time.sleep(1)
            if out_path.exists():
                size = out_path.stat().st_size
                # Check if size is stable (no growth for 3 seconds)
                time.sleep(3)
                new_size = out_path.stat().st_size
                if new_size == size and size > 0:
                    print(f"  [{i}s] download stable at {size} bytes", flush=True)
                    break
                if i % 10 == 0:
                    print(f"  [{i}s] size={new_size} bytes (growing...)", flush=True)
        else:
            print("  [!!] download did not stabilize", flush=True)

        if not out_path.exists():
            print("  [!!] file not created", flush=True)
            browser.close()
            return 1

        size = out_path.stat().st_size
        print(f"\n  final size: {size} bytes", flush=True)

        # Verify 7z header
        SIG7Z = bytes([0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c])
        with open(out_path, "rb") as f:
            magic = f.read(6)
            f.seek(12)
            next_off = struct.unpack("<Q", f.read(8))[0]
            next_size = struct.unpack("<Q", f.read(8))[0]
        magic_ok = "OK" if magic == SIG7Z else "BAD"
        print(f"  magic: {magic.hex()} ({magic_ok})", flush=True)
        print(f"  next_header_offset: {next_off}", flush=True)
        print(f"  next_header_size: {next_size}", flush=True)
        needed = 32 + next_off + next_size
        print(f"  file needs >= {needed} bytes, has {size}", flush=True)
        if size < needed:
            print(f"  WARNING: file truncated!", flush=True)

        browser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
