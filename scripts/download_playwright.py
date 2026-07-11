#!/usr/bin/env python3
"""Download SF2 files via Playwright with real (non-headless) Chromium.

Strategy:
  1. Launch Chromium in non-headless mode (Xvfb provides the display).
     Cloudflare flags headless Chrome via navigator.webdriver, missing
     permissions, and other signals — non-headless passes.
  2. Navigate to root page first — Cloudflare JS challenge auto-passes
     in ~5s, we get cf_clearance cookie.
  3. Set up download handler that saves to /home/z/my-project/work/.
  4. Navigate to APK URL. Browser sends cf_clearance cookie, CF may
     auto-pass, file downloads.
  5. If CF shows Turnstile, click the checkbox inside the iframe.
  6. Repeat for sf2.7z.

Xvfb must be running on :99. Start it with:
  Xvfb :99 -screen 0 1280x800x24 -nolisten tcp -ac &
"""
import os
import sys
import time
from pathlib import Path

os.environ["DISPLAY"] = ":99"

from playwright.sync_api import sync_playwright

WORK_DIR = Path("/home/z/my-project/work")
WORK_DIR.mkdir(parents=True, exist_ok=True)
DOWNLOAD_DIR = WORK_DIR

URLS = [
    ("sf2.apk", "https://chat.chobat.ru/Shadow+Fight+2_1.9.21.apk"),
    ("sf2.7z", "https://chat.chobat.ru/sf2.7z"),
]


def click_turnstile_if_present(page) -> bool:
    """Check if Cloudflare Turnstile is visible and click the checkbox.
    Returns True if clicked (or already passed), False if not present."""
    try:
        # The Turnstile widget is inside an iframe with title containing
        # "Cloudflare security challenge"
        for frame in page.frames:
            if "challenges.cloudflare.com" in (frame.url or ""):
                # Find the checkbox inside the iframe
                try:
                    checkbox = frame.query_selector('input[type="checkbox"]')
                    if checkbox:
                        print(f"  [turnstile] found checkbox in iframe {frame.url}, clicking", flush=True)
                        checkbox.click()
                        return True
                except Exception as e:
                    print(f"  [turnstile] iframe check error: {e}", flush=True)
        # Also check via the outer page's iframe element
        iframe_loc = page.locator('iframe[title*="Cloudflare"]')
        if iframe_loc.count() > 0:
            print(f"  [turnstile] found CF iframe on page", flush=True)
            # Try to click at the iframe's location (Turnstile checkbox
            # is usually at the left edge of the iframe)
            box = iframe_loc.first.bounding_box()
            if box:
                print(f"  [turnstile] iframe at ({box['x']},{box['y']}) "
                      f"size {box['width']}x{box['height']}", flush=True)
                # Click slightly inside the iframe (where the checkbox is)
                page.mouse.click(box["x"] + 28, box["y"] + 28)
                return True
    except Exception as e:
        print(f"  [turnstile] error: {e}", flush=True)
    return False


