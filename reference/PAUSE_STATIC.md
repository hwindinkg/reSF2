# PAUSE_STATIC — pause/freeze semantics (web build)

Static-only spec from `reference/www/sf2.502f0946.js` (2533 lines, 1-based)
for Stream 3's pause menu (and Stream 1's freeze, if implemented).
`OPEN (needs runtime trace)` where noted.

Minified names: `ca.wn` fight-pause latch (L379), `Ca.M4a/P4a/N4a`
platform bridge (L34-35), `Sf.Jn` HUD pause button (L2034), `Ar` fight HUD
(`ha`, L2016), `Sf` HUD timer (L2033-2036), `Cr` overlay (`Se`, L2022-2027),
`Qg` pause-menu router (L410), `Dr` pause dialog (L2018), `kk` result dialog
(L2018), `Us` fixed step (L135).

---

## 1. Freeze gate: `ca.Ea` / `wn` (L379/L385)

```
Ea(){var a=this.ha;a!=null&&a.Qrb(this.wn);
  this.wn ? (a!=null&&a.layout())
          : (this.nzb(),this.ia(),xX/mV.Qh...)}
```

`wn=true` skips the whole sim tick (`nzb` facing, `ia` combat+AI+HUD,
slow-mo watchers). Only `ha.layout()` refreshes. `wn` writers: init
`false` (L379), `$K` reset `false` (L401), `kD(a)` sets `wn=!0` at battle
end (L427), `fb.Lf case 1` debug toggle (L437). `Qrb(wn)` → `Se.pause=wn`
(L2020-2022).

## 2. `eu` phases (L380/386-388/410-411/437/499)

`eu`: 0 setup → 1 intro → 2 fight → 3 end. `xF(a)` sets + notifies
(`Za.nla`, fighter `Je`, `tb.Gj`, `Bg.Ih`, L388). Transitions: intro done
(`kg`, L387) → `Am()/xF(2)`; `Rkb` round-begin → `xF(2)` (L410); `i4a` →
`xF(3)` (L410); setup `$K/MHa` → `xF(0)` (L401-402/405). Gates: `Mfb`
only at `eu==2` (L411); `fb.Lf` debug needs `eu==2` (L437); `MOa`
regen ticks only at `eu==2` (L499); `Pf` end-presentation at `eu==3`
(L386).

## 3. Pause entry / resume / quit

- HUD button `Sf.Jn` (L2034): visible iff `DJ&&round.Vt&&!Pia`
  (`fla/Gkb`, L2039; killed entirely without `Ca.hasFeature("pause")`);
  tap → `Ca.M4a(cb)` = `GameInterface.gamePause()` → `eS.Z(0)`.
- Menu router `ca.Qg` (L410): 0 → `Aia()` (pause entry: `wn=!0` +
  interstitial `button:fight:pause`, L425); 1 → `Xc.Zhb` exit-confirm
  dialog (L931); 2 → `tZ()` resume (`wn=!1`, L425).
- `ha.zia` → `kk` result/quit dialog; `ha.Aia` builds `Dr` pause dialog
  (`Mgb/Jgb` callbacks, L2018); `tZ` destroys it.
- Resume-from-menu: general-menu `play` → `Ca.P4a(resume)` (L2067).
  Surrender/exit dialog `O3a` → `Ca.N4a` = `gameQuit()` (L1904).
- Platform side (L65): `gamePause` → `Sc.stop()` (+ foreground off);
  `gameResume` → `Sc.start()`. `Fcb()` = `isPaused()` query (L35).
- NOTE: HUD-button pause sets **no `wn`** — freeze comes from `Sc.stop()`
  (no frames); menu-path pause sets `wn`. On resume both clear. A platform
  pause with stale `wn` is not reconciled statically — OPEN-KEPT.

## 4. Timers while paused

- Round timer `Sf`: `reset: xU=round.gma*60+1` (L2036); `iPa` ticks
  `--xU`, `NF=xU/60|0` + display (L2036); `PEa: NF<=0` (L2020). Ticks in
  `Sf.ia`, driven by `Ar.ia←ca.ia` (L2018/L2035/L390) ⇒ frozen by `wn`;
  doubly frozen by `Sc.stop()`.
- Overlay intro `Cr.aa`: `!pause && wU && (Sc-=a…)` (L2027) — frozen.
- Slow-mo watchers `cu.Qh`: skip while `wn||yt` (L433).

## 5. What ticks and what doesn't

- FROZEN: fighters + AI (`de.ia←wd.ia←ca.ia`, L499), combos (`Enb`),
  rounds (`Onb`), particles (`Ta…WL()`, L389 `sga` branch), HUD timer +
  life bars (`ha.ia`, L390), overlays (`Cr.aa`), slow-mo (`cu.Qh`).
- CONTINUES: music (no `ta.$f` pause call exists on any pause path —
  `$f` has only play/stop/uF/cMa, L1264-1277); platform clock resumes via
  `Sc.start()`.
- Clarification: quest action `Jn` (L1034, `p.Cw.a_a()` offers) is NOT
  pause — the pause button is the `Sf.Jn` widget (L2034).

## OPEN (needs runtime trace)

1. `$f` backend under `Sc.stop()` (WebAudio suspend vs keep-playing).
2. External-pause `wn` reconciliation (see §3 note).
3. `Ar.ia` engine driver confirmation (caller not found statically;
   inferred via `ca.ia` tail, L390).
