# Table formats survey - .tbs/.stb/.sts containers + .atf secondary records (R1)

Scope: phase-5 PLAN step B3 (GAP-4, R1). Static Ghidra analysis only, no engine
changes. All addresses are in the relocated image
`reverse/binaries/game_region_runtime.bin`, base `0x8F057000`, ARM:LE:32:v7,
auto-analyzed (16420 functions). Companion to PORT_GAPS.md tactic-family notes.

No candidate C was produced for any function (string/xref/flow-level survey per
the R1 contract), so no @re-verifier round was required. R2 (.atf record
semantics) owns the byte-level follow-up.

---

## 0. TL;DR - per-family verdict

| Family (PORT_GAPS layout) | Verdict | Live container |
|---|---|---|
| `attack/*.tbs` | **DEAD format** in this build. Dir + ext strings exist in rodata but **zero code references** anywhere in the dump. | `.atf` (zlib `78 DA`) via the unified loader |
| `shift/*.stb` | **DEAD format** - same evidence class as `.tbs`. | `.atf` secondary records |
| `shiftTables/*.sts` | **DEAD as a standalone file**. Shift tables themselves are ALIVE, but ship as **binary node-table sub-records inside `.atf`** (entry-header value 7 = nested archive), parsed by `FUN_8f446528`. Binary u32 rows, **not** zlib-per-file, **not** XML. | `.atf` secondary records (tag 7) |
| `dodge/` | Never a file family at all (dir string dead; no ext string exists). Dodge tables register from the unified `.atf` stream. | `.atf` |
| `movements/` | Same as dodge. | `.atf` |
| `outcometablesforattack/` | Same as dodge. | `.atf` |

**Secondary-records answer: YES.** Every tactic table family is packed inside
`assets/tactics/*.atf` (single-name) and `assets/tactics/<a>_<b>.atf`
(weapon-pair) archives - the only files the dump ever opens for tactics. The
`.atf` payload is a **multi-entry archive**: a header value per entry
(engine-visible `version` 1/2) selects the tactic-table record parser; header
value **7** marks a **nested archive** of binary node tables (shift tables).
The engine's `binary_records` span (`engine/reverse/atf_tactics.hpp:88`) is the
tail of the decompressed payload after the first 858-byte record + string pool -
i.e. where these secondary entries live. Byte-level framing of entries inside
the decompressed stream is R2 scope (see section 5 caveat).

---

## 1. Method & addressing model (how the claims were made)

The dump is PIC ARM code. String/data references are **not** absolute pointers;
they use literal-pool dwords consumed as:

```
ldr  rX, [pool]      ; pool dword = (target - (add_site + 8))
add  rX, pc, rX      ; pc = add_site + 8   <-- PC+8 trap: PC belongs to the ADD
```

Verified per-site against disassembly, e.g. `FUN_8f450250`:
`0x8f45027c: add r3,pc,r3` with pool `0x8f4509c4 = 0x00347c84` gives
`0x8f450284 + 0x00347c84 = 0x8F797F08` = `"assets/tactics/"`.
In decompiler output this shows as `DAT_<pool> + <negative constant>`; the
stored pool dword alone is a stale pre-relocation value - **byte-searching the
dump for absolute string addresses finds nothing** (confirmed: zero hits for
both runtime and link-base forms of every family path string).

Reference recovery, two independent methods:

1. `get_xrefs_to` on defined strings (works only where Ghidra created data
   items - `.atf`/`assets/tactics/` resolved, the family strings did not).
2. Whole-image scan of all 1,675,003 instructions for the `ldr`+`add/sub pc`
   pattern (24-instr pairing window, register-clobber bail-out), computing
   effective targets. Scanner validated by **positive controls**: it finds
   exactly the manually-verified sites for `assets/tactics/` (0x8f45027c,
   0x8f4505e4, 0x8f4508dc, 0x8f450a14, 0x8f450c30, 0x8f450e24), `.atf`
   (0x8f450594, 0x8f450be4), `file %s not unzip` (0x8f451048, 0x8f451754),
   `Reading - table %s/%s !` (0x8f4515cc), `Skipping - table %s/%s !`
   (0x8f4517dc), and 257 string-island targets total.

Negative claims below rest on BOTH methods returning nothing (plus the two
byte-pattern searches). This is as strong as static evidence gets.

---

## 2. String island map (rodata anchors)

