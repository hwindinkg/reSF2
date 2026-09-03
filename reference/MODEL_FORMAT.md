# MODEL_FORMAT — Shadow Fight 2 web fighter models: schema + game render path

**Sources (all byte-verified this session):**
- Model XMLs: `reference/www/res/models.473fd74f.dat` (408 models) and
  `reference/www/res/models_dojo.e57366a0.dat` (5 models). Full dumps of
  `mdl_skeleton`, `mdl_body`, `mdl_head`, `mdl_punching_bag`,
  `mdl_skeleton_punching_bag` saved to `reference/extracted/models/`
  (gitignored; `pretty/` subdir has the pretty-printed versions).
- Game JS: `reference/www/sf2.502f0946.js` (1,601,954 bytes, Haxe 4.3.7 output).

**Mangled class names in this build (re-mangled from other SF2 builds):**

| Role | Class | Line | Notes |
|---|---|---|---|
| Model/ragdoll parse | `Yc` | L568-575 | `Yc.load` → `Yc.parse` → `Mia/kjb/Uib/nkb` |
| Model container ("physics body") | `Dl` | L575-581 | holds nodes/edges/capsules/triangles; `model.go` is its render tree |
| Node lists | `Du`/`Cu`/`Bu`/`Au` | L581-582 | bones / edges / capsules / pairs |
| Bone (Node) | `Vc` | L793-796 | `ma` = current pos, `mf` = prev pos, `p8` = bind pos |
| MacroNode | `Fl` | L796-797 | child-bone averages via `LCC` weights |
| Edge | `yu` | L793 | joint constraint; capsule colliders ride on edges |
| Capsule figure | `zu` | L793 | `<Figures Type="Capsule">` → render capsule |
| Triangle mesh data | `dv` | L840-841 | `DXa()` from 3 node names; `ia()` fills `Xg` vertex array |
| Mesh render node | `Fk` | L841-842 | draws `Bc` (dv) via `e` (Yi) |
| Capsule render node | `Ek` | L842 | container of capsule drawables |
| 3D container (dd) | `dd` | L37-40 | `tWa/vWa/uWa/wWa` create mesh/capsule/other components |
| Model render tree root | `Hd` | L1466 | `Kh` subclass; canvas 2D node |
| Animation controller | `Te` | L545-566 | `eda()` writes frame positions into bones |
| Fighter | `wd` | L490-536 | `oa` = Dl body, `go`/`MW`/`Jba` = render tree |
| Fighter params | `xc`/`El` | L804-822 | `lx` = model name list, equipment slots |
| 2D canvas renderer | `Td` (via `Ph`) | L1569-1583 | Path2D triangle fill (2D projection) |

---

## 1. The model XML schema (models.*.dat entries)

Each archive entry (container format: u16 LE count + name-len/name/u24-size/data —
see `core/data/README.md`) is one **plain XML document**:

```xml
<Scene>
  <Nodes>    ... </Nodes>
  <Edges>    ... </Edges>
  <Figures>  ... </Figures>
  <GroupsOfSelection> ... </GroupsOfSelection>   <!-- optional, skeleton only -->
</Scene>
```

### 1.1 `<Nodes>` — the bones

Every child is a bone. Two types (attribute `Type`):

**`Type="Node"`** — a single point:

```xml
<NTop Z="7.33019" Y="283.47760" X="-20.51979" Visible="1" Type="Node" Rank="5"
      PinFixed="0" Passive="0" Mass="0.77293" Fixed="0" Collisible="1" Cloth="0"/>
```

Attributes (all verified from `mdl_skeleton.xml`):

| Attr | Type | Meaning |
|---|---|---|
| `X`,`Y`,`Z` | float | **bind position** in model space (screen units; world Y = up) |
| `Type` | enum | `Node` \| `MacroNode` \| `CenterOfMass` |
| `Mass` | float | physics mass (0.01 … 9.375 observed) |
| `Visible` | 0/1 | visibility (→ `d.R()`, `Yc.Ijb` L571) |
| `Fixed` | 0/1 | fixed-in-space (→ `d.kla()`, L571) |
| `Collisible` | 0/1 | collision flag (→ `d.$Da`, L572) |
| `Cloth` | 0/1 | cloth-sim flag (→ `d.KMa` → `PG`, L571) |
| `Attenuation` | float | cloth damping (→ `bI`, L571) |
| `Weak` | 0/1 | (→ `UEa`, L572; on `Node12` of punching bag) |
| `Shock` | 0/1 | shockable flag (→ `vc`, L572; on `Weapon-Node*`) |
| `Rank` | int | skeleton rank (0=root/pivot … 9=fingertips) — **bone hierarchy depth hint** |
| `PinFixed` | 0/1 | (parsed by `PinFixed` in Yc.Ijb; drives pinning) |
| `Passive` | 0/1 | passive ragdoll flag (→ `passive` in physics; `NChestS_*` etc. are Passive=1) |