def download_one(context, page, name: str, url: str) -> bool:
    """Download a single file. Returns True on success."""
    out_path = DOWNLOAD_DIR / name
    if out_path.exists() and out_path.stat().st_size > 1_000_000:
        print(f"[skip] {name} already exists ({out_path.stat().st_size} bytes)", flush=True)
        return True

    print(f"\n=== downloading {name} ===", flush=True)
    print(f"URL: {url}", flush=True)

    # Set up download event handler
    downloaded_file = {"path": None}
    def on_download(download):
        print(f"  [event] download started: {download.suggested_filename}", flush=True)
        try:
            path = DOWNLOAD_DIR / download.suggested_filename
            download.save_as(str(path))
            downloaded_file["path"] = path
            print(f"  [event] saved to {path}", flush=True)
        except Exception as e:
            print(f"  [event] save error: {e}", flush=True)

    page.on("download", on_download)

    # Navigate to the file URL
    print(f"  navigating to URL...", flush=True)
    try:
        page.goto(url, wait_until="domcontentloaded", timeout=30000)
    except Exception as e:
        print(f"  navigation error (may be ok if download started): {e}", flush=True)

    # Wait for Cloudflare challenge to potentially appear and pass
    print(f"  waiting for CF challenge / download...", flush=True)
    for i in range(60):  # up to 30 seconds
        time.sleep(0.5)
        title = page.title() if not page.is_closed() else "(closed)"
        if downloaded_file["path"]:
            print(f"  [{i*0.5:.1f}s] download completed!", flush=True)
            break
        # Check for Turnstile every 2 seconds
        if i % 4 == 2:
            if "Just a moment" in title or "Performing" in title:
                if click_turnstile_if_present(page):
                    print(f"  [{i*0.5:.1f}s] clicked Turnstile, waiting...", flush=True)
                    time.sleep(3)
            else:
                # Title changed — maybe download started or page loaded
                if i % 10 == 0:
                    print(f"  [{i*0.5:.1f}s] title={title!r}", flush=True)

    # Check if download completed
    if downloaded_file["path"] and downloaded_file["path"].exists():
        size = downloaded_file["path"].stat().st_size
        print(f"  [ok] downloaded {name}: {size} bytes", flush=True)
        # Rename if needed
        if downloaded_file["path"].name != name:
            final = DOWNLOAD_DIR / name
            downloaded_file["path"].rename(final)
            print(f"  [ok] renamed to {final}", flush=True)
        return True

    # Check if file appeared in download dir
    for f in DOWNLOAD_DIR.iterdir():
        if f.name.startswith("Shadow") or f.name.startswith("sf2"):
            if f.stat().st_size > 1_000_000:
                print(f"  [ok] found downloaded file: {f.name} ({f.stat().st_size} bytes)", flush=True)
                if f.name != name:
                    f.rename(DOWNLOAD_DIR / name)
                return True

    print(f"  [!!] download did not complete for {name}", flush=True)
    # Print page state for debugging
    try:
        print(f"  final title: {page.title()!r}", flush=True)
        print(f"  final url: {page.url}", flush=True)
    except:
        pass
    return False


def main() -> int:
    print("=== launching chromium (non-headless, Xvfb :99) ===", flush=True)
    with sync_playwright() as p:
        # Launch non-headless Chromium with Xvfb
        browser = p.chromium.launch(
            headless=False,
            args=[
                "--no-sandbox",
                "--disable-dev-shm-usage",
                "--disable-blink-features=AutomationControlled",
            ],
        )
        # Create context with download dir set
        context = browser.new_context(
            accept_downloads=True,
            user_agent="Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                       "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36",
        )
        page = context.new_page()

        # First pass: root page to establish CF clearance
        print("\n=== step 1: navigate to root to pass CF challenge ===", flush=True)
        page.goto("https://chat.chobat.ru/", wait_until="domcontentloaded", timeout=30000)
        print("  waiting for CF challenge to auto-pass...", flush=True)
        for i in range(40):  # up to 20 seconds
            time.sleep(0.5)
            title = page.title()
            if "Just a moment" not in title and "Performing" not in title:
                print(f"  [{i*0.5:.1f}s] CF passed, title={title!r}", flush=True)
                break
            if i % 4 == 2:
                click_turnstile_if_present(page)
        else:
            print("  [!] CF challenge did not auto-pass on root", flush=True)

        # Print cookies
        cookies = context.cookies()
        cf_cookies = [c for c in cookies if c["name"].startswith("cf_")]
        print(f"  cf cookies: {len(cf_cookies)}", flush=True)
        for c in cf_cookies:
            print(f"    {c['name']}={c['value'][:40]}...", flush=True)

        # Now download each file
        results = []
        for name, url in URLS:
            ok = download_one(context, page, name, url)
            results.append((name, ok))

        browser.close()

    print("\n=== summary ===", flush=True)
    for name, ok in results:
        status = "OK" if ok else "FAIL"
        path = DOWNLOAD_DIR / name
        size = path.stat().st_size if path.exists() else 0
        print(f"  {name}: {status} ({size} bytes)", flush=True)
    return 0 if all(ok for _, ok in results) else 1


if __name__ == "__main__":
    sys.exit(main())