Tactic path/ext strings (all ASCII, all in the `0x8F797xxx` island):

| Addr | String | Referenced? |
|---|---|---|
| `0x8F7978A8` | `assets/tactics/attack/` | **NO** (dead) |
| `0x8F7978C0` | `.tbs` | **NO** (dead, undefined data) |
| `0x8F7979B0` | `assets/tactics/shift/` | **NO** (dead) |
| `0x8F7979AC` | `.stb` | **NO** (dead, undefined data) |
| `0x8F797EE4` | `assets/tactics/shiftTables/` | **NO** (dead) |
| `0x8F797F00` | `.sts` | **NO** (dead, undefined data) |
| `0x8F797E88` | `assets/tactics/movements/` | **NO** (dead) |
| `0x8F797EA4` | `assets/tactics/outcometablesforattack/` | **NO** (dead) |
| `0x8F797ECC` | `assets/tactics/dodge/` | **NO** (dead) |
| `0x8F797F08` | `assets/tactics/` | YES - filename builders |
| `0x8F797F18` | `.atf` | YES - filename builders |
| `0x8F797F20` | `zip error` | **NO** (legacy of the old zip loader) |
| `0x8F797F4C` | `file %s not unzip` | YES - both `.atf` loaders' open-fail path |
| `0x8F797F60` | `Skipping - table %s/%s !` | YES - `FUN_8f4514d8` |
| `0x8F797F7C` | `Reading - table %s/%s !` | YES - `FUN_8f4514d8` entry loop |
| `0x8F797888` | `Skipped load tactics: %s - %s` | YES - `FUN_8f442a84` |
| `0x8F797958` | `heel %s not found in shift table for %s` | YES - `FUN_8f445ba0` |
| `0x8F797980` | `count % 4 != 0` | YES - `FUN_8f446528` assert |
| `0x8F797990` | `count % nodeCount != 0` | YES - `FUN_8f446528` assert |
| `0x8F798734`/`0x8F79873C` | `NHeel_1` / `NHeel_2` | YES - heel node names |
| `0x8F7977BC`/`D0`/`EC`/`34` | `Available tables:` / `  outcometablesforattack:` / `  movementsTable:` / `  dodgeTable:` | YES - `FUN_8f442a84` debug dump |
| `0x8F7979E4` | `Strange tactic type: %s` | YES - type dispatch (`0x8F44A890`) |
| `0x8F7979DC` | `Tabular` | YES - decision-type name |

Table-type name array (pointers, `0x8F819210`): indexed by
`FUN_8f43f2c8(type)` as `array[type + 2]`:

```
[-2] RandomAnimation   [-1] NoneTable        [0] AttackTable
[ 1] MovementsTable    [ 2] DodgeTable       [3] AttackTableOld
[ 4] SummaryResultTable[ 5] -> "CautiousMovements" (0x8F79756C) [UNCERTAIN]
[ 6] QuickAttack       [ 7] ShiftTable       [8] ThrowTactics
```

[UNCERTAIN]: the pointer run has 11 slots; PORT_GAPS lists 10 type strings
(the 10 contiguous type-name strings `0x8F7978C8..0x8F797954`). Slot [5]
aliases the tacticSettings key string `CautiousMovements`. Exact enum bound /
whether slot 5 is a real type is R2-scope - the pointer table is ground truth
for code, the string contiguity is ground truth for intent; they disagree by
one slot.

The `0x8F797574..0x8F79834C` region (key schema + tracer decision strings) is
the **tacticSettings.xml parser's** key set (`SafeDodges`, `EmergencyDodges`,
`CautiousMovements`, `EvadeThrowDodges`, `RandomizingEnemyAnimation`,
`MissileAnimations`, `MagicAnimations`, `EvadeUnsafeDodges`, `AttackMoves`,
`DistanceNode`, ...) referenced from `0x8F4413xx..0x8F4416xx`
(inside `FUN_8f4427c4`) - XML config schema, **not** container-format evidence.

---

## 3. The `.atf` loader chain (the only tactic file I/O in the dump)