**`Type="CenterOfMass"`** — one per model (named `COM` in `mdl_skeleton`):

```xml
<COM Z="14.44092" Y="186.63337" X="-15.74271" Visible="1" Type="CenterOfMass"
     NodesCount="42" Mass="0" Fixed="0" ChildNode1="NPivot" ... ChildNode42="NHip_2"/>
```

- The `ChildNodeN` list enumerates **every** bone of the model (reference pose).
- Parser: `Yc.Ijb` (L571) — `h = f=="CenterOfMass"` → creates bone `Vc`, clears the
  child list `c` and calls `Yc.FIa(a,b,!1)` which pushes `Ba(ChildNodeN, 0)` pairs.
  `COM` becomes the model's "root" (`Yc.Trb` L578: `Va.Yd = Ic(this.jX)` where
  `jX = "_CenterOfMass_"`).

**`Type="MacroNode"`** — a cluster over 3-4 child bones:

```xml
<HEAD-MacroNode1 Z="61.96223" Y="204.67498" X="0.53679" Visible="1"
     Type="MacroNode" NodesCount="4" Mass="0.00248"
     LCC4="1.04091" LCC3="1.03198" LCC2="-0.67190" LCC1="-0.40098" Fixed="0"
     ChildNode4="NHeadS_2" ChildNode3="NHeadS_1" ChildNode2="NHeadF" ChildNode1="NTop"/>
```

- `ChildNode1..N` reference bones by name; `LCC1..N` are **barycentric weights**
  (can be negative; usually ≈1 each → the MacroNode is placed near the average of
  its children).
- Parse: `Yc.Ijb` (L571) — `f=="MacroNode"` → `e.x*=-1` (mirror!), then
  `new Fl(g,e)`, `Yc.Yib` (L574) → `Yc.FIa(a.Uba,b,!0)` reads
  `NodesCount`, `ChildNodeN`, `LCCN`. In `Dl.dGa` (L579) each MacroNode gets
  `XWa(g, f.second)` = `Fl.XWa` (L797) which pushes `Ba(bone, weight)` into
  `Fl.children`; `Fl.seb` (L797) then computes the MacroNode position as the
  **weighted average of its child bones** (unless it was explicitly placed by the
  animation this frame — `Ega` flag).
- **MacroNodes are the mesh skin-control points.** The head mesh triangles
  reference `HEAD-MacroNodeN`, not raw skeleton bones.

### 1.2 `<Edges>` — joints/constraints

```xml
<EHead WithSign="0" Visible="1" Type="Edge" SubNodesCount="0" Radius="12"
       Margin2="0.5" Margin1="0" Length="22" Fixed="0" End2="NTop" End1="NHead"
       Defense="HeadDefense" Collisible="1" BodyPart="Head"/>
```

