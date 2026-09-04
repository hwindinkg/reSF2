# WEA_STATIC — label→bone resolver + knockback feed (web build)

Static-only spec from `reference/www/sf2.502f0946.js` (2533 lines, 1-based)
for Stream 1. **Verdict first: `Wea` is NOT knockback.** `Wea` resolves a
hit-label to a skeleton bone's world-x for AI distance windows; knockback
proper flows through the impulse chain (§3). Both are given below with the
exact feed to implement.

## 1. `Wea(label, me, foe)` (L600, verbatim)

```
Wea(a,b,c){a=b.da.Ic(a,this.t0(b,c));return a!=null?a.ma.x:3.4028234663852886E38}
```

- Inputs: `a` = label string (tactics `Ju.label` / hit-part name), `b` =
  own fighter, `c` = enemy fighter. Output: bone world-x float, or
  `3.4028234663852886E38` on miss.
- Callers: `Q6a` (L610: `Wea(r,model,enemy)` per row label), `XAa` (L612),
  `VAa`-area (L615). All feed `Gu.n0(distance)` window tests — AI range
  measurement, not hit reaction.

## 2. Facing sign `t0` (L618, verbatim)

```
t0(a,b){return a.oa.Fe()==null||b.oa.Fe()==null?0:a.oa.Fe().ma.x<b.oa.Fe().ma.x?1:-1}
```

`NPivot` x-order of the two fighters (either missing → 0).

## 3. Node lookup `Te.Ic(name, facing)` (L549, verbatim)

```
Ic(a,b){if(a==null||a=="")return null;let c=this.model.Ic(a);if(c==null)return null;
let d=c.NE;if(d==null)return c;a=a.charAt(a.length-1);
switch(b){case -1:switch(a){case "1":return c.ma.x<d.ma.x?c:d;case "2":return c.ma.x<d.ma.x?d:c}break;
case 1:switch(a){case "1":return c.ma.x<d.ma.x?d:c;case "2":return c.ma.x<d.ma.x?c:d}break;
default:return c}return null}
```

- `model.Ic(name)` = skeleton node by name (null if missing).
- Trailing `"1"`/`"2"` suffix picks the left/right of the node↔`NE`
  neighbor pair, mirrored by facing `b` (e.g. `NHeel_1`/`NHeel_2`,
  `Z.uTa/vTa`, L2477). No suffix (or `b==0`) → the node itself.
- Consumers besides `Wea`: `oxb` bounds (L618:
  `NPivot.x + t0·xea` vs `yu/zu`), `Gf` bind (L370: `sba` node).

## 4. Per-frame offset `xea` (`Jl`, L635-636)

```
xea(a,b){return a<this.bv.length&&(b=this.M7a(b),-1<b)?m.t3a(this.bv,a)[b]:0}
```

`bv` tables built by `DFa/Gdb/gkb` (L635); `M7a` names→index. Used by
`oxb` (L618) for the moving-fighter bound.

## 5. Knockback proper (impulse chain to implement)

- Per-interval impulse `kw/gR/hR` from `<Impulse X/Y/Z>` (`Ul`, L777);
  shock values `0/-0.5/0` (COMBAT_STATIC §A8).
- `Kwb(a,b)` (L509): `H(kw,gR,hR)` mirrored by `da.hd()`, scaled by
  `JG.x/y/z` → `target.strike(...)`.
- Intake `lrb(bk,dir,se?1/60:1/120)` (L395); direction `Vi` alignment:
  `SBa` sign, `Reverse` flag, `SetDirection From/To` (L704/L721).
- Slow-mo on crit/head/shock via `ZAa→DL` (L396/L422/COMBAT_STATIC §A9).

## OPEN-KEPT

Live bone positions (`ma.x` per frame) and `NE` neighbor wiring come from
the animated skeleton — formulas above are static; values need a runtime
pose trace to close.
