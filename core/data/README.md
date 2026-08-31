# core/data — asset & data layer

Portable C++17 modules for loading the Shadow Fight 2 web-game assets.
No platform-specific code lives here (that goes in `platform/` later).

## Modules

| Module | Purpose |
|--------|---------|
| `zstd_stream` | Decompress a complete zstd frame (RFC 8478) into a buffer. |
| `xml_doc` | pugixml wrapper: parse UTF-8 XML text (BOM-tolerant) + attribute helpers. |
| `xml_archive` | Reader for the `xml.dat` container format (see below). |
| `anim_archive` | Reader for the animation clip format inside `animations.*.dat` (see below). |

## The xml.dat container format

`reference/www/res/xml.9e0b4b10.dat` (226,646 bytes) is a zstd-compressed
archive of XML files. After decompression (5,494,525 bytes) the container is a
simple sequential archive — **not** Haxe-serialized:

```
Offset  Size  Field
------  ----  ---------------------------------------------------------
0       2     u16 LE  file count (41 in xml.9e0b4b10.dat)
              ── per file, repeated `count` times ──
+0      1     u8   name length (bytes)
+1      N     name bytes (UTF-8, archive-relative path, e.g. "res/moves.xml")
+N      3     u24 LE data size (3 bytes, little-endian)
+N+3    M     data bytes (the raw XML text)
```

Byte-level example (first entry of xml.9e0b4b10.dat):

```
29 00                count = 0x0029 = 41
15                   name_len = 21
72 65 73 2F 75 73 65 72 73 5F 64 65 66 61 75 6C 74 2E 78 6D 6C
                     "res/users_default.xml"
E7 04 00             size = 0x0004E7 = 1255
3C 3F 78 6D 6C ...   "<?xml version=\"1.0\"?>" ... (1255 bytes)
```

Notes:
- All integers are little-endian. The size field is 24-bit (max 16 MiB per file).
- The archive parses exactly to the end of the decompressed buffer (no trailer).
- The game's `Ja.Mda(archive, name)` (JS_MAP §7.1) extracts a named file from
  this container; `Ja.ki(id)` parses the XML text with `Wg.parse`.
- The same container layout is used by the other `.dat` archives
  (`models.*.dat`, `animations.*.dat`, ...) — verified per-file (Phase 2b).

## The animations.*.dat clip format

`reference/www/res/animations.b22c72ff.dat` (asset 1355, `Ja.Dka`) and
`animations_dojo.3314a7de.dat` (asset 1354) are zstd-compressed containers of
the same layout as `xml.dat` (u16 count + per-file name/size/data). Each entry
is one named move clip (e.g. `air_axe_kick`, `axe_stance_idle`) whose bytes are
parsed by the game's `Vlb` method (the animation data holder feeding the `Te`
animation controller, JS_MAP §7.3). **This is NOT Haxe serialization** — the
game build ships no `haxe.Unserializer`; the clip is a custom binary format:

```
Version 1 (564 of 566 clips in animations.b22c72ff.dat):
  u8   version = 1
  u8   frame count
  per frame:
    u16  bone count
    per bone: 3 x i16 LE (x, y, z)  ->  position (x/16, -y/16, z/16)

Version 0 (2 clips: ranged_blaster_bulet, tentacle_ability):
  u8   version = 0
  u32  frame count
  per frame:
    u8   skipped (unused flag byte)
    u32  bone count
    per bone: 3 x f32 LE (x, y, z)  ->  position (x, -y, z)
```

- Layout is frame-major: `frames[f].bones[b]` = position of bone `b` at frame
  `f`, matching the game's `Te.Kk[frame][bone]` access (`Te.Xqb`, L566).
- The `y` component is negated by the game (`new H(x/16, -(y/16), z/16)`).
- Bone names are NOT stored in the clip — bones are indexed; names live in the
  model XML (`<Nodes>` elements of `models.*.dat`).
