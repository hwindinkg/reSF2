#!/usr/bin/env python3
"""Fix cross-platform compile errors for Linux GCC builds.

fopen_s() is MSVC Annex-K only; Linux GCC does not have it.
Fix: replace with std::fopen() which is POSIX/C99/C++11 standard.
Idempotent -- safe to run multiple times.
"""
import sys

FIXES = [
    {
        "file": "app/game/main.cpp",
        "old": (
            '        std::FILE* out = nullptr;\n'
            '        if (fopen_s(&out, path.c_str(), "wb") != 0 || out == nullptr) {'
        ),
        "new": (
            '        std::FILE* out = std::fopen(path.c_str(), "wb");\n'
            '        if (out == nullptr) {'
        ),
    },
    {
        "file": "core/scene/fight.cpp",
        "old": (
            '        if (fopen_s(&pose_dump_file_, pose_dump_path_.c_str(), "wb") != 0 ||\n'
            '            pose_dump_file_ == nullptr) {'
        ),
        "new": (
            '        pose_dump_file_ = std::fopen(pose_dump_path_.c_str(), "wb");\n'
            '        if (pose_dump_file_ == nullptr) {'
        ),
    },
]

errors = 0
for fix in FIXES:
    path = fix["file"]
    try:
        with open(path, "r", encoding="utf-8") as f:
            content = f.read()
    except FileNotFoundError:
        print(f"[skip] {path}: not found (repo root mismatch?)")
        continue
    if fix["new"] in content:
        print(f"[ok]   {path}: already fixed")
        continue
    if fix["old"] not in content:
        print(f"[warn] {path}: old pattern not found -- may already be fixed or changed")
        continue
    content = content.replace(fix["old"], fix["new"], 1)
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"[fix]  {path}: fopen_s -> std::fopen (Linux GCC compatible)")

if errors:
    sys.exit(1)
print("fix_compile.py: done")
