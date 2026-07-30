# Runtime map — S3E game code (verified 2026-07-30)

Blocker "game code not found at runtime" is **resolved**. Several addresses in
the old notes were wrong; corrections below.

## 1. The region

```
0x8F056000  ---s   4KB     /dev/zero   image base (guard)
0x8F057000  rwxs   8.18MB  /dev/zero   game code + data   <-- 8.5MB ARM
0x8F884000  ---s   4KB     /dev/zero   guard
```

Why every earlier scan missed it: the mapping is **shared** (`rwxs`), and
`Process.enumerateRanges('rwx'|'r-x'|'--x')` did not report it. It is plainly
visible in `/proc/<pid>/maps`. All scripts now parse `/proc/self/maps`.
No `android_dlopen_ext` / custom-mmap theory needed.

ASLR moves the base each launch — always resolve it, never hardcode.

## 2. Runtime <-> file mapping (derived, not assumed)

Verified by locating live byte patterns in `ShadowFight2_android.bin`
(`find_runtime_offset.py`, 5/5 samples agree):

```
file_offset = (runtime_addr - 0x8F057000) + 0x3E6F1
runtime_addr = file_offset - 0x3E6F1 + 0x8F057000
```

`0x3E6F1` is not arbitrary: PLT table (`0x6B60` bytes) + `0x3E6F1` lands
exactly on the header's `codeOffset` = `0x45251`. Consistency check passes.

Dump of the live region: `reverse/binaries/game_region_runtime.bin`
(8572928 bytes, 0 unreadable pages). It is the **relocated** image, so branch
targets and imports resolve — prefer it over the static file for analysis.

Diff vs static file: 96.44% identical, 3.56% changed in 3740 runs = loader
relocations + live state.

## 3. PLT — corrected

| | old notes | verified |
|---|---|---|
| entries | 1395 | **1718** |
| entry size | 12 bytes | **16 bytes** |
| location | `0x4A0000C8` | **region start + 0x0**, ends `+0x6B60` |
| GOT formula | `0x4A7FEA30 + N*8` | not used; see below |

Stub shape (16 bytes):
```
+0x0  E59FC000   LDR R12, [PC, #0]
+0x4  E59FF000   LDR PC,  [R12]
+0x8  <target in libs3e_android.so or an extension .so>
+0xC  <common fixup, 0x961FF82C>
```
262 resolve to named exports, 637 to non-exported internals, 819 are unbound
lazy slots (target 0). Map: `reverse/analysis/plt_map.json`.

Key indices: `s3eDeviceYield` #513 (`+0x2010`), `s3eDeviceRegister` #508
(`+0x1FC0`), `s3eTimerGetMs` (`+0x29F0`), `s3eDeviceCheckQuitRequest`
(`+0x1F10`), `s3eKeyboardUpdate` (`+0x2470`), `s3ePointerUpdate` (`+0x2630`).

## 4. Second import band (the 12-byte stubs)

`game+0x6B60 .. +0x8000` holds PC-relative thunks — this is what the notes
described as "the PLT". Game code calls **these**, never the 16-byte stubs,
which is why scanning for branches to `s3eDeviceYield` found 0 callers.

```
ADD IP, PC, #hi
ADD IP, IP, #mid
LDR PC, [IP, #lo]!     ; GOT slot
```

Resolved (`resolve_thunks.py`):

| thunk | API |
|---|---|
| `game+0x6C88` | s3eAccelerometerGetZ |
| `game+0x6D00` | **s3eDeviceYield** |
| `game+0x6E98` | s3eAccelerometerGetY |
| `game+0x73FC` | s3eKeyboardUpdate |
| `game+0x7474` | s3eDeviceCheckQuitRequest |
| `game+0x78A0` | s3eAccelerometerGetX |
| `game+0x7B4C` | s3ePointerUpdate |
| `game+0x7B94` | s3eTimerGetMs |

**Do not `Interceptor.attach` these thunks.** They are PC-relative; the
trampoline corrupts the `ADD IP, PC` math and the hook silently does nothing.
Hook the 16-byte PLT stubs or the libs3e exports instead.

## 5. Main loop — corrected

Old notes: `0x4A679F54`. At the verified mapping that address is bitstream/
decompression code, not a loop. It was an artifact of the wrong file offset.

**Real main loop: `game+0x64400`** (this session `0x8F0BB400`), found by
hooking the yield PLT stub and reading LR. LR returned `game+0x64434` and
`game+0x644F0`; those are *return addresses*, so the two `BL` call sites are
`game+0x64430` and `game+0x644EC` — both inside one function starting at
`game+0x64400`.

Frame timing object, captured live from `r8` at the yield site:

```
this+0x00  vtable    0x8F82CE58
this+0x08  interval  16  (uint64)   -> 62.5 fps cap
this+0x0C  0
```

`LDRD r4, r5, [r8, #8]` reads that 64-bit interval; the loop compares elapsed
`s3eTimerGetMs` against it and yields the remainder. Fixed timestep, 16 ms.