```
FUN_8f4427c4  (0x8F441988..0x8F4427F7)  tacticSettings.xml parser
  - reads "assets/tacticSettings.xml" (str 0x8F79762C, site 0x8F4419A8),
    builds the tactic-name list (TablesReduction/MovementsTables/AttackTables/
    OutcomeTables/Throws keys) + "loadGame - loading tactics" (0x8F79774C)
        |
        v
FUN_8f442a84  orchestrator (per tactic name)
  - already-loaded check against a global name vector
  - FUN_8f450f90(name)          -> single-name file  assets/tactics/<name>.atf
  - per pair (name,name2): FUN_8f4514d8(name,name2)
                              -> pair file          assets/tactics/<a>_<b>.atf
    (or "Skipped load tactics: %s - %s" when the pair is exempt)
  - debug dump of the three global table registries (0x1C-stride records):
      outcome tables   registry ~0x8F86ED50   "  outcometablesforattack:"
      movements tables registry ~0x8F86FD60   "  movementsTable:"
      dodge tables     registry ~0x8F86ED68   "  dodgeTable:"
    (registry addresses derived from the dump's own pool arithmetic -
     pools 0x8F44312C / 0x8F44313C(+0x8F443140 self-relative) / 0x8F44314C;
     treat as [UNCERTAIN] to one-dword precision)
        |
        v
FUN_8f450f90(param_1=name)                        FUN_8f4514d8(name_a, name_b)
  FUN_8f4509f0 -> "assets/tactics/"+name+".atf"    FUN_8f450250 -> dir+a+"_"+b+".atf"
  FUN_8f2065e4(path, &reader)   archive open; fail -> "file %s not unzip"
  FUN_8f21f2b8(reader)          begin entries
  entry loop: FUN_8f21f3a8 = more?, FUN_8f21f64c = entry header value,
              FUN_8f21f670 = entry name
    |
    |-- header == 7  (only in FUN_8f450f90):
    |     FUN_8f21f458 -> NESTED archive reader
    |     per sub-entry: name via FUN_8f21f670;
    |       FUN_8f45b7e4(name, 1) -> existing table object or 0
    |       FUN_8f446528(obj+0x34, obj, &subreader)   <-- BINARY NODE-TABLE PARSE
    |       (not-found branch builds a temp via FUN_8f445b70 then parses & frees)
    |
    '-- else:
          FUN_8f21f5e4(reader, &blob)      read entry blob
          obj = calloc(0xDC); FUN_8f440b58(obj)      init table object
          FUN_8f44ff08(obj, &blob, header, name)     TACTIC TABLE RECORD PARSE
          FUN_8f43f918(obj, &name, &name)            register into registries
```

Archive-reader family (identity [UNCERTAIN], minizip-flavored by the error
strings): `FUN_8f2065e4` open, `FUN_8f21f2b8` reset/begin, `FUN_8f21f3a8`
more-entries, `FUN_8f21f64c` header value, `FUN_8f21f670` read name,
`FUN_8f21f5e4` read blob, `FUN_8f21f458` open nested, `FUN_8f21f394` blob data
ptr, `FUN_8f21f3f4` advance, `FUN_8f26cc1c` path fixup.

---

## 4. The binary node-table (shift table) sub-record - `FUN_8f446528`

`FUN_8f446528(table+0x34, table, &reader)` (body `0x8F446528..0x8F446743`):

1. `table[9] = table` (context ptr); `total = FUN_8f21f64c(reader)` (remaining
   entry bytes); `used = FUN_8f44638c(table, reader)` (reads header - fills
   the u32 vector at `table+0xC..0x10`, the node-name/id list; `nodeCount` =
   `(table[4]-table[3])>>2`).
2. `rest = total - used`; assert `(rest & 3) == 0`  (`"count % 4 != 0"`,
   site `0x8F446730`)  -> payload is a **u32 array**.
3. `count = rest >> 2`; `count % nodeCount != 0` -> assert
   (`"count % nodeCount != 0"`, site `0x8F446720`, via `FUN_8f733cbc` =
   `__aeabi_uidivmod`, remainder checked).
4. `memcpy` the `rest` bytes from `FUN_8f21f394(reader)` into the u32 vector at
   `table+0x18..0x1C`; advance reader by `rest` (`FUN_8f21f3f4`).
5. `rows = count / nodeCount` (`FUN_8f733ad0` = `__aeabi_uidiv`); resize the
   12-byte-element vector at `table+0..4` to `rows` (element dtor is virtual;
   insert helper `FUN_8f445c30`).
6. Each row element gets `{begin,end}` pointers striding `nodeCount*4` bytes
   through the shared u32 array - i.e. **rows of nodeCount u32 values**, one
   row per table line, node per column.