- Duration = frame count / 60 s (the game's fixed 60 Hz update step).
- `anim_clip_parse` (anim_archive) implements both versions and validates that
  the clip parses exactly to the entry size.

## The models.*.dat format

`reference/www/res/models.473fd74f.dat` (asset 315, `Ja.Ra`) and
`models_dojo.e57366a0.dat` (asset 314) are zstd-compressed containers of the
same layout as `xml.dat`. Each entry is a named ragdoll model (e.g.
`mdl_armor_alloy`, `mdl_body`) whose data is **plain XML text** parsed by the
game's `Yc.parse` (JS_MAP §7.3):

```
<Scene>
  <Nodes>    <NAME X=.. Y=.. Z=.. Type="Node"|"MacroNode" Mass=.. .../>
             MacroNodes carry ChildNode1..4 + LCC1..4 (local child coords)
  <Edges>    <NAME End1=.. End2=.. Length=.. Radius=.. Type="Edge"|"Muscle" .../>
  <Figures>  <NAME Type="Capsule" Edge=.. .../>   (collision capsules)
             <NAME Type="Triangle" Node1=.. Node2=.. Node3=.. .../>  (mesh)
```

- "Bones" = `<Nodes>` elements (Type="Node" + Type="MacroNode").
- "Mesh" = `<Figures Type="Triangle">` (each references 3 nodes by name).
- The fighter's visual mesh and physics body are both built from this XML
  (`Yc.load` → `Yc.parse`, L289191).

## Decode chain (verified, Phase 2b)

All five `.dat` archives in `reference/www/res/` are **plain zstd frames** —
decompress the whole file, then parse the container. There is **no XOR/decrypt
layer** on these files:

- The game loads assets 0/314/315/1354/1355 via `Ja.Vxb()` with
  `Og.GI` = `(new si).read(a)` (zstd only, JS_MAP §7.2).
- The `oy.vza` wrapper (zstd + SHA-256 XOR) is applied only to assets in
  `G.$Ua = [1317, 1316]` (JS L2389-2390), which are not the animation/model
  archives. Its algorithm, for reference: last 7 bytes = [u24 LE payload
  size][flag byte][3 unused]; payload = bytes [size-(d+7), size-7); if flag
  bit 0 is set, XOR each payload byte with the ASCII hex characters of
  SHA-256(prefix [0, size-(d+7))), cycling 64.
- Empirically verified: whole-file zstd decompression succeeds for all five
  files (xml 5,494,525 B; animations 8,835,082 B; models 39,451,606 B;
  animations_dojo 417,960 B; models_dojo 140,944 B).

## Extraction

`xml_archive_extract(entries, out_dir)` writes every entry under `out_dir`
preserving the archive-relative path, e.g. `res/moves.xml` →
`<out_dir>/res/moves.xml`. The asset_explorer tool extracts to
`reference/extracted/xml/` (gitignored).

## The move system (moves.xml → MoveDef → condition evaluator)

The fight engine's action system lives in `res/moves.xml` (1048 `<Move>`
elements) and is parsed/evaluated by the game JS
(`reference/www/sf2.502f0946.js`). Native port: `core/scene/move_def.*`
(parser) + `core/scene/conditions.*` (evaluator), probed by `app/move_probe`.

### How the game parses moves.xml (JS study)

Entry point is `Fa.parse` (static) → `Fa.Ueb` (the `<Moves>` list) and
`Fa.xbb` (per-move sub-objects). JS refs: class `Fa` g="13C" (`Fa.Ueb`,
`Fa.dMa`, `Fa.xbb`, `Fa.H3`, `Fa.LIa`, `Fa.Hib`, `Fa.hjb`, `Fa.djb`,
`Fa.CIa`, `Fa.HIa`).

- **Move attributes** (`Fa.Ueb`): `Name`, `ID`, `FileName` (with `.bytes`
  stripped → `Eza`), `MidFrames`→`XJ`, `FirstFrame`→`qx`, `EndFrame`→`Lj`,
  `Priority`, `NoMagicRecharge`, `NoWallRepulsion`, `WallRepulsion`,
  `StyleFactor`→`RNa`, `Physics`→`MS`, `EndsStage`→`yda`, `Looped`,
  `NoInterpolationFrames`, `NoAnimation`, `AlignOnParentWallCollision`,
  `MirrorNode`→`J2` (Grb), `CameraCOMAlignStage`, `TacticWeapon`→`Gsb`,
  `TacticEquivalent` (registered into a list, resolved later to the target
  move's animation name), `Type` ("MOVE"→`EAnimationMove`,
  "ATTACK"→`EAnimationAttack`), `Profile` (→ `Ru`, the shop/menu icon list).
- **Template inheritance** (`Fa.dMa`): the `Template="A|B|C"` string is
  split on `|`; each tag resolves against the `<Templates><Template Name=..>`
  table (`Fa.kxb`). The template's **Conditions / Locks / Intervals / Align /
  SetDirection** are merged into the move (templates may inherit other
  templates, `Fa.dMa` recurses). Native: `collect_templates` + merge.
