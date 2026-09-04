# FONT_METRICS — BMF advances, line heights, pixel formula (web build)

Parsed from disk (read-only) for Stream 3's `draw_ui_label`. Files:
`reference/www/res/ui/font-en.7043b83b.fnt` (2136 B),
`reference/www/res/fight/digits.c9e1eb7a.fnt` (337 B),
`reference/www/res/fight/round.e85f44ab.fnt` (336 B).
Companion art: `font-en.2dfae7e9.png`, `digits.86d1056c.png`,
`round.c9bacdb4.png` (same dirs).

## 1. Format (as the engine reads it)

Standard AngelCode BMF v3 (`BMF\x03`, L1670 `tq.ek` validates magic +
version 3) with two quirks that break naive parsers:

1. **Block-type bytes are unreliable** — `font-en` carries wrong type
   bytes (e.g. common block tagged `01`). The engine **discards** every
   type byte (`a.ea()`) and reads blocks strictly positionally
   (info → common → pages → chars → kerning). Parse the same way.
2. **Info fixed part is 14 bytes, not 13** (one extra byte vs spec);
   the engine consumes 14 (`Zd,ea,ea,ie,ea ×8`) then skips `size-14`
   (name). Net effect: none for metrics (all padding `0x01` here).

Field order per block (verified against `tq.ek`, L1670-1672):
info: `size=i16(fontSize 100/90/140, abs applied)`, padding ignored;
common: `lineHeight, base, scaleW, scaleH, pages` (all u16) + 5 skip
bytes → `lineHeight=max(common, size)`;
chars: `count=size/20`, records
`id:u32, x,y,w,h:u16, xo,yo,xa:i16, page,chnl` (Xj keeps 8, drops
page/chnl); kerning: `(first,second):u32, amount:i16`.

## 2. Per-font headers (all parses land exactly, trail 0, kerning 0)

| Font | size (`eF`) | lineHeight | base | scale | pages | ids |
|---|---|---|---|---|---|---|
| font-en | 100 | **126** | **81** | 512² | 1 (`-`) | 104 (32..8230) |
| digits | 90 | **138** | **99** | 256² | 1 (`digits_0.png`) | 13 |
| round | 140 | **216** | **155** | 544×256 | 1 (`round_0.png`) | 13 |

Baseline = `base` (81/99/155). No explicit ascender/descender fields
exist in BMF — conventional reading: ascender ≈ base, descender ≈
lineHeight − base (45/39/61). The engine uses `lineHeight`/`base`
directly (`lx`, L1672-1673; `RQ`/`ew`, L1635).

## 3. `font-en` advances, ASCII 32–126 (`id: xadvance`)

```
32:15 33:22 34:31 35:45 36:38 37:60 38:51 39:16 40:29 41:29
42:33 43:40 44:18 45:24 46:20 47:31 48:41 49:41 50:41 51:41
52:41 53:41 54:41 55:41 56:41 57:41 58:19 59:19 60:34 61:40
62:34 63:30 64:68 65:48 66:43 67:47 68:55 69:39 70:37 71:52
72:55 73:22 74:22 75:49 76:37 77:72 78:57 79:58 80:42 81:58
82:46 83:38 84:42 85:54 86:47 87:70 88:45 89:42 90:47 91:28
92:31 93:28 94:39 95:33 96:22 97:38 98:42 99:35 100:42 101:39
102:26 103:40 104:43 105:21 106:21 107:41 108:21 109:64 110:43
111:42 112:42 113:42 114:28 115:32 116:28 117:43 118:38 119:56
120:39 121:37 122:34 123:27 124:18 125:27 126:44
```

All 95 present (plus 9 extra ids up to 8230, e.g. ellipsis — out of scope
for ASCII labels). Cap check: H=55, A=48, M=72.

## 4. `digits` / `round` advances (`id: xadvance`)

digits: ` :19 /:38 0:72 1:30 2:50 3:49 4:59 5:47 6:57 7:47 8:56 9:58 ::29`
round: ` :30 /:60 0:111 1:47 2:77 3:76 4:92 5:74 6:88 7:73 8:88 9:91 ::46`

## 5. Pixel formula (engine, L1627/L1634-1635)

```
px = xadvance × (fontSize / eF)          # eF = 100 / 90 / 140 above
linePx = lineHeight × (fontSize / eF)    # ew(): × qc.ij × nha × xp
heightPx ≈ fontSize / eF × Bc            # RQ(); Bc = base
```

Advance accumulates per glyph (`Qh.apply`, L1627); no kerning shipped
(empty `cFa` map — step skipped); unknown ids fall back to image glyphs
(`Zea`, L1634) or are skipped (65533). Cap-height lookup prefers
`HEIM`/A–Z then Cyrillic (`Kza`, L1635).

Stream-3 cross-check: their ~24 px/cap-char ≈ `55 × 44/100` — i.e.
`fontSize≈44` (or scale 0.44 at `fontSize=eF`). Table units are em-100;
multiply, don't re-measure.

## Ambiguities (explicit)

1. `pages` name of font-en decodes as `-` (single-page index 0 is all
   that matters; name unused by the engine).
2. `Opb`/`Kpb` (`scaleW/H` position) are stored on `lx` but never enter
   the width/height formulas above — treated as opaque.
3. Descender/ascender split is conventional (base given; `lh-base`
   derived), not engine-asserted.