| Attr | Meaning |
|---|---|
| element name | edge id (referenced by capsules) |
| `End1`,`End2` | **bone names** (resolved via `Dl.wBa`, L576 — falls back to the owner fighter's body for shared bones) |
| `Length` | rest length (world units) |
| `Radius` | collision radius (used for hit/impact) |
| `Margin1`,`Margin2` | capsule margin offsets |
| `Defense` | `BodyDefense`/`HeadDefense` — hit-defense region |
| `BodyPart` | `Body`/`Legs`/`Head` — **hit-slot classification** (equipment slot semantics) |
| `Collisible` | 0/1 — if 1 the edge is also added to `Nl.oI` (impact list) |
| `WithSign` | 0/1 — "signed" edge (has `SubNode1..3`, see below) |
| `SubNodesCount` + `SubNode1..3` | additional via-points for curved constraints |
| `Iterations` | (read in `Yc.kjb` L569) — how many copies of the edge to create; copies get suffix `CI0..CIN` |
| `Type="Muscle"` | muscle constraint (`Tension`, `MinLength`, `MaxLength` instead of radius) |
| `Type="Edge"` | normal constraint |

`Yc.jjb` (L572-573) creates a `yu` per edge; `Type=="Edge"` → `Nl.IF` (+`Nl.oI`
if collisible), `Type=="Muscle"` → `Nl.GGa`, else ignored. Edge copies with
`Iterations>1` get names `<name>CI<k>`.

### 1.3 `<Figures>` — mesh + collision capsules

Two child types, dispatched by `Type` attribute (`Yc.Uib` L570 for Capsule,
`Yc.nkb` L570 for Triangle).

**`Type="Triangle"` — the mesh** (the visual skin):

```xml
<BODY-Triangle-1 Type="Triangle" Node3="NAnkle_2" Node2="NToe_2" Node1="NHeel_2" DoubleSided="-1"/>
```

- `Node1`,`Node2`,`Node3` = **bone names** (Node OR MacroNode). **The bones ARE the
  vertices** — there are no separate vertex positions, no UVs, no bone weights, no
  indices array in the XML. Vertex count = number of distinct node names
  referenced; triangle count = number of `<Figures Type="Triangle">` elements.
- `DoubleSided` = `-1` → both faces drawn.
- Parse: `Yc.mkb` (L574) — resolves each node via `a.Ic(name)` (map lookup in
  `Va.Xca`) then `a.model.MW.Bc.DXa(c,d,b)`. `dv.DXa` (L840) dedupes the node
  list (`kM`) and pushes 3 indices into `zU`; `dv.ia` (L840) builds the interleaved
  vertex buffer `Xg[] = {x0,y0, x1,y1, ...}` from the nodes' **current** `ma`
  positions, **dropping z** (only `x`,`y` are copied).
- `mdl_body` mesh = 29 triangles referencing cloth nodes (`BODY-Node*`) + real
  skeleton bones (`NAnkle_2`, `NHip_2`, `NStomach`, `NChest`, ...). `mdl_head`
  mesh = 218 triangles referencing only `HEAD-MacroNodeN`.

**`Type="Capsule"` — collision collider:**

```xml
<Capsule_EFoot_1 Type="Capsule" Radius2="4" Radius1="4" Margin2="0" Margin1="0" Edge="EFoot_1"/>
```

- `Edge` = an `<Edges>` element name. Capsule spans that edge; `Radius1/2` and
  `Margin1/2` modify the thickness at each end.
- Parse: `Yc.Tib` (L573) — looks up the edge (`RAa`), builds `zu`, then
  `b.U1a(a.model.go)` creates a `dd` child with a capsule drawable (`tWa()`)
  and registers it on `a.model.Jba` (the `Ek` capsule render node) via
  `oWa(c)` (L842).
- `mdl_skeleton` itself has **no** `<Figures>` (empty); `mdl_body` adds ~80
  capsules that thicken the skeleton edges into a body; `mdl_head` adds 2.

### 1.4 `<GroupsOfSelection>` (skeleton only)

`mdl_skeleton` carries 9 `<SG-N>` groups listing node names (`<NAnkle_1 Type="Node"/>`).
Not referenced by any JS code path found — cosmetic/editor metadata (selection sets
in the ragdoll editor). **Ignored by the game.**

### 1.5 Model "types" observed (408 models in models.dat)

| Family | Pattern | Contents |
|---|---|---|
| Skeleton | `mdl_skeleton` | bones + edges + muscles + COM, **no mesh** |
| Body | `mdl_body` | cloth nodes (`BODY-Node*`) + `BODY-MacroNode*` + mesh triangles + capsules (references skeleton bone names) |
| Head | `mdl_head` | `HEAD-Node*` + `HEAD-MacroNode*` + 218 triangles + 2 capsules |
| Armor | `mdl_armor_*` (177) | full rig: 156-186 bones, 200-272 triangles, 82-236 capsules |
| Weapon | `mdl_weapon_*` | small rig with `Weapon-Node*`/`Weapon-Edge*` (mirrored in `mdl_skeleton` as `Weapon-Node*_1/_2`) |
| Dojo | `mdl_punching_bag`, `mdl_skeleton_punching_bag` | bag = edges+capsules only; bag skeleton = bones+edges, no mesh |

**Key insight for Phase 3:** the fighter visual = **one shared bone hierarchy**.
The skeleton model defines the bones; body/head/armor/weapon models ADD their own
cloth/MacroNodes and **their triangles reference both their own nodes AND the
skeleton's bone names**. All models in one fighter are parsed into the SAME `Dl`
(`Yc.load` L568: loop over the model-name list `b`, each `Yc.parse(a, xml, name)`
merges into `a`). So there is exactly one bone list; every triangle of every part
is emitted into the single `Fk` mesh; every capsule into the single `Jba` node.

---

## 2. Game render path (how a fighter is assembled + drawn)

### 2.1 Model assembly (XML → structures)

1. **`wd.Erb(this.parameters.lx)`** (L496 in `wd`, called from `init`) — `lx` is
   the **list of model names** for this fighter (built by `xc.cM`, L809-810, from
   equipment: `Of.model`=weapon, `Hd.model`=weapon-in-hand, `hg.model`=helm,
   `Lg.model`=armor + `Kv` extras; each item's `Model` attribute in items.xml is
   the exact archive name, e.g. `Model="mdl_armor_alloy"`).
2. **`Yc.load(oa, lx)`** (L568) — for each name: `e = Ja.Lh(d)` (extract XML from
   the models archive by name; L45 `Ja.Lh` = `Mda(Ja.Ra,name)`), then
   `Yc.parse(oa, e, d)`.
3. **`Yc.parse`** (L569) → `Mia` (Nodes) + `kjb` (Edges) + `Uib` (Capsules) +
   `nkb` (Triangles). Every bone is pushed into `oa.Va.all[]` with `id` = index
   (`Yc.Ijb` L571: `d.id = a.Va.all.length`), and into `oa.Va.Xca` map by **name**
   (`X.Xa(a.Va.Xca,d.name)||a.Va.Xca.set(d.name,d)` L572). **Bone order = document
   order across all models, in the order models were listed in `lx`.**
4. Post-pass `Yc.Trb` (L578): `Va.Yd` = the COM bone (root reference).
5. `Yc.dGa` (L579): wires MacroNode child lists (`Fl.XWa`). 
6. `Yc.Hqb` (L580) → `v5a`: pairs `_1`/`_2` bones (e.g. `NWrist_1`↔`NWrist_2`)
   into `Wf.b3` and sets `NE` (used for mirror-swap "rw").
7. `Erb` then creates `this.oa` (a `Dl`), plus `Nd` (`Al` physics), `IH` (`Bl`),
   `da` (`Te` animation), `Fu` (`Cl` hits), `nf` (`de` weapon controller)
   (L502-504). The **render tree** was already created in the `wd` constructor
   (L492): `go=new dd("Model")`, `MW=go.uWa()` (a `Fk` mesh node inside a
   `dd("Mesh")` child), `Jba=go.vWa()` (an `Ek` capsule node inside
   `dd("ModelCapsules")`). `Yc.mkb` writes triangles into `MW.Bc` (`dv`);
   `Yc.Tib` writes capsules into `Jba`.
8. **The same `Dl` object is the physics body AND the render source** — `oa.Va.all`
   are the bones; the mesh and capsules read the bones' `ma` each frame.

Resulting structures:

| Game structure | Type | Content |
|---|---|---|
| `oa.Va.all` | `Vc[]` | all bones (Node + MacroNode + COM), `id` = index |
| `oa.Va.Xca` | Map<name→Vc> | name lookup for triangle/capsule refs |
| `oa.Va.UJ` | `Fl[]` | MacroNodes (weighted children) |
| `oa.Nl.all` | `yu[]` | edges |
| `oa.Jqa.ywa` | `zu[]` | capsules (collider figures) |
| `oa.model.MW.Bc` | `dv` | triangle mesh (deduped node list + index list) |
| `oa.model.MW.e` | `Yi` | draw data: `Xg` = 2D vertex array, `qu` = indices |
| `oa.model.Jba` | `Ek` | capsule render node |

### 2.2 Animation → bones (per-frame transform)

**Bone positions in the animation clips are ABSOLUTE WORLD positions** (per-bone
per-frame, no parent-relative chain). Evidence:

- `Te` constructor (L545) + `Skb` (L550): `this.Ua.Pka(this.jc, f, ...)` fills
  `jc` (`vu`), a per-frame buffer; `Gka` (L561) calls `rpa.initialize(...)` +
  `rpa.f6a(a[f], c[f], b[f], this.fq[f])` — `fq[frame][bone]` = interpolated
  absolute position per bone (`wu` interpolator, `fq` = per-bone per-frame list).
- **`Te.eda()` (L556) is the apply step, run every update tick:**
  ```js
  for (c=0; c<a; ) { let e = this.model.Va.all[c];      // bone by index
    if (!this.model.vc || this.model.vc && !e.vc) {
      e.f4();                                            // mf = ma (save prev)
      d = this.Go; c = this.fq[c][this.mo];              // fq[bone][frame]
      d.x = c.x; d.y = c.y; d.z = c.z;
      d = this.Go; c = this.j8; d.x += c.x; d.y += c.y; d.z += c.z;
      e.XA(this.Go);                                     // ma = position (absolute)
      e.Ega = !0;                                        // explicitly placed
    }
  }
  ```
  `fq[bone][frame]` is indexed **by bone id = node index** (`fq` is sized to
  `Ua.ZW` = bone count, L551 `m.resize(this.fq, a.ZW, ...)`), and `e = Va.all[c]`
  iterates bones in id order — so **clip bone index b ↔ model bone id b**, a
  straight 1:1 mapping by order, NOT by name. (This is why the archive stores no
  names: `animations.*.dat` bone i = `Va.all[i]`.)
- `e.XA(pos)` (L794) just sets `ma = pos` (absolute). **No parent-chain math.**
- Facing: `Te.Qeb()` (L550) — `this.FX==-1 && this.jc.Neb()` flips the whole
  buffer's X when the fighter faces left.
- Mirror-swap ("rw"): `Te.Peb()` (L560) — `this.rw = Te.MYa(model, Ua, hd, jc.Kh(2).data)`
  decides if the animation should be mirrored for `_1`/`_2` bones (when
  `hd()==-1` and `Te.lwa` (L566) detects the left/right bones crossed in X).
  `Te.xqb` (L553) then swaps `_1`↔`_2` bone names in edge events.
- Root/offset: `Gub` (L557-560) computes `yaa`/`Fk` from the clip's `align`
  (`EObjectAnimation`/`EObjectNodes`/`EObjectPivot`/`EObjectWall`), and
  `Gla(a.cI?...` shifts the whole `jc` buffer by `Fk` — the fighter's world
  position offset. `bYa` (L565) applies a per-frame rotation (`Ua.zX`, degrees)
  around a bone for things like spinning attacks.
- Interpolation: `Te.Gka` (L561) builds a `wu` spline interpolator
  (`rpa.f6a(a[f], c[f], b[f], this.fq[f])`) over frames f, f+1, f+2; `eda` advances
  `mo` by `this.Tx = (Ua.XJ+1)*HD()` per frame (`XJ` = subdivision). **Positions are
  interpolated in world space.**

**Confirmed: animation clips store per-bone absolute world-space positions,
applied directly to bones by index. There is no bone hierarchy transform
chain.** (The `Rank` attribute and `GroupsOfSelection` are not used for posing.)

### 2.3 Mesh rendering

- `Fk.update()` (L841-842) runs each frame: `Bc.init()` (once, L840) then
  `Bc.ia()`:
  ```js
  ia(){ let a=0, b=0, c=this.Xg.length>>1;
    for (; b<c; ) { let d = this.kM[b++].ma;   // bone current position
      this.Xg[a++] = d.x; this.Xg[a++] = d.y;  // X,Y only — Z DROPPED
    }
  }
  ```
  The **mesh is CPU-skinned in 2D**: every frame the render node copies each
  referenced bone's (x,y) into a flat vertex buffer `Xg`, then
  `Fk.update` sets `e.indices = Bc.qu; e.Xg = Bc.Xg; e.Rd = count` on the `Yi`
  draw data. **Z is dropped at skin time** — the projection to 2D is trivial
  (identity on x/y).
- Draw: `Xb` (L1466, the `Kh` canvas node holding `Yi` as its `effect`) is drawn
  by the Canvas2D renderer `Ph.Ea` (L1569+): `(t&1)>0` branch (L1578) iterates
  `e.qu` (indices), reads `A[D*3],A[D*3+1],A[D*3+2]` from the vertex array,
  transforms through the 2D affine `g` matrix, and emits `this.path.mE(...)`
  (Path2D). **Flat color fill** (`fillStyle`), no texture, no lighting, no
  depth. `DoubleSided="-1"` → both winding orders drawn (the `(t&2)>0` branch,
  L1581-1583, re-emits the triangle reversed).
- No WebGL shaders anywhere in the fighter path — the only "shader" is the
  Canvas2D `fillStyle` color, set via `Fk.La(a)` → `Na.cd(a)` (L1448) =
  `H((a>>16&255)/255, (a>>8&255)/255, (a&255)/255, 1)` (ABGR int → RGBA float).
  `wd.Qs` (L494-495) converts `color` (H, RGBA 0..1) to ABGR int:
  `(a.z*255|0)&255 | ((a.y*255|0)&255)<<8 | ((a.x*255|0)&255)<<16`.

### 2.4 Fighter assembly from equipment

- `wd.fya(a,b,c,d)` (L535) → `b = a.h7a(b)` (L528: default `El` params) →
  `new ih(b)` (`ih` L589, the sub-fighter) — every fight fighter is actually an
  `ih` sharing the "main" fighter's body via `wI`/`Rlb` (L514-515) →
  `this.init()` → `Erb(parameters.lx)`.
- `parameters.lx` (model list) is filled by `xc.cM()` (L809-810) from equipment:
  - `Of` (Weapon slot, `I.Px="Skeleton"` per L2473! — the enum is
    `I.Px="Skeleton"; I.vg="Weapon"; I.Ai="Armor"; I.Bi="Helm"; I.Vh="Ranged"; I.Cf="Magic"`):
    actually `Of` = **melee weapon**; `Hd` = **weapon in hand** (`I.vg="Weapon"`).
  - `hg` (Helm) → `Lg` (Armor) → `Of`/`Hd` → `Kv` extras.
  - `cM()` pushes `this.LQ(item.model)` — the `Model` attribute string (the
    `mdl_*` name) for each equipped item that has one.
- `Yc.load` parses all these models into ONE `Dl` — **single shared bone
  hierarchy** (bones are appended in list order; duplicate names in later models
  are skipped by the `Xca` map guard `X.Xa(...)||set` L572, so skeleton bones
  defined first win). Mesh triangles and capsules from every part merge into the
  single `MW`/`Jba` render nodes.
- Model→part mapping (from items.xml `Model` attributes + observed names):
  - Skeleton: not an item; `mdl_skeleton` is always in `lx` (it is the base
    rig; armor/body models reference its bones).
  - Body: `mdl_body` (default body cloth+mesh), armors use `mdl_armor_*`.
  - Head: `mdl_head` (helm items reference `mdl_head_*` / the base `mdl_head`).
  - Weapon: `mdl_weapon_*`; the skeleton itself already carries mirrored
    `Weapon-Node*_1/_2` + `Weapon-Edge*` (the "fists" placeholder weapon rig).

### 2.5 Weapon rendering / attachment

- The weapon is **part of the same bone hierarchy** — `mdl_skeleton` embeds
  `Weapon-Node1..4_1` (left) and `_2` (right) bones plus `Weapon-Edge*` edges
  (L22-29, L145-156 of `mdl_skeleton.xml`). The actual weapon model
  (`mdl_weapon_*`) adds its own mesh triangles over those same bones.
- Attachment: edges `Edge129`/`Edge130` (`Shock="1" Length="0.1"`)
  connect `Weapon-Node2_1 ↔ NWrist_1` and `Weapon-Node2_2 ↔ NWrist_2`
  (L157-158) — **the weapon rig hangs off the wrist bones**; because the weapon
  nodes are ragdoll bones in the same solver, they track the wrists.
- The `de` weapon controller (L589) drives weapon-state logic (`nf.ia` → attack
  checks, `nf.Gc` = magic weapon, `Wsb`/`jwb`/`LLa` = weapon switch); it does not
  re-parent the mesh — the mesh follows the bones automatically.
- Equip/unequip: `wd.$o` (L527) swaps the item + `jmb` (L501) rebuilds
  (`ra.Hza(this,!0)` re-derives move lists); `Wqb` (L528) does the "pick up
  weapon" animation.

### 2.6 Camera / projection / z-handling

- The fighter mesh is rendered by the Canvas2D renderer with **z dropped at the
  vertex-copy step** (`dv.ia` L840: only `x`,`y` written). The 2D transform chain
  is `Kh.tu()/tg()` (L1459-1462) composing `translate`/`scale`/`matrix` (`Jb`/`dc`
  local→world), all 2D affine — the `z` component of `translate` exists in `Lh`
  but is ignored by the affine matrix math for drawing.
- `z` IS still meaningful in the scene graph for **sorting/depth only**:
  `Qi.NWa/pWa` (L487) do `a.Dla(this.QH); this.QH += -.01` — background layers
  are pushed with decreasing `translate.z`; the `$d.Ea`/screen render sorts by
  node order and uses `$m` (render flag) + `Ke.Dla` (L1599, sets
  `Jb.translate.z`) to order painter's-algorithm draws. The model's own
  `translate.z` defaults to 0; `dd` children keep their `z=0` so the whole
  fighter draws as one unit at its scene depth.
- **Fighter world position**: `wd.oL(pos)` (L501) → `oa.oL(pos)` (L577-578)
  offsets ALL bones (`l.x += f; l.y += g; l.z += c`) relative to the COM bone —
  the model is translated as a rigid whole; the `Te` controller's `Gub`/`Gla`
  (L557-560) additionally shifts the clip buffer so the animation's root aligns
  with the fighter's position (`yaa`/`Fk`).
- Camera: the fight camera `Ut` (L823) / `ma.Sya` (L1833) sets the screen
  container's `translate`/`scale`; no perspective — orthographic, x-right,
  y-up-in-world / y-down-on-canvas (model Y is world-up; the canvas transform
  flips via the 2D matrix).

---

## 3. Native implementation notes (Phase 3)

1. **Parse once, share bones.** Parse `mdl_skeleton` first, then parts; bones
   append by document order; triangle/capsule refs resolve by name against the
   shared `Map<name, bone>`.
2. **Bone index = animation clip bone index.** Clip bone i maps to `bones[i]`
   (order-sensitive). Clip positions are absolute world (x, −y, z — y negated
   at clip parse, see `core/data/README.md`).
3. **Skinning = per-frame 2D copy.** For each triangle, look up 3 bones, take
   `(x,y)`, emit triangle. No matrices, no weights, no UVs. Z is never used for
   the fighter mesh.
4. **Depth**: fighters draw in scene order; within a fighter, order is
   painter's by node order (mesh then capsules; capsules drawn with `$m`
   transparency as debug-ish overlays — verify `Ek`/capsule draw style at
   runtime).