Structure:
```
game+0x64400  push {r4-r8,lr}; sub sp,#0x10
              if (this->vtable[0x08]() == 0) return 0      ; beq +0x64504

game+0x64420  ; ---- frame top ----
              t0 = s3eTimerGetMs()          ; 64-bit, r0:r1 -> r6:r7
              s3eDeviceYield(0)             ; BL @0x64430, pump events
              s3eKeyboardUpdate()
              s3ePointerUpdate()
              ctx = game+0x143C8()          ; singleton accessor
              r4 = ctx->vtable[0x50]()
              a = s3eAccelerometerGetX/Y/Z()
              game+0x64230(r4, ax, ay, az, t0)   ; step, ints -> /1000.0
              if (s3eDeviceCheckQuitRequest() != 0) goto +0x6450C
game+0x644B0  ctx = game+0x143C8()
              ctx->vtable[0x2C]()           ; frame work (render)

game+0x644C0  ; ---- inner wait loop (NOT the frame top) ----
              elapsed  = s3eTimerGetMs() - t0        ; 64-bit subs/sbc
              interval = this->[0x08]                ; ldrd r4,r5
              if (interval <= elapsed) goto +0x64420 ; bls, budget spent
              remaining = (t0 + interval) - s3eTimerGetMs()
              if (remaining < 0) goto +0x64420       ; bmi
              s3eDeviceYield(remaining)              ; BL @0x644EC
              goto +0x644C0                          ; re-check, spin

game+0x644F4  this->vtable[0x08](); return -1         ; mvn r0,#0
game+0x6450C  ctx = game+0x143C8()
              if (ctx->[0x34] == 0) goto +0x644F4    ; shutdown
              game+0x14870(); goto +0x644B0          ; quit deferred
```

Two details worth keeping straight:

- The wait at `+0x644C0` is an **inner spin loop**: after yielding the
  remaining time it branches back to `+0x644C0`, re-reads the clock and
  yields again until the interval is consumed. It does not fall through to
  the frame top. `interval` is re-read from `this+0x08` every iteration, so
  changing it mid-frame takes effect immediately.
- The comparison is `cmp r5,r1 / cmpeq r4,r0 / bls`, i.e. the branch to the
  next frame is taken when **interval <= elapsed** — not the reverse.

`game+0x64230` converts its int arguments to double and divides by a literal
at `0x8F0BB2B0`, verified to be exactly `1000.0` (`0x408F4000_00000000`), so
the per-frame values are passed in **seconds as double**.

## 6. The loop is not on the main thread

Main thread is idle (~17 CPU ticks/4s). The loop runs on **"Thread-2"**
(557 ticks/4s), the only thread ever sampled inside the game region.

This explains the old "s3eDeviceYield only fires in battle" note: hooks that
recorded zero calls were watching the wrong thread / a paused state. In the
dojo with a dialog open the loop is throttled and yield traffic is minimal.

## 7. Callback registration

`s3eDeviceRegister` is PLT #508 (`+0x1FC0`). The old claim that the frame
callback is `0x4A679914` rests on the disproven mapping and should be
re-derived by hooking that stub at spawn:
`frida -U -f com.nekki.shadowfight -l register_capture_spawn.js`.

## 8. Tooling

| script | purpose |
|---|---|
| `frida_hooks/locate_game_code.js` | find region + static->runtime table |
| `frida_hooks/dump_game_region.js` | dump the 8.18MB relocated image |
| `frida_hooks/resolve_plt.js` | resolve 1718 stubs -> `plt_map.json` |
| `frida_hooks/trace_yield_caller.js` | find loop via yield-stub LR |
| `frida_hooks/sample_game_thread.js` | identify the game-loop thread |
| `analysis/find_runtime_offset.py` | derive the file<->runtime slide |
| `analysis/correlate_runtime_dump.py` | header, diff, PLT discovery |
| `analysis/disasm_runtime.py` | disassemble with API annotations |
| `analysis/find_callers.py` | call-graph scan over the dump |
| `analysis/resolve_thunks.py` | thunk -> S3E API name |

Load `game_region_runtime.bin` in Ghidra as raw ARM LE at base `0x8F057000`.

## 9. Corrections to prior notes (summary)

| claim in old notes | status |
|---|---|
| game code absent from runtime memory | **wrong** — `rwxs /dev/zero` mapping, visible in `/proc/pid/maps` |
| loaded via `android_dlopen_ext` / custom mmap | **unnecessary** — plain shared mapping |
| main loop `0x4A679F54` | **wrong** — that offset is bitstream code; real loop `game+0x64400` |
| frame callback `0x4A679914` | **unverified** — derived from the bad mapping, re-derive |
| callback dispatch `0x4A6798B0` | **unverified** — same reason |
| PLT 1395 entries x 12 bytes @ `0x4A0000C8` | **wrong** — 1718 x 16 bytes at region+0 |
| GOT `0x4A7FEA30 + N*8` | **not the mechanism** — 16-byte stub holds target at +8 |
| 12-byte `ADD R12,PC` stub is the PLT | it is the **second** band, `game+0x6B60..0x8000` |
| `s3eDeviceYield` only called in battle | **wrong** — called every frame; earlier zero counts were the wrong thread/state |
| S3E `codeOffset 0x45251` | **correct**, and it corroborates the derived slide |

Still open: frame-callback / dispatch addresses, and whether registration
happens at spawn (needs `-f` spawn capture).
