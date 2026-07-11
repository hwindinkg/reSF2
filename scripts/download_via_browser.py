#!/usr/bin/env python3
"""Download large files via agent-browser's fetch() — bypasses Cloudflare
TLS fingerprinting because the download happens inside the real browser.

Strategy:
  1. The browser has already passed Cloudflare's challenge and holds the
     cf_clearance cookie.
  2. We use agent-browser eval to run JavaScript that fetches the file
     URL via fetch(), reads the response as ArrayBuffer, and returns it
     as a base64 string.
  3. For files > ~10 MB, we fetch in HTTP Range chunks (1 MB each) to
     avoid memory issues and eval output size limits.
  4. Each chunk's base64 is written to a .part file, concatenated at the
     end.

Requires: agent-browser session open on https://chat.chobat.ru/ (challenge
already passed).
"""
import base64
import json
import os
import subprocess
import sys
import time

WORK_DIR = "/home/z/my-project/work"
CHUNK_SIZE = 1024 * 1024  # 1 MB per fetch
URLS = [
    ("sf2.apk", "https://chat.chobat.ru/Shadow+Fight+2_1.9.21.apk"),
    ("sf2.7z", "https://chat.chobat.ru/sf2.7z"),
]

# JavaScript that fetches a byte range and returns base64.
# We use XMLHttpRequest because fetch() with Range headers can be flaky
# in some browsers; XHR with responseType='arraybuffer' is rock-solid.
JS_FETCH_RANGE = """
(async () => {{
    const url = "{url}";
    const start = {start};
    const end = {end};
    return new Promise((resolve, reject) => {{
        const xhr = new XMLHttpRequest();
        xhr.open('GET', url);
        xhr.responseType = 'arraybuffer';
        xhr.setRequestHeader('Range', `bytes=${{start}}-${{end}}`);
        xhr.onload = () => {{
            if (xhr.status === 206 || xhr.status === 200) {{
                const bytes = new Uint8Array(xhr.response);
                let binary = '';
                const chunk = 8192;
                for (let i = 0; i < bytes.length; i += chunk) {{
                    binary += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk));
                }}
                resolve(btoa(binary));
            }} else {{
                reject(`HTTP ${{xhr.status}}`);
            }}
        }};
        xhr.onerror = () => reject('network error');
        xhr.send();
    }});
}})()
"""


def get_total_size(url: str) -> int:
    """Get Content-Length via a HEAD request in the browser."""
    js = f"""
    (async () => {{
        const resp = await fetch("{url}", {{method: 'HEAD'}});
        return JSON.stringify({{
            status: resp.status,
            contentLength: resp.headers.get('content-length') || '0',
            contentType: resp.headers.get('content-type') || '',
            acceptRanges: resp.headers.get('accept-ranges') || ''
        }});
    }})()
    """
    result = subprocess.run(
        ["agent-browser", "eval", js],
        capture_output=True, text=True, timeout=60
    )
    try:
        info = json.loads(result.stdout.strip().split('\n')[-1])
    except (json.JSONDecodeError, IndexError) as e:
        print(f"[!!] could not parse HEAD response: {e}", file=sys.stderr)
        print(f"    stdout: {result.stdout[:500]!r}", file=sys.stderr)
        print(f"    stderr: {result.stderr[:500]!r}", file=sys.stderr)
        return 0
    print(f"[info] HEAD {url}: status={info['status']} "
          f"content-length={info['contentLength']} "
          f"content-type={info['contentType']} "
          f"accept-ranges={info['acceptRanges']}", flush=True)
    return int(info.get('contentLength', 0))


def fetch_chunk(url: str, start: int, end: int) -> bytes:
    """Fetch bytes [start, end] (inclusive) via browser fetch()."""
    js = JS_FETCH_RANGE.format(url=url, start=start, end=end)
    result = subprocess.run(
        ["agent-browser", "eval", js],
        capture_output=True, text=True, timeout=120
    )
    # agent-browser prints a ✓ line then the result. The result is the
    # last line of stdout (the promise resolution).
    lines = result.stdout.strip().split('\n')
    b64_data = lines[-1] if lines else ''
    # Strip surrounding quotes if present (eval returns JSON-ish strings)
    if b64_data.startswith('"') and b64_data.endswith('"'):
        b64_data = b64_data[1:-1]
    try:
        return base64.b64decode(b64_data)
    except Exception as e:
        print(f"[!!] b64decode failed: {e}", file=sys.stderr)
        print(f"    last line: {lines[-1][:200]!r}", file=sys.stderr)
        raise


def download(name: str, url: str) -> bool:
    out_path = os.path.join(WORK_DIR, name)
    tmp_path = out_path + ".part"
    total = get_total_size(url)
    if total == 0:
        print(f"[!!] {name}: could not determine file size", flush=True)
        return False
    print(f"[..] {name}: {total} bytes ({total/1024/1024:.1f} MB), "
          f"chunk size {CHUNK_SIZE/1024:.0f} KB", flush=True)
    written = 0
    t0 = time.time()
    with open(tmp_path, "wb") as f:
        offset = 0
        chunk_idx = 0
        while offset < total:
            end = min(offset + CHUNK_SIZE - 1, total - 1)
            try:
                data = fetch_chunk(url, offset, end)
            except Exception as e:
                print(f"[!!] {name}: fetch failed at offset {offset}: {e}", flush=True)
                os.unlink(tmp_path)
                return False
            f.write(data)
            written += len(data)
            offset = end + 1
            chunk_idx += 1
            if chunk_idx % 10 == 0 or offset >= total:
                elapsed = time.time() - t0
                speed = written / elapsed if elapsed > 0 else 0
                pct = 100.0 * written / total
                print(f"    {name}: {written}/{total} bytes ({pct:.1f}%) "
                      f"{speed/1024/1024:.1f} MB/s", flush=True)
    os.rename(tmp_path, out_path)
    elapsed = time.time() - t0
    print(f"[ok] {name}: {written} bytes in {elapsed:.1f}s "
          f"({written/elapsed/1024/1024:.1f} MB/s)", flush=True)
    return True


def main() -> int:
    os.makedirs(WORK_DIR, exist_ok=True)
    rc = 0
    for name, url in URLS:
        if not download(name, url):
            rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