5. **Facing**: negate X of all bones (or the vertex copy) when `da.hd()==-1`.
6. **`_1`/`_2` mirroring** (`rw`): when facing left, swap the frame data between
   `_1` and `_2` bone pairs (per `Te.Peb`/`Te.lwa`/`Te.xqb`) — only when the
   animation's `align` says so (`Te.MYa`).
7. **Interpolation**: spline over f, f+1, f+2 (`wu.f6a`) with `XJ` subdivisions;
   `Tx=(XJ+1)*HD()` ticks per clip frame at 60 Hz.
8. **Weapon**: no special attach code — weapon bones are skeleton bones; the
   weapon mesh just draws over them.

## 4. Honest gaps / unverified

- `GroupsOfSelection` purpose: no JS reference found (likely editor-only).
- `PinFixed`/`Passive` exact physics semantics: flags exist in `Yc.Ijb` (L571-572
  reads them) but their runtime effect is inside the ragdoll solver (`Al.sk`,
  L583) — not traced to completion.
- The `WithSign`/`SubNodeN` edge constraint math (`yu.dw`, L793; `Uy` helper):
  exact constraint formula not re-derived (only parse confirmed).
- Capsule visual style (opacity/color) at runtime: `Ek` draws `zu` shapes; the
  exact Canvas2D capsule path (`ke`/`Zw` arc flatten, L1638) confirmed present but
  not traced per-frame.
- Which item names map to `mdl_body`/`mdl_head` exactly: `items.xml` lives inside
  asset 818 (`it.parse` L165: `Ja.ki(818)`), which is a separate archive we did
  not extract; the mapping is via each item's `Model` attribute → archive name
  (`xc.cM` L809-810) and the observed `mdl_*` families above.
- Dojo punching bag wiring: `mdl_skeleton_punching_bag`/`mdl_punching_bag` exist
  in `models_dojo.dat` but the dojo screen code that instantiates them was not
  traced (dojo uses `Tf`/`Oa` fight screens, L1972/L2285; the `ModelsViewer`
  positions in `dojo_params.xml` place player/enemy).