- **Conditions** (`Fa.H3` → `Tl.create`): each child of `<Conditions>` is
  dispatched by name (JS `sa.oe`):
  RoundStage=1, Keys=4, Distance=2, Direction=21, Weapon=5, Player=6,
  Health=7, Operator=8, CurrentInterval=9, CurrentAnimation=10,
  PhysicsFrameNumber=11, RoundResult=12, Item=13, Perk=15, Bullets=14,
  Birth=16, Name=17, Screen=18, ModelMirrored=19, ModExists=20,
  BattleType=22, BossAbilityState=23, Hit=24, ModelExists=25.
  `Operator` (And/Or) recurses via `Tl.J3`.
- **Intervals** (`Fa.LIa`): each `<Interval>` maps via `fe.G0`
  (0=other, 2=Uninterrupt, 3=SelfUninterrupt, 4=Attack, 5=Block,
  6=Invulnerable, 7=Invisible). Attack (`Ul`) additionally parses
  AttackingParts (`<Edge Name>`), Hit, Impulse (X/Y/Z), Damage
  (`<Damage Value=..><Damage Type=.. Shift=..>`), Combo Time.
- **Locks** (`Fa.HS`): `<Locks><Item Type SubType/>` (or `Operator Type="Or"`
  wrapping items).
- **Align** (`Fa.Hib`→`Fa.jva`), **SetDirection** (`Fa.hjb`→`Fa.Zca`),
  **Actions** (`Fa.CIa`), **Events** (`Fa.HIa`), **Transitions**
  (`Fa.Cxb`), **Shop** (`Fa.Mub`).

### Condition types + evaluation semantics (JS `Ha` g="125" + subclasses)

Base `Ha`: `Player` attr (Me=1, Enemy=2, ... via `Nd.ol`) and `Not="1"`
(`cb`); `Nba` flips the result. All conditions are **And-ed** at the move
level (`jc.nw`); `Operator` nodes provide Or/And nesting.

| Type (class) | JS | Semantics |
|---|---|---|
| Keys (`vm`) | g="131" | Buffered keys match the `<Key Type PressType/>` list. Tap/Hold/Release. If `Ae.gm` (hold mode) only held keys match; else all three sets. |
| Distance (`qm`) | g="12C" | Axis X (signed `to.x-from.x`, scaled by `Wl`), Y, or 3D (`sqrt(dx²+dy²)`); `Min <= v <= Max`. From/To are `ee` object refs (Player/Object/Part). |
| Weapon (`Hm`) / Player (`Hm`) | g="13D" | My (or Enemy, via Player attr) items have Type+SubType+Name. |
| Health (`rm`) | g="12D" | `current/max` ratio in [Min,Max]. |
| Operator (`wm`) | g="132" | And (all) / Or (any); `Not` flips the whole group. |
| CurrentInterval (`tm`) | g="12F" | An active interval on the fighter matches by Name and/or Type (Attack/Block/Invulnerable). |
| CurrentAnimation (`lg`) | g="12A" | The animation name is in the fighter's current animation set (`Ae.XH`/`z_`/`G3`/`oZ`/`A_` per Player). `$Move` = the candidate move's name; `$NoAnimation$` = empty set; no Name = physics flag. |
| PhysicsFrameNumber (`Cm`) | g="138" | `Nd.frameCount` in [Min,Max] (unset = -1). |
| RoundResult (`Fm`) | g="13B" | Victory/Defeat + Timeout/Ringout. |
| Item (`um`) | g="130" | My items match Type+SubType+Name. |
| Bullets (`lp`) | g="2C1" | Bullet count (MagicBullet/RaidChargeBullet) in [Min,Max]. |
| Perk (`Bm`) | g="137" | Perk by Name in my/enemy perk lists. |
| MagicCharge (`sp`) | g="2C2" | Magic charge in [Min,Max]. |
| ModExists (`tp`) | g="2C3" | Mod name in the fighter's mod set (+ optional Namespace). |
| Pain (`vp`) | g="2BE" | Pain value in [Min,Max]. |
| Round (`yp`) | g="2C0" | Round number == Number. |
| InTheArea (`qp`) | g="2C4" | Fighter is in the arena. |
| Random (`xp`) | g="2B6" | `(Chance/100) < random()`. |
| PerkStart (`wp`) | g="2C5" | Always true. |
| Name (`Am`) | g="136" | Fighter model name == Value. |
| Screen (`Gm`) | g="13C" | Screen enum == Name (Fight/Profile/Shop*). |
| ModelMirrored (`zm`) | g="135" | Fighter is mirrored. |
| BattleType (`lm`) | g="126" | Battle type == Value. |
| BossAbilityState (`nm`) | g="128" | Value flag. |
| Hit (`sm`) | g="12E" | Last-hit Type/Name match. |
| ModelExists (`ym`) | g="134" | Model by name exists. |
| Combo (`mp`) | g="2B9" | Combo counter in [Min,Max]. |
| Style (`Ap`) | g="2B8" | Style enum (Turtle..Crazy) in [Min,Max]. |
| Direction (`pm`) | g="12B" | Facing sign matches From/To direction. |
| Birth (`mm`) | g="127" | Fighter's birth name == Name. |

