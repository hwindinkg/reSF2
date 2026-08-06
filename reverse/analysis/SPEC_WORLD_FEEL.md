# SPEC: World feel — crits/knockdown, bag physics, enemy Y/bounds

Binary: `reverse/binaries/game_region_runtime.bin` — ARM:LE:32:v7, image base `0x8F057000`
(game+0x.. offsets relative to base). Data verified against `reverse/data/animations/moves.xml`
(33925 lines), `reverse/data/models/punching_bag.xml`, `reverse/data/stages.xml`,
`assets/locations/<name>/params.xml` (original dojo data).

ARM PC+8 note: all string references were resolved with `reverse/analysis/resolve_dats.py`
(LDR Rn,[PC,#x] + ADD Rn,PC,Rn), never by eyeballing `DAT_8f... + -0x709a...` pairs.

---

## 1. CRITS + KNOCKDOWN

### 1a. Critical-hit mechanic (formula + trigger)

**Settings** (`assets/internalSettings.xml`, parsed by `FUN_8f6567e8`, game+0x5FF7E8;
strings `CriticalHit` @0x8f7966a8, `CriticalHitChance` @0x8f7a4e94, `HeadHitChance` @0x8f7a4ea8):

```xml
<CriticalHit>
  <Probability Base="0.0001" Attribute="CriticalChance"/>
  <Damage     Base="0.0001" Attribute="CriticalDamage"/>
</CriticalHit>
```

- `FUN_8f6634c0` (game+0x60C4C0) parses `<CriticalHit>`: `Probability.Base` (float, default 0)
  → struct[0], `Probability.Attribute` (string) → struct[1..3], `Damage.Base` → struct[4],
  `Damage.Attribute` (string) → struct[5..7]. Struct exposed by getter `FUN_8f66011c`.
- A second, sibling `<CriticalHit><Damage Attribute="CriticalDamage" Base="0.0001"/></CriticalHit>`
  is parsed at game+0x5FEF14 via `FUN_8f65e6f8` (reads attrs `Attribute`+`Base`) into a
  string+float pair exposed by getter `FUN_8f65fcc0` — this is the crit-DAMAGE settings object.

**Chance getter** — `FUN_8f4a610c` (game+0x3EF10C):

```c
// crit_chance(attacker)
if (mode == 0x15 /* tutorial-ish */ && attacker->obj[0x8d] && FUN_8f4b1c90(&attacker->hits) == 0)
    return 0.0f;                       // crits disabled in that mode
s = crit_settings();                    // FUN_8f66011c
if (attr_lookup(attacker->attributes + 0x1c4, s.prob_attr_name, &v) && !s.prob_attr_name.empty())
    return (float)v * s.prob_base;      // CRIT_CHANCE = CriticalChance_attr * 0.0001
return s.prob_base;
```

Attribute map of the fighter lives at `fighter->obj + 0x1c4` (`obj = *(fighter+500)`);
lookup is `FUN_8f2a5f5c(map, name, &out, 1, 0)`. Data: `stages.xml` Template `Default`
has `CriticalChance="1000"` → 1000 × 0.0001 = **0.1 = 10%**; dojo disciples have
`CriticalChance="0"` → 0% (never crit).

**Roll + application** — `FUN_8f4aa998` (game+0x3F3998), the hit-application function:

```c
// ... damage is computed later; the crit flag first:
if (!hit->is_blocking /* +0x1c2 */ && !move->no_critical /* move+0x4c */) {
    float chance = crit_chance(fighter);          // FUN_8f4a610c
    hit->is_critical = roll(chance);              // FUN_8f65bd64 -> FUN_8f264674
}
hit->is_critical  = ...;  /* +0x1c3 */
```

`FUN_8f65bd64(chance)` → `FUN_8f264674` (game+0x1DF674): draws 3 PRNG values
(`FUN_8f264a34`/`FUN_8f264a64`) and returns `K*(r1/r2 + (r3/r4)/r5) < chance*K` — i.e. a
random-ish value compared against the chance (K cancels). Exact distribution of the
`r1/r2+(r3/r4)/r5` sum is [UNCERTAIN] (PRNG internals not resolved), but semantically the
probability scales linearly with `CriticalChance`, per-hit, at damage time.

**Crit damage multiplier** — `FUN_8f4a95a8` (game+0x3F25A8), called from the damage
computation `FUN_8f4a97b4` (game+0x3F27B4):

```c
float crit_multiplier(fighter, is_critical) {
    if (!is_critical) return 1.0f;
    base = crit_dmg_settings().base;              // FUN_8f65fcc0, +0xc == 0.0001
    attr = lookup(fighter->attributes + 0x1c4, crit_dmg_settings().attr_name);
    return powf(2.0f, base * (float)attr);        // FUN_8f72ed40(2.0, x) == 2^x
}
```

So **CRIT_DMG = base_damage × 2^(0.0001 × CriticalDamage_attr)**; `CriticalDamage=0` → 1.0×.

Full damage chain in `FUN_8f4a97b4` (game+0x3F27B4):
`dmg = pow2(attr(DamageFactor)*frictionbase) * crit_mult * FUN_8f4a94f0(block?) * FUN_8f665794(blockfactor) * (move.damage + fighter.bonus)`,
clamped to `[DAT_8f4a9a5c, DAT_8f4a9a60]` (matches `engine/game/damage_formula.hpp`).

**Where the crit reaches the victim**: `FUN_8f4aa998` writes `hit->is_critical` (+0x1c3),
`hit->is_shock` (+0x1c4/0x1c5, from `FUN_8f4a92bc`), records the hit via
`FUN_8f4b173c(victim->hits+0x638, ...)` and stats `FUN_8f4b1c6c` (+0x1c hits / +0x20 crits
counters), and registers the hit record in the global active-hit list (`FUN_8f420f9c`).
The victim's state machine then selects the reaction move using the hit *name*
(`move+0x50` string) and *type* (critical/shock flags).

`super_hit1-5.wav` — NOT a crit marker in data: `moves.xml` plays `super_hit1/2` on specific
attacks only (TitanLowKick @8299, TitanKick @8343, TwoHandedSuperSlash @15120,
ShopTwoHandedSuperSlash @15143, Win_TitanGiantSword @5378). The binary contains no
`super_hit` string (0 matches) — sound names come from moves.xml data. Do not gate crits on
these samples.

### 1b. Knockdown mechanic (trigger + flow)

Reaction moves (moves.xml) that put the fighter on the ground — the *fall family*:

| Move | Line | Sound | Trigger condition (`<Hit Name=X Type=Y/>`) |
|---|---|---|---|
| `HighHitFall` | 6375 | bodyfall3 @8 | High/HighPlus/HighShort/HighShortPlus/HighHeavy/HighHeavyDeflect/HighLong **Type=Critical|Shock**, or WaspFly, TitanHighHeavy, ModExists SteelFootAndConcussion |
| `MiddleHitFall` | 6425 | bodyfall3 @9 | Middle* **Critical|Shock**, TitanMiddleHeavy, SteelFootAndConcussion |
| `SweepHitFall` | 6470 | bodyfall3 @11 | Sweep/Low/SweepHeavy/LowHeavy/Earthquake/TitanSweep **Critical|Shock** |
| `SpinningHitFall` | 6516 | bodyfall1 @9 | Spinning* **Critical|Shock** |
| `OverheadHitFall` | 6549 | bodyfall1 @4 | Overhead* **Critical|Shock** |
| `WallHitFall` | 6587 | — | CurrentInterval `CanWallHitFall` + CurrentAnimation `Fall` (wall-bounce fall) |
| `PhysicalFall` | 6629 | bodyfall1 @1 | Template Physical, `Physics="1"` (ragdoll), Priority 600 |
| `PhysicalFallSuperHit` | 6661 | bodyfall1 @1 | Physics=1, Priority 1000, requires ModExists WhirlEffectPhysicalFall |

So: **a knockdown happens when the received hit is of Type=Critical (crit roll succeeded)
or Type=Shock** — the same crit/shock flags computed in `FUN_8f4aa998`. Template `Fall`
(line 333) = `Hit|NotTitan` → falls are suppressed for titans (`ModExists MOD_TITAN Not=1`),
and `RockOn` blocks the fall reactions (moves 6411/6456). Fall moves carry
`<Interval Name="CanWallHitFall">` (frames during which a wall bounce can chain into
`WallHitFall`) and `Unstable`/`Uninterrupt` windows; recovery is `AfterThrowFall`
(line 591) / `StandupAfterThrowFall` (line 27613) which ends on `AnimationEnd ThrowFall`.

**Binary side of the fall flow**:
- Shock type — `FUN_8f4a92bc` (game+0x3F22BC): per-fighter shock accumulator at +0x670
  (`acc += hit_impulse_scaled`), threshold `Shock.Treshold` from `FUN_8f6601f8` (parsed by
  `FUN_8f662fd0`, game+0x60BFD0: `Treshold`/`HeadHitChance`/`CriticalHitChance`/`Impulse` X/Y/Z);
  head-hit chance and shock-crit chance rolls use the `Shock` settings strings
  (`ShockCriticalHitChance`, `ShockHeadHitChance` attributes in stages.xml Default template:
  0 and 2500). Result flags hit +0x1c4/0x1c5.
- Physical-fall handlers referencing the string `PhysicalFall` @0x8f796604:
  `FUN_8f41ae34` (game+0x3A3E34) and `FUN_8f41ad6c` (game+0x3A3D6C) — they walk the
  active-fighter list and flip fall-related state on fighters matching the caller's
  condition (used by scene/wall systems to force `PhysicalFall`).
- Reaction selection machinery (hit name → reaction move, `<Hit Type=Critical>` condition
  evaluation inside the moves engine) is NOT fully pinned: the hit record carries
  name (move+0x50) and type flags (+0x1c3 crit, +0x1c4/0x1c5 shock); the exact function
  that matches `Type="Critical"` conditions in the moves XML remains
  [UNCERTAIN] — evidence: data side is unambiguous (conditions above), binary side is the
  moves engine around the moves.xml loader `FUN_8f64b8e0` (game+0x3F48E0).

Candidate C++ (reaction condition, per moves.xml):

```cpp
// move selection condition for the FALL reaction moves (HighHitFall etc.)
// binary refs: hit apply FUN_8f4aa998 (game+0x3F3998), flags hit+0x1c3/+0x1c4
// data refs: reverse/data/animations/moves.xml:6375-6668
bool fall_reaction_applies(const HitEvent& hit /* name + type flags */,
                           const Fighter& me) {
    if (me.has_mod("MOD_TITAN")) return false;        // Template Fall = Hit|NotTitan
    if (me.has_mod("RockOn"))    return false;        // moves.xml:6411,6456
    bool crit = (hit.type == HitType::Critical);      // hit+0x1c3 (crit roll)
    bool shock = (hit.type == HitType::Shock);        // hit+0x1c4/0x1c5 (shock roll)
    if (hit.name in {"WaspFly", "TitanHighHeavy", "TitanMiddleHeavy", "Earthquake"}) return true;
    if (me.has_mod("SteelFootAndConcussion")) return true;
    return (crit || shock) && hit.name_matches_family_of(fall_family); // per-move Event list
}
```

---

## 2. BAG PHYSICS (punching bag)

**Data**: `reverse/data/models/punching_bag.xml` — 11 capsule figures on the bag body:
`Capsule_Edge16/17` Radius1=25 (the heavy body), all others Radius1=2 (ropes/links).
`reverse/data/models/skeleton_punching_bag.xml` — the Verlet-style node skeleton.

**Loader**: `FUN_8f674734` (game+0x61F734, ~22KB) reads `assets/models/punching_bag.xml`
(@0x8f7a5ac4) and `assets/models/skeleton_punching_bag.xml` (@0x8f7a5c0c) [READ/PARAM],
builds the bag scene (nodes + capsule figures). Called from `FUN_8f2b2f9c`
(game+0x24BF9C) — the training-scene init (together with `FUN_8f674674`, `FUN_8f64d5b4`).
Capsule figures are parsed by `FUN_8f4a4418` (game+0x3DD418), which consumes the string
`Capsule` @0x8f799f78 — same figure parser used for fighter bodies.

**Hit condition**: there is NO bag-specific hit code in the binary. No `[BAG]`-style
logging strings exist; the only bag strings in the image are the two model paths, the
`btn_punching_bag*.png` UI files and `tutorial_punchbag` (UI button). The bag is hit through
the **generic attack pipeline**: an attack interval's `<AttackingParts><Edge .../></AttackingParts>`
capsules collide with the target's capsule figures (physics engine capsule-capsule
collision, same as vs a fighter); on hit, the move's `<Impulse X=".." Y=".." Z=".."/>`
(moves.xml Template Samples, line 51) is applied to the defender's physics nodes.
The engine's own reverse notes cite the original as
`Kwb() line 15467 creates impulse H(kw,gR,hR,1) from XML <Impulse X/Y/Z>, then strike() applies to defender physics`
(engine/game/game.cpp:4713) — i.e. impulse comes from the move data, applied to the
hit nodes; nothing scales it by a *distance* term.

**Impulse law**: per-edge distribution along the hit point ratio `t` on the target edge
(impulse to end1 = `dir*imp_x*(1-t)`, end2 = `dir*imp_x*t`), exactly what the engine's
non-fallback path reproduces (game.cpp:4724-4729).

**Fallback verdict**: the engine's distance fallback
(`dist_to_bag < 200 → count hit + apply 0.5× impulse to ALL edges`, game.cpp:4796-4839,
tagged "[ORIGINAL]" in the code) has **no counterpart in the binary** — no string, no
constant 200, no branch keyed on player-bag distance exists anywhere in the image
(searched `super_hit`, `BAG`, `punching_bag`, `Verlet`, distance-related strings — none
match). The original only registers a bag hit when a real capsule collision occurs
(generic hit path → `FUN_8f4aa998`), and only then applies the move's impulse. The
fallback is an engine invention and should be removed; a "progress-counting hit" in the
original is by definition a collision hit.

Candidate C++ (bag hit path, original semantics):

```cpp
// binary refs: bag loader FUN_8f674734 (game+0x61F734), capsule parser
//   FUN_8f4a4418 (game+0x3DD418, string "Capsule" @0x8f799f78), hit apply
//   FUN_8f4aa998 (game+0x3F3998); data: reverse/data/models/punching_bag.xml,
//   moves.xml <Impulse X/Y/Z> (Template Samples, line 51)
// NO distance fallback exists in the binary (see NOTES).
bool bag_hit(BagScene& bag, const AttackEdge& atk, Vec2 atk_p, float atk_radius,
             const MoveImpulse& imp, float facing) {
    for (auto& be : bag.collidable_edges) {            // Capsule_Edge16/17 r=25, ropes r=2
        HitRatio t; float sq;
        if (!segment_segment_closest(atk_p, atk_p + atk.delta, be.p1, be.p2, t, sq))
            continue;
        if (sq >= (atk_radius + be.radius) * (atk_radius + be.radius))
            continue;                                  // real capsule contact only
        // original strike(): impulse split by hit ratio t, no distance term
        bag.apply_impulse(be.end1, facing * imp.x * (1.0f - t), imp.y * (1.0f - t));
        bag.apply_impulse(be.end2, facing * imp.x * t,       imp.y * t);
        return true;
    }
    return false;
}
```

---

## 3. ENEMY WORLD-Y AND BOUNDS

### params.xml path & parse
- Path builder `FUN_8f43bdf8` (game+0x3E4DF8): `"assets/" + "locations/" + <location> + "/params.xml"`
  (strings @0x8f7898e4, @0x8f79724c, @0x8f797258). Single caller `FUN_8f43c6f8`.
- Parser `FUN_8f43c6f8` (game+0x3E56F8) — all fields are **attributes of `<Root>`**;
  the attribute parse blocks are at `0x8F43CC1C+` (game+0x3E5C1C+) — the earlier
  `0x3E4E80..0x3E50A0` range is the path-builder's string-append code inside
  `FUN_8f43bdf8`, NOT the parse (VERIFY_W11):

```xml
<Root Music=".." Color="0x281409" Wall="305" Floor="80" PositionY=".." FrictionForce=".."
      Width="1960" Height="560" MinWidth=".." GridSize="..">
  <Layer Type="1" Factor="0.1" Atlas="bg"> <Image .../> </Layer>
  <Layer Type="2" Factor="1"><ModelsViewer PlayerPositionX="690" PlayerPositionY="-93"
       EnemyPositionX="973" EnemyPositionY="-105"/></Layer>
  ...
</Root>
```

| attr | Location offset | parse | notes |
|---|---|---|---|
| Music | +0x18 | vector\<string\> | music id list (`"6|7"`) → registry (SPEC_PRESENTATION Q2) |
| FrictionForce | — (global `FUN_8f633e64`) | float dflt 0 | world friction — NOT a struct field (VERIFY_W11) |
| Wall | +0x34 | float dflt 0 | wall (height) |
| Floor | +0x2c | float dflt 0 | floor level |
| PositionY | +0x30 | float dflt 0 | world Y offset (resolved by VERIFY_W11; distinct from Floor) |
| Color | +0x60..0x62 | hex string → 3 bytes | e.g. "0x281409" → R,G,B (`FUN_8f65aee0`) |
| Width | +0x38 | float dflt 0 | whole world width (XML comment) |
| Height | +0x3c | float dflt 0 | whole world height |
| MinWidth | +0x40 | float dflt = Width | |
| MinWidth/Width | +0x44 | computed | scale factor |
| GridSize | +0x28 | int dflt 0 | |

Children of `<Root>` (layers/decorations) parsed by `FUN_8f43b0d0` (game+0x3E40D0):
element-name dispatch by length (5=`Image`→FUN_8f437fe8, 0xc=`ModelsViewer`→inline,
0xe=…, 0x11=…); `ModelsViewer` block reads 4 float attrs into two Vec2:
**Location+0x48 = player spawn (PlayerPositionX/Y), Location+0x54 = enemy spawn
(EnemyPositionX/Y)**. Layer Type=2 → `Location+0x68 = layer` (the ModelsViewer layer).

### 3a. Enemy world-Y source
`FUN_8f426524` (game+0x3FF524, level constructor): after `FUN_8f43c6f8(Location)` it copies
`Location+0x48` → `player_fighter+0xa0` and `Location+0x54` → `enemy_fighter+0xa0`
(`FUN_8f271ae4(fighter+0xa0, Location+0x48/0x54)`) — the fighters' spawn positions,
then `FUN_8f4d2e70(scene, fighter, FUN_8f4d2a10(scene))` places them.
So **enemy world-Y = `EnemyPositionY` attribute of `<ModelsViewer>` in the `Type="2"`
layer of the location's params.xml** (dojo: −105; player: −93). The engine's enemy Y must
come from this attribute, not from a floor constant.

### 3b/3c. Bounds fields & clamp condition
Fields read by the binary: Width/Height (+0x38/+0x3c) define the *world box* (XML comment:
Width = whole world width), Wall (+0x34) and Floor (+0x2c) define the *fighting walls and
floor* used by the scene physics (wall repulsion: moves.xml `NoWallRepulsion`,
`CanWallHitFall`, `<Distance>` conditions with `Object="Wall" Part="Back|Front"` /
`Object="Floor"`). The fighters are moved by the animation/physics pipeline; bounds are
enforced by the wall/floor collision objects, not by a width/2 clamp.

**Clamp verdict**: the engine's `enemy_pos_x_ = clamp(x, -width/2, +width/2)` with
width=1960 → ±980 (game.cpp:2646-2649, same on player side at 4901) has **no matching
field in params.xml** — 980 is not derivable from Wall=305/Width=1960 by any formula the
binary applies (Width is a *whole-world* size, not an arena half-width). The original
arena limits come from Wall/Floor collision (and wall-hit reactions), applied equally to
both fighters through the shared physics. The clamp's bounds source should be the Wall
field (or the wall collision objects), not Width/2. Exact arena-width function in the
binary is [UNCERTAIN] — the wall object's position is produced by the scene physics
(wall geometry from `<Image ClassName="left/right">` anchors + Wall attribute), whose
arena-width computation was not fully resolved.

Candidate C++ (bounds, original semantics):

```cpp
// binary refs: params.xml parser FUN_8f43c6f8 (game+0x3E56F8) — Wall +0x34,
//   Floor +0x2c, PositionY +0x30, Width +0x38, Height +0x3c, GridSize +0x28;
//   friction_force → global FUN_8f633e64 (game+0x5DCE64), NOT a field;
//   spawns Location+0x48/+0x54
//   (ModelsViewer Player/EnemyPositionX/Y) → fighter+0xa0 via FUN_8f426524
//   (game+0x3FF524); data: assets/locations/dojo/params.xml (Wall=305 Floor=80
//   Width=1960 Height=560)
struct LocationParams {                       // parse offsets from FUN_8f43c6f8
    int   grid_size;                          // +0x28
    float floor;                              // +0x2c  (floor level)
    float position_y;                         // +0x30  (world Y offset)
    float wall;                               // +0x34  (arena wall height)
    float width;                              // +0x38  (whole world width, not arena)
    float height;                             // +0x3c
    float min_width;                          // +0x40  (default: width)
    float min_width_scale;                    // +0x44  (= min_width/width)
    uint8_t color[3];                         // +0x60  (RGB from "0xRRGGBB")
    Vec2  player_spawn;                       // +0x48  (PlayerPositionX/Y)
    Vec2  enemy_spawn;                        // +0x54  (EnemyPositionX/Y)
};
// friction_force is NOT a field — parsed into the global via FUN_8f633e64.
// Music ID list (vector<string>) lives at +0x18 (see SPEC_PRESENTATION Q2).
// fighters spawn at player_spawn/enemy_spawn (fighter+0xa0), then are bound by
// wall/floor collision physics — NOT by clamp(x, ±width/2).
```

---

## NOTES / UNCERTAIN

1. **Crit roll distribution** [UNCERTAIN]: `FUN_8f264674` returns
   `K*(r1/r2+(r3/r4)/r5) < chance*K` with 3 PRNG draws (`FUN_8f264a34/8f264a64`); the
   effective distribution is not pinned (PRNG internals unexamined). Linear-in-attribute
   probability (attr×0.0001) is certain from `FUN_8f4a610c`.
2. **Wall/Floor/PositionY offsets — RESOLVED by VERIFY_W11**: Wall→+0x34,
   Floor→+0x2c, PositionY→+0x30; FrictionForce is **not a struct field** — it
   is parsed into the global via `FUN_8f633e64`. The earlier [UNCERTAIN]
   (decompile order FrictionForce→+0x34 / Wall→+0x2c / Floor→+0x30) was wrong
   on all three; the adjudication used the parse blocks at game+0x3E5C1C+
   (0x8F43CC1C+) — the previously suspected game+0x3E4E80..3E50A0 range is
   the path-builder's string-append code inside `FUN_8f43bdf8`, not the parse.
3. **Reaction selection** (which binary function evaluates `<Hit Type="Critical"/>`
   conditions inside the moves engine) not fully resolved — hit record carries the type
   flags (crit +0x1c3, shock +0x1c4/0x1c5); moves-engine entry is the moves.xml loader
   `FUN_8f64b8e0` (game+0x3F48E0).
4. **Arena width source** [UNCERTAIN]: binary does not compute ±Width/2; wall/floor
   collision is the original bound. The exact function deriving the wall x-coordinates
   from Wall + `<Image>` anchors was not resolved.
5. **super_hit1-5.wav** are attack-sample sounds tied to specific moves in moves.xml
   (TitanKick, TwoHandedSuperSlash), NOT a crit marker; binary has no super_hit string.
6. **`<Hit>` type semantics**: `Type="Critical"`/`Type="Shock"` on reaction-move
   conditions match hit flags computed at damage time (crit roll / shock roll); `Critical="1"`
   on the `<Hit>` *event* (moves.xml line 10) is the same flag on the receiving side.
7. Bag: no distance fallback in the binary (no strings/constants/branches); engine
   fallback (game.cpp:4796-4839) is an invention to remove. Impulse is purely from
   moves.xml `<Impulse X/Y/Z>` applied on capsule contact.
8. Tutorial bag-hit counter (3 hits → FIRST_FIGHT) in the original increments only on
   real hits (generic hit path); the `tutorial_punchbag` string @0x8f79cdbc is a UI
   button label only.

## Address index

| What | Address (game+0x) |
|---|---|
| internalSettings.xml parser | 0x8F6567E8 (0x5FF7E8) |
| `<CriticalHit>` (Probability/Damage) parser | 0x8F6634C0 (0x60C4C0) |
| Attribute+Base pair parser | 0x8F65E6F8 (0x6076F8) |
| crit chance getter `attr×0.0001` | 0x8F4A610C (0x3EF10C) |
| hit application (crit flag +0x1c3, shock +0x1c4/5) | 0x8F4AA998 (0x3F3998) |
| crit roll (PRNG compare) | 0x8F65BD64 / 0x8F264674 (0x3E4D64 / 0x1DF674) |
| crit damage multiplier `2^(0.0001·attr)` | 0x8F4A95A8 (0x3F25A8) |
| damage computation | 0x8F4A97B4 (0x3F27B4) |
| shock accumulator/type | 0x8F4A92BC (0x3F22BC) |
| Shock settings parser (Treshold/HeadHitChance/CriticalHitChance/Impulse) | 0x8F662FD0 (0x60BFD0) |
| moves.xml loader | 0x8F64B8E0 (0x3F48E0) |
| 'PhysicalFall' handlers | 0x8F41AE34 / 0x8F41AD6C (0x3A3E34 / 0x3A3D6C) |
| bag loader (both XMLs) | 0x8F674734 (0x61F734) |
| capsule figure parser ('Capsule') | 0x8F4A4418 (0x3DD418) |
| training scene init (bag) | 0x8F2B2F9C (0x24BF9C) |
| params.xml path builder | 0x8F43BDF8 (0x3E4DF8) |
| params.xml parser (`<Root>` attrs) | 0x8F43C6F8 (0x3E56F8) |
| Layer/ModelsViewer parser (spawns) | 0x8F43B0D0 (0x3E40D0) |
| level constructor (spawns → fighter+0xa0) | 0x8F426524 (0x3FF524) |

Key strings: `params.xml` 0x8f797258, `moves.xml` 0x8f7a3d54, `internalSettings.xml`
0x8f7a4008, `CriticalHit` 0x8f7966a8, `CriticalHitChance` 0x8f7a4e94, `PhysicalFall`
0x8f796604, `Capsule` 0x8f799f78, `punching_bag.xml` 0x8f7a5ac4,
`skeleton_punching_bag.xml` 0x8f7a5c0c.

---

## VERIFY_W11 corrections applied

(Verification report `VERIFY_W11.md`, commit e8e202a — §3 offsets adjudicated.)

- `Wall` → **+0x34**, `Floor` → **+0x2c**, `PositionY` → **+0x30** (spec had
  +0x2c / +0x30 / [UNCERTAIN] — all three corrected).
- `FrictionForce` is **not a `LocationParams` field** — parsed into the global
  via `FUN_8f633e64` (game+0x5DCE64).
- `Music` → vector\<string\> list at **+0x18** (was wrongly "global float").
- Attribute parse blocks live at **0x8F43CC1C+** (game+0x3E5C1C+); the
  game+0x3E4E80..0x3E50A0 range is the path-builder string append inside
  `FUN_8f43bdf8`, not the parse.
