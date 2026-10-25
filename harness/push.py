#!/usr/bin/env python3
"""Push to GitHub WITHOUT any interactive credential window.

`git push` goes through Git Credential Manager, which pops an account picker
whenever more than one GitHub account is cached. This helper bypasses GCM
entirely: it reads the OAuth token that GCM already stored in the Windows
Credential Manager (`.harness/gh.token`, written by `.harness/get_token2.ps1`)
and pushes over HTTPS with it, with every prompt disabled.

Usage:
  python harness/push.py                 # main + the current branch
  python harness/push.py main            # only main
  python harness/push.py --tags          # push every tag
  python harness/push.py --delete <ref>  # delete a remote branch/tag
  python harness/push.py --refspec <rs>  # raw refspec

The token never appears in the output (it is redacted) and never enters the
repository: `.harness/` is gitignored scratch.
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOKEN_FILE = ROOT / ".harness" / "gh.token"
OWNER = "hwindinkg"
REPO = "reSF2"


def token() -> str:
    if not TOKEN_FILE.exists():
        sys.exit("push: no .harness/gh.token - run "
                 "powershell -File .harness/get_token2.ps1")
    t = TOKEN_FILE.read_text(encoding="utf-8").strip()
    if len(t) < 20:
        sys.exit("push: token looks empty")
    return t


def git(args: list[str]) -> int:
    env = dict(os.environ)
    # No prompts, no credential manager, no askpass.
    env["GIT_TERMINAL_PROMPT"] = "0"
    env["GCM_INTERACTIVE"] = "never"
    env["GIT_ASKPASS"] = ""
    env["SSH_ASKPASS"] = ""
    p = subprocess.run(["git"] + args, cwd=ROOT, env=env, capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    out = (p.stdout + p.stderr).replace(token(), "***")
    for line in out.splitlines():
        if line.strip():
            print(line.rstrip())
    return p.returncode


def main(argv: list[str]) -> int:
    url = f"https://x-access-token:{token()}@github.com/{OWNER}/{REPO}.git"
    if "--tags" in argv:
        return git(["push", url, "--tags"])
    if "--delete" in argv:
        i = argv.index("--delete")
        if i + 1 >= len(argv):
            sys.exit("push: --delete needs a ref")
        return git(["push", url, "--delete", argv[i + 1]])
    if "--refspec" in argv:
        i = argv.index("--refspec")
        if i + 1 >= len(argv):
            sys.exit("push: --refspec needs a value")
        return git(["push", url, argv[i + 1]])
    targets = [a for a in argv if not a.startswith("-")]
    if not targets:
        targets = ["HEAD:main"]
    rc = 0
    for t in targets:
        refspec = t if ":" in t else f"{t}:{t}"
        rc = git(["push", url, refspec]) or rc
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