Elements actually used in moves.xml (verified by scan): CurrentAnimation
(2636), CurrentInterval (1865), Operator (1403), Key (780), Distance (507),
RoundStage (477), Keys (450), ModExists (343), Item (178), Player (76),
Hit (64), RoundResult (52), Bullets (29), BattleType (22), Health (18),
Perk (12), BossAbilityState (9), Direction (7), PhysicsFrameNumber (7),
Birth (6).

### Interval system (JS `fe` g="150", `Ul` g="151")

`<Interval Name=.. Type=.. Start=.. End=..>` — Start/End are 1-based
animation frames. `fe.init`: `start = Start` (default 0);
`finish = End` if present else `pva+2` (pva = the move's `EndFrame`).
The name overrides the type: `Name=="Unstable"`→1, `"Uninterrupt"`→2,
`"SelfUninterrupt"`→3, else `fe.G0(Type)` (Attack=4, Block=5, Invisible=7,
Invulnerable=6, 0=other). `Ul` (Attack) adds: AttackingParts (edge names),
Hit (name + Start/End), Impulse (X/Y/Z), Damage (Value, NoCritical, sub
Damage Type/Shift), Combo (Time). At runtime the fighter tracks active
intervals (`Ae.xb`), and `CurrentInterval` conditions test membership.

### Move selection (JS `wd` g="?" fighter, `de` move-finder, `Gc` fight)

- The fighter's move list `HB` is built from the equipped items: each
  equipped item contributes its `<Item>`/`Weapon` moves (the `me` set), and
  `Naa` pushes an opponent's moves onto the list. `Zka` sets `jb = HB[0]`.
- `wd.V1(a)` (test one move `a`): the fighter's current-move set must
  contain `a` (`me`), the candidate's animation list goes into `Ae.xK`,
  then `a.Yz(...)` runs the move's **conditions** (`jc.Yz` → `Ha.Sea` →
  `he`). `de.ia` iterates the moves and `jL` returns the chosen one.
- Input (keys) is buffered by `wd.Kl` (`zl` class): `Sgb` (press) fills
  `zg.sh` (Tap), `Xgb` (release) fills `zg.released` and updates `Fh`
  (Hold); `Lea` snapshots it into `Ae.keys`. A move's `Keys` condition
  matches against that buffer (`vm.he` via `zd.$ga`).
- Priority: `Fa.Ueb` reads `Priority` into `jc.priority`; the move set is
  kept sorted by priority (highest first) and `Zka` picks `HB[0]`. The
  "1key/2key/3key" templates encode input complexity (one/two/three key
  presses) and the `Central/Forward/Back/Up/Down` tags encode direction.

### Native port status

`core/scene/move_def.*` + `core/scene/conditions.*` implement the parser
and the evaluator with the JS semantics above (per-type comments cite the
JS class + g-id). `FightContext` carries the state the conditions read
(current animation set, active intervals, round stage, buffered keys, mods,
items, distance, health, ...). `app/move_probe` loads moves.xml, verifies
the HighPunch contract, and evaluates HighPunch's conditions against two
contexts. Types implemented: Keys, Distance, Weapon, Player, Health,
Operator, CurrentInterval, CurrentAnimation, PhysicsFrameNumber,
RoundResult, Item, Bullets, Perk, MagicCharge, ModExists, Pain, Round,
InTheArea, Random, PerkStart, Name, Screen, ModelMirrored, BattleType,
BossAbilityState, Hit, ModelExists, Combo, Style, Direction, Birth —
all 25 `Tl`/`sa.oe` types (some as unconstrained stubs where the native
fight state does not exist yet; noted in conditions.cpp).