Companion: `FUN_8f445ba0(obj, name)` looks a node name up in the object's
string vector (`+0xC..+0x10`), returns row index or logs
`"heel %s not found in shift table for %s"` (`0x8F797958`) and returns -1.
Heel node names `NHeel_1`/`NHeel_2` (`0x8F798734/0x8F79873C`) are referenced at
`0x8F45E2A0/0x8F45E2B4`.

This is the live form of what `.sts` files once held: **binary, u32-row,
node-count-strided** - consistent with the assert pair. No XML, no per-file
zlib wrapper (compression happens once at the `.atf` whole-file level).

---

## 5. Secondary-records answer (engine/reverse/atf_tactics.hpp:88)

- The `.atf` file is opened and iterated as a **multi-entry archive** in both
  loaders; entries have: a **header value** (`FUN_8f21f64c`), a **name**
  (`FUN_8f21f670`), and a **blob** (`FUN_8f21f5e4`).
- Header value semantics on the binary side: `7` = nested archive of binary
  node tables (section 4); other values flow as arg3 into `FUN_8f44ff08` -
  matching the engine's `version` field (`kBadVersion` = "version not 1 or 2")
  and the PLAN note that `weapon_a`-alone files are v=2.
- The engine parses entry #1 (858-byte animation-index record + string pool)
  and exposes the remaining decompressed bytes as
  `ParsedTactics::binary_records`. The binary-side loader proves those
  remaining bytes are **structured secondary entries** (named, header-tagged),
  including the tag-7 nested shift-table archive - not padding and not a raw
  blob.
- **Caveat [R2 scope]:** the exact byte framing of entries inside the
  decompressed stream (sizes, alignment, where the tag-7 archive starts) was
  not reversed here; the mapping "`binary_records` == serialized secondary
  entries" is structural, from the loader flow. R2 should pin the framing
  against `FUN_8f21f64c/FUN_8f21f670/FUN_8f21f5e4/FUN_8f21f458`.

---

## 6. Stub-note one-liners (for backend header annotation)

- `tbs_tables.hpp`: "R1: `attack/*.tbs` is a legacy layout - dir+ext strings
  (0x8F7978A8/0x8F7978C0) have zero refs in the dump; attack tables load from
  `.atf` (zlib 78 DA) via FUN_8f450f90/FUN_8f4514d8 -> FUN_8f44ff08. Family
  stays unavailable."
- `stb_tables.hpp`: "R1: `shift/*.stb` is legacy - strings 0x8F7979B0/0x8F7979AC
  dead; no loader in dump. Shift data lives in `.atf` secondary records (see
  sts). Family stays unavailable."
- `sts_tables.hpp`: "R1: standalone `.sts` is dead (0x8F797EE4/0x8F797F00
  unreferenced); shift tables are binary node-table sub-records inside `.atf`
  (entry header 7, nested archive) - u32 rows, count%4==0, count%nodeCount==0,
  parser FUN_8f446528, layout section 4."

---

## 7. Residual uncertainties (all R2-scope, none blocking B2 stubs)

1. Byte-level entry framing inside the decompressed `.atf` payload (section 5
   caveat).
2. `FUN_8f44ff08` record layout / 0xDC-object semantics and the exact mapping
   of entry header values (1/2) to the 858-record versions (R2's core task).
3. Type-name array: 11 pointer slots vs 10 contiguous type strings (section 2,
   slot [5] "CautiousMovements" alias).
4. Which entry names/kinds populate the outcome/movements/dodge registries
   (FUN_8f43f918 internals) - relevant if a future step needs per-family
   lookup rather than the unified `find`.
5. Archive-reader family (`FUN_8f2065e4` etc.) not identified to library
   level; "zip error"/"file %s not unzip" strings suggest minizip lineage,
   and the engine's zlib `78 DA` parse already fixes the outer compression.

---

## 8. Companion: .atf record internals (R2)

R2's byte-level follow-up lives in `ATF_RECORD_858.md` (+ `atf_record_858.candidate.cpp`).
Corrections to this document: the stride-858 model of the first record was a misread
(of `u32 blob_size` + `u16 pool-A count`) - the real framing is repeated
`{version, names, blob_size, blob}` groups; and the probe is an inline per-child
sum in `FUN_8f44ac78` with damage-first order, not a table walk. Verification
status: candidate pending @re-verifier.
