#!/usr/bin/env node
/*
 * combat_golden.js — Phase 5 Node-harness golden (static-first methodology).
 *
 * Transcribes the PURE combat gates 1:1 from
 * reference/www/sf2.502f0946.js (same language — expressions are copied
 * verbatim, only the `this`/statics plumbing is stubbed). Every block cites
 * its JS line. Full spec: `reference/COMBAT_STATIC.md`.
 * Run: `node reference/tools/combat_golden.js` → JSON vectors on stdout,
 * self-test asserts on stderr, exit 0 = GREEN.
 *
 * HONEST SCOPE (same as ai_golden.js): this verifies the transcription of the
 * gate LOGIC. It cannot catch a spec misreading shared by a future C++ port
 * (Phase 8 runtime traces), and geometry/volume tests (`Cl.W1a/Bz`,
 * `Gc.Pkb/Nsb`, `v.pAa` armor tables) are injected stubs — see OPEN in
 * COMBAT_STATIC.md. RNG (`uf.RGa→Math.random`, `Da.cT` stream) is injected
 * per scenario so every vector is deterministic.
 */
"use strict";

// ---- Te container ops (L553-554), xj passed explicitly ----
function yD(xj, a) { let b = 0, c = xj.length; for (; b < c;) { let d = b++; if (a == xj[d].type) return xj[d]; } return null; }
function cBa(xj, a) { let b = 0, c = xj.length; for (; b < c;) { let d = b++; if (xj[d].name == a) return xj[d]; } return null; }
function hT(xj, a) { let b = xj.length - 1; for (; b >= 0;) a == xj[b].type && xj.splice(b, 1), --b; return xj; } // m.ye == splice
function F4(xj, a) { let b = xj.length - 1; for (; b >= 0;) a == xj[b].name && xj.splice(b, 1), --b; return xj; }
function Hja(xj, names) { let b = 0; for (; b < names.length;) F4(xj, names[b++]); return xj; }
function SZa(xj, a) { let b = 0, c = a.length; for (; b < c;) if (cBa(xj, a[b++]) != null) return !0; return !1; }

// ---- fe.G0 (L774) ----
function feG0(a) { switch (a) { case "Attack": return 4; case "Block": return 5; case "Invisible": return 7; case "Invulnerable": return 6; default: return 0; } }

// ---- Da.cT shortcut (L2352, cf. ai_golden.js) + v.Lcb (L1204) ----
function daCT(draw, a, b) { b == null && (b = 100); return a > b ? true : draw < a; } // draw = pg.s4(b)
function vLcb(draw, a) { return daCT(draw, a * 100); }

// ---- wd.qYa / Nbb (L514) ----
function qYa(xj) { return yD(xj, 5); }
function Nbb(xj) { return qYa(xj) != null; }

// ---- wd.LAa (L536): static LAa(a,b,c){...} ----
function wdLAa(KP, blocked, weaponXi, pYa, lNa) {
  if (KP.length > 0) return KP[0];
  if (blocked) return pYa;
  let b;
  weaponXi != null ? (b = weaponXi, b = !(b == null || b == "")) : b = !1;
  return b ? weaponXi : lNa;
}

// ---- IAa / kea / qea (L512-513) ----
function IAa(flag, Mk, Bc, attrs) { return flag ? Math.pow(2, attrs[Mk] * Bc) : 1; }
function kea(blocked, VY, attrs) { return IAa(blocked, VY.Mk, VY.Bc, attrs); }
function qea(se, HZ, attrs) { return IAa(se, HZ.Mk, HZ.Bc, attrs); }

// ---- wd.Orb (L517): Orb(a){this.sr+=a;return!this.oa.vc&&...} ----
function orb(st, add) { st.sr += add; return { sr: st.sr, shock: !st.oaVc && st.sr > st.threshold }; }

// ---- wd.R8a (L531-532), RNG + attrs injected ----
function r8a(P) {
  if (P.ecb) return { raw: true, via: "ecb" };
  if (P.wdVc) return { raw: false, via: "wdVc" };
  let b = P.Zi / P.atkSo;
  const o = orb({ sr: P.sr, oaVc: P.oaVc, threshold: P.threshold }, P.ws ? 0 : b);
  let a = P.iyaBase; a *= P.hyaVal;
  let d = P.pDaBase * P.oDaVal, e = false, f = false;
  if (P.se) e = a * b > P.rCrit;
  if (P.Uq && !P.block) f = d * b > P.rHead;
  return { raw: (o.shock || f) ? true : e, painShock: o.shock, critE: e, headF: f, srNew: o.sr, b };
}

// ---- strike() block-break routing (L509): g.DDa&&(hga empty?hT(5):Hja(hga)) ----
function strikeBlockBreak(targetXj, DDa, hga) {
  if (DDa) { hga.length == 0 ? hT(targetXj, 5) : Hja(targetXj, hga); }
  return targetXj;
}
// ---- strike() se composition (L510): b=!block&&!a3&&Lcb(A9a) ----
function strikeSe(blocked, a3, lcbHit) { return !blocked && !a3 && lcbHit; }

// ---- Cl.ia skeleton (L566-567); geometry injected ----
function clIa(st, targetParts, GY, atk, geoHit) {
  if (st.dW == atk) return { hit: false, via: "oneshot" };
  if (!atk.aEa) { st.dW = atk; return { hit: true, via: "noParts" }; }
  for (let d = 0; d < GY.length; d++) { void targetParts; void d; }
  if (geoHit) { st.dW = atk; return { hit: true, via: "geo" }; }
  return { hit: false, via: "miss" };
}

// ---- wd.HZa gate (L500-501) ----
function hzaGate(P) {
  // P: {ownAtk|null, targetXj|null->jb, jga, iga, geoHit, skipImpulse, clSt, GY}
  let c = false, impulse = false;
  const d = P.ownAtk;
  if (d != null) {
    const t = P.targetXj; // null => jb default; harness passes resolved list
    if ((t == null || yD(t, 6) == null || (d.jga && d.iga.length == 0) || (d.jga && SZa(t, d.iga))) &&
        clIa(P.clSt, null, P.GY, d, P.geoHit).hit) {
      if (!P.skipImpulse) impulse = true; // Kwb(a,d): impulse+strike
      c = true;
    }
  }
  return { chained: c, impulse };
}

// ---- ca.Cgb guards (L394) ----
function cgbGuards(Bb, P) {
  // P: {a3, gd, fightNone, wdVc, sn, auHdName, ownHdName}
  if (P.a3) Bb.se = false;
  if (P.gd < Bb.bR) { Bb.Zi = P.gd + 0.01; Bb.Iza = true; } else Bb.Iza = false;
  if (P.fightNone) { Bb.se = false; Bb.Ub = false; Bb.Yi = false; }
  if (Bb.Ub) { if (P.wdVc) Bb.Ub = false; else P.wdVc = true; }
  let kwbFired = false;
  if (Bb.Yi) {
    if (P.sn || P.auHdName === P.ownHdName) Bb.Yi = false;
    else { P.sn = true; kwbFired = true; } // kwb(): Wx<0&&(Wx=MFa), L522
  }
  Bb.ep = !P.Dga;
  return { Bb, wdVc: P.wdVc, sn: P.sn, kwbFired };
}

// ---- kwb / Pnb / Wqb flag skeleton (L522, L528, L527-528) ----
function kwb(st, MFa) { if (st.Wx < 0) st.Wx = MFa; return st.Wx; }
function pnb(st) {
  // st: {oaVc, Wx, sr, Xza}; returns 'wqb' on the fire frame, else null
  let fired = null;
  if (!st.oaVc && st.Wx >= 0) { if (st.Wx == 0) fired = "wqb"; st.Wx--; }
  st.sr = Math.max(st.sr - st.Xza, 0);
  return fired;
}

// ---- ca.Sba event flags (L393) ----
function sbaFlags(Bb) {
  return { Defense: Bb.JP, Animation: Bb.aI, Critical: Bb.se, Shock: Bb.Ub, Block: Bb.block, Damage: Bb.Zi };
}

// ---- bCa core composition (L513-514), tables injected ----
function bcaCore(P) {
  // P: {SZ_KP:{KP}, blocked, weaponXi, pYa, lNa, raidACa, raidAttr, raidMax,
  //     tgtAttrs, VY, atkAttrs, HZ, seFlag, Xb, atkLy, iea, UZ, capC2a, ceaBp, dta, so}
  const JP = wdLAa(P.KP, P.blocked, P.weaponXi, P.pYa, P.lNa); // recomputed, L513
  const h = Math.pow(2, P.raidACa * Math.min(P.raidAttr, P.raidMax));
  const b = kea(P.blocked, P.VY, P.tgtAttrs);
  const c = qea(P.seFlag, P.HZ, P.atkAttrs);
  let g = (P.Xb + P.atkLy) * P.iea * b * c * h * P.UZ;
  g = Math.max(g, 0);
  g = Math.min(g, P.capC2a); // c2a cap (attr-table value injected)
  g *= P.ceaBp; g *= P.dta;
  return { dmg: g * P.so, JP };
}

// ---- Gc.DK partition (L673-674); Pkb/Nsb tails stubbed, sja injected ----
function dkPartition(cands, c, sja) {
  const d = [], f = [], g = [];
  const Aua = (a, b) => { const ap = a.animation.priority, bp = b.length > 0 ? b[0].animation.priority : 0; if (ap >= bp) { if (ap > bp) b.length = 0; b.push(a); } };
  let e = null, ukb = null;
  for (let k = 0; k < cands.length;) {
    const h = cands[k++];
    if (c || !h.eb || h.animation.Rha) d.push(h);
    if (h.animation.Rha) Aua(h, g); else Aua(h, f);
  }
  if (f.length > 0) e = f[sja(f.length)];
  if (g.length > 0) ukb = g[sja(g.length)].animation;
  return { d: d.map(x => x.id), f: f.map(x => x.id), g: g.map(x => x.id), e: e && e.id, ukb: ukb && ukb.id };
}

// ---- Gc.DK tail branches (L674); Ukb/Pkb/jJa/Nsb recorded, sja injected ----
function dkTail(a, dIds, eId, gPickId, sjaNote, rec) {
  // verbatim: g.length>0 && a.Ukb(g[sja].animation)
  if (gPickId != null) { a.Ukb = gPickId; rec.ukb.push(gPickId); }
  // verbatim: d.length>0 ? (e!=null&&d.push(e), Pkb(a,d))
  //          : e!=null && (MS ? jJa : Nsb, zY/jza)
  if (dIds.length > 0) {
    if (eId != null) dIds.push(eId);
    rec.pkb.push(dIds.slice());
  } else if (eId != null) {
    if (eId.MS) rec.jja.push([eId.id, eId.R1]);
    else rec.nsb.push([eId.id, eId.index]);
    a.zY = eId.animation.type; a.jza = eId.E_;
  }
  return { d: dIds.map(x => x.id || x), e: eId && eId.id };
}

// ---------------- self-test ----------------
let pass = 0, fail = 0;
const failures = [];
function eq(name, got, want) {
  const a = JSON.stringify(got), b = JSON.stringify(want);
  if (a === b) { pass++; } else { fail++; failures.push({ name, got, want }); }
}
function near(name, got, want, tol) {
  if (Math.abs(got - want) <= tol) pass++;
  else { fail++; failures.push({ name, got, want }); }
}

const VY = { Mk: "BD", Bc: 1 }, HZ = { Mk: "CH", Bc: 0.5 };

// NOTE S1 recompute: b = Zi/atkSo = 10/2 = 5. Expectations live in S1 below.
{
  const out = { scenarios: [] };
  // S1 (corrected): clean hit
  const R1 = r8a({ ecb: false, wdVc: false, Zi: 10, atkSo: 2, ws: false, sr: 0, oaVc: false, threshold: 100, iyaBase: 0.05, hyaVal: 2, pDaBase: 0.1, oDaVal: 1, se: false, Uq: false, block: false, rCrit: 0.3, rHead: 0.3 });
  eq("S1 raw=false", R1.raw, false); eq("S1 sr=5", R1.srNew, 5); eq("S1 E/F", [R1.critE, R1.headF], [false, false]);
  const Bb1 = { JP: wdLAa([], false, "", "BLOCK_DEF", "SLOW_DEF"), aI: "punchanim", se: strikeSe(false, false, false), Ub: R1.raw, Yi: R1.raw, block: false, bR: 10, Zi: 10, ep: false, Iza: false };
  const G1 = cgbGuards(Bb1, { a3: false, gd: 100, fightNone: false, wdVc: false, sn: false, auHdName: "fists", ownHdName: "sword", Dga: false });
  eq("S1 stage7", sbaFlags(G1.Bb), { Defense: "SLOW_DEF", Animation: "punchanim", Critical: false, Shock: false, Block: false, Damage: 10 });
  eq("S1 ep first-hit", G1.Bb.ep, true);
  out.scenarios.push({ id: "S1-clean-hit", stage7: sbaFlags(G1.Bb) });

  // S2: blocked — KP empty, bare fists (Xi==""), JP falls back to pYa; se forced false
  const txj2 = [{ type: 5, name: "blk" }, { type: 4, name: "atk" }];
  const blocked = Nbb(txj2);
  eq("S2 Nbb", blocked, true);
  const JP2 = wdLAa([], blocked, "", "BLOCK_DEF", "SLOW_DEF");
  eq("S2 JP=pYa", JP2, "BLOCK_DEF");
  eq("S2 KP wins", wdLAa(["ARM"], true, "X", "BLOCK_DEF", "SLOW_DEF"), "ARM");
  eq("S2 weapon Xi", wdLAa([], false, "SWORD_Xi", "BLOCK_DEF", "SLOW_DEF"), "SWORD_Xi");
  const se2 = strikeSe(blocked, false, true); // roll says crit, block vetoes
  eq("S2 se vetoed", se2, false);
  const Bb2 = { JP: JP2, aI: "kickanim", se: se2, Ub: false, Yi: false, block: blocked, bR: 4, Zi: 4, ep: false, Iza: false };
  const G2 = cgbGuards(Bb2, { a3: false, gd: 100, fightNone: false, wdVc: false, sn: false, auHdName: "fists", ownHdName: "fists", Dga: false });
  eq("S2 stage7", sbaFlags(G2.Bb), { Defense: "BLOCK_DEF", Animation: "kickanim", Critical: false, Shock: false, Block: true, Damage: 4 });
  out.scenarios.push({ id: "S2-blocked", stage7: sbaFlags(G2.Bb) });

  // S3: IgnoresBlock — DDa blanket + named breaks
  const t3a = [{ type: 5, name: "blk" }, { type: 4, name: "atk" }];
  strikeBlockBreak(t3a, true, []);
  eq("S3 blanket hT(5)", [t3a.length, Nbb(t3a)], [1, false]);
  const t3b = [{ type: 5, name: "blk" }, { type: 5, name: "parry" }, { type: 4, name: "atk" }];
  strikeBlockBreak(t3b, true, ["blk"]);
  eq("S3 named Hja", [t3b.length, Nbb(t3b)], [2, true]); // parry survives -> still blocked
  out.scenarios.push({ id: "S3-ignoreblock", blanketLeft: t3a.length, namedLeft: t3b.length });

  // S4: crit via se roll — a=iya*hya=0.05*2=0.1, b=20/2=10 -> 1.0>0.3
  const R4 = r8a({ ecb: false, wdVc: false, Zi: 20, atkSo: 2, ws: false, sr: 0, oaVc: false, threshold: 100, iyaBase: 0.05, hyaVal: 2, pDaBase: 0.1, oDaVal: 1, se: true, Uq: false, block: false, rCrit: 0.3, rHead: 0.3 });
  eq("S4 raw=E", [R4.raw, R4.critE, R4.headF, R4.painShock], [true, true, false, false]);
  const Bb4 = { JP: "SLOW_DEF", aI: "critanim", se: true, Ub: R4.raw, Yi: R4.raw, block: false, bR: 20, Zi: 20, ep: false, Iza: false };
  const G4 = cgbGuards(Bb4, { a3: false, gd: 100, fightNone: false, wdVc: false, sn: false, auHdName: "fists", ownHdName: "sword", Dga: false });
  eq("S4 shock latched", [G4.Bb.Ub, G4.wdVc], [true, true]);
  eq("S4 disarm armed", [G4.Bb.Yi, G4.sn, G4.kwbFired], [true, true, true]);
  eq("S4 stage7", sbaFlags(G4.Bb), { Defense: "SLOW_DEF", Animation: "critanim", Critical: true, Shock: true, Block: false, Damage: 20 });
  out.scenarios.push({ id: "S4-crit-disarm", stage7: sbaFlags(G4.Bb) });

  // S5: shock accumulation — 40/40/40 vs threshold 100; weapon strikes add 0
  let st5 = { sr: 0, oaVc: false, threshold: 100 };
  const seq5 = [40, 40, 40].map(x => orb(st5, x).shock);
  eq("S5 pain seq", seq5, [false, false, true]);
  eq("S5 sr", st5.sr, 120);
  const st5w = { sr: 0, oaVc: false, threshold: 100 };
  orb(st5w, 0); // ws=true passes add=0 (caller-side: Orb(ws?0:b), L531)
  eq("S5 weapon adds 0", st5w.sr, 0);
  const st5d = { oaVc: false, Wx: -1, sr: 120, Xza: 30 };
  pnb(st5d);
  eq("S5 decay", st5d.sr, 90);
  // wdVc early-out: shocked target cannot be re-shocked via R8a
  const R5b = r8a({ ecb: false, wdVc: true, Zi: 50, atkSo: 1, ws: false, sr: 0, oaVc: false, threshold: 100, iyaBase: 9, hyaVal: 9, pDaBase: 9, oDaVal: 9, se: true, Uq: true, block: false, rCrit: 0, rHead: 0 });
  eq("S5 vc veto", R5b.raw, false);
  out.scenarios.push({ id: "S5-shock-meter", shocks: seq5 });

  // S6: disarm timer — Wx=MFa ticks to Wqb while !oaVc
  const st6 = { oaVc: false, Wx: 5, sr: 0, Xza: 0 };
  const fires = [];
  for (let i = 0; i < 7; i++) fires.push(pnb(st6));
  eq("S6 fire frame", fires, [null, null, null, null, null, "wqb", null]);
  eq("S6 Wx end", st6.Wx, -1);
  // second Yi while sn -> Yi=false (no re-arm)
  const Bb6 = { JP: "x", aI: "y", se: false, Ub: true, Yi: true, block: false, bR: 5, Zi: 5, ep: false, Iza: false };
  const G6 = cgbGuards(Bb6, { a3: false, gd: 100, fightNone: false, wdVc: true, sn: true, auHdName: "fists", ownHdName: "sword", Dga: true });
  eq("S6 no re-arm", [G6.Bb.Yi, G6.kwbFired, G6.Bb.Ub], [false, false, false]); // Ub cleared: wdVc already set
  eq("S6 ep latched", G6.Bb.ep, false); // Dga already true this round
  out.scenarios.push({ id: "S6-disarm-timer", fires });

  // S7: combo gates (HZa L500-501). aEa:true so the geometric stub is consulted
  // (aEa falsy = "no AttackingParts = always connects", L566-567; see S7h).
  const atk = { jga: false, iga: [], aEa: true };
  const mkCl = () => ({ dW: null });
  eq("S7a no-own-atk", hzaGate({ ownAtk: null, targetXj: [], clSt: mkCl(), GY: [], geoHit: true, skipImpulse: false }), { chained: false, impulse: false });
  eq("S7b invuln blocks", hzaGate({ ownAtk: atk, targetXj: [{ type: 6, name: "inv" }], clSt: mkCl(), GY: [], geoHit: true, skipImpulse: false }), { chained: false, impulse: false });
  const atkJ = { jga: true, iga: [], aEa: true };
  eq("S7c blanket bypass", hzaGate({ ownAtk: atkJ, targetXj: [{ type: 6, name: "inv" }], clSt: mkCl(), GY: [], geoHit: true, skipImpulse: false }), { chained: true, impulse: true });
  const atkN = { jga: true, iga: ["spin"], aEa: true };
  eq("S7d named bypass hit", hzaGate({ ownAtk: atkN, targetXj: [{ type: 6, name: "inv" }, { type: 0, name: "spin" }], clSt: mkCl(), GY: [], geoHit: true, skipImpulse: false }), { chained: true, impulse: true });
  eq("S7e named bypass miss", hzaGate({ ownAtk: atkN, targetXj: [{ type: 6, name: "inv" }], clSt: mkCl(), GY: [], geoHit: true, skipImpulse: false }), { chained: false, impulse: false });
  eq("S7f geo miss", hzaGate({ ownAtk: atk, targetXj: [], clSt: mkCl(), GY: [], geoHit: false, skipImpulse: false }), { chained: false, impulse: false });
  eq("S7g test-only", hzaGate({ ownAtk: atk, targetXj: [], clSt: mkCl(), GY: [], geoHit: true, skipImpulse: true }), { chained: true, impulse: false });
  // Cl one-shot: same atk object twice -> false; hob resets (hob(){dW=null}, L567)
  const cl = mkCl();
  clIa(cl, null, [], atk, true); eq("S7h oneshot", clIa(cl, null, [], atk, true).hit, false);
  eq("S7h noParts always", clIa(mkCl(), null, [], { aEa: false }, false).hit, true);
  out.scenarios.push({ id: "S7-combo-gates", ok: true });

  // S8: head-hit knockdown path f — Uq, unblocked, d*b>rHead, no se, pain low
  const R8 = r8a({ ecb: false, wdVc: false, Zi: 30, atkSo: 3, ws: false, sr: 0, oaVc: false, threshold: 100, iyaBase: 0, hyaVal: 0, pDaBase: 0.2, oDaVal: 2, se: false, Uq: true, block: false, rCrit: 0.9, rHead: 0.5 });
  // d=0.4, b=10 -> 4.0>0.5 -> f; c: sr=10<100 false; e: se false
  eq("S8 head path", [R8.raw, R8.headF, R8.critE, R8.painShock], [true, true, false, false]);
  // ...but blocked head -> f suppressed -> raw false
  const R8b = r8a({ ecb: false, wdVc: false, Zi: 30, atkSo: 3, ws: false, sr: 0, oaVc: false, threshold: 100, iyaBase: 0, hyaVal: 0, pDaBase: 0.2, oDaVal: 2, se: false, Uq: true, block: true, rCrit: 0.9, rHead: 0.5 });
  eq("S8b block kills head", R8b.raw, false);
  out.scenarios.push({ id: "S8-headhit", raw: R8.raw, blockedRaw: R8b.raw });

  // S9: bCa composition order (L513-514) with injected tables
  const B9 = bcaCore({
    KP: [], blocked: true, weaponXi: "", pYa: "BLOCK_DEF", lNa: "SLOW_DEF",
    raidACa: 0.25, raidAttr: 2, raidMax: 5,
    tgtAttrs: { BD: 1 }, VY, atkAttrs: { CH: 2 }, HZ, seFlag: true,
    Xb: 10, atkLy: 2, iea: 0.5, UZ: 1, capC2a: 100, ceaBp: 1.5, dta: 1, so: 2
  });
  eq("S9 JP recomputed", B9.JP, "BLOCK_DEF");
  near("S9 dmg", B9.dmg, 12 * 0.5 * 2 * 2 * Math.SQRT2 * 1.5 * 2, 1e-9);
  // unblocked, no flags -> kea=qea=1
  const B9b = bcaCore({
    KP: ["ARM"], blocked: false, weaponXi: "", pYa: "BLOCK_DEF", lNa: "SLOW_DEF",
    raidACa: 0, raidAttr: 0, raidMax: 5,
    tgtAttrs: { BD: 9 }, VY, atkAttrs: { CH: 9 }, HZ, seFlag: false,
    Xb: 8, atkLy: 0, iea: 1, UZ: 1, capC2a: 100, ceaBp: 1, dta: 1, so: 1
  });
  eq("S9b plain", [B9b.dmg, B9b.JP], [8, "ARM"]);
  out.scenarios.push({ id: "S9-damage", dmg: B9.dmg, JP: B9.JP });

  // S10: DK partition + sja picks (L673-674); deterministic sja stub
  const cands = [
    { id: "a", eb: false, animation: { Rha: false, priority: 1, id: "A" } },
    { id: "b", eb: true, animation: { Rha: false, priority: 5, id: "B" } },
    { id: "c", eb: true, animation: { Rha: true, priority: 3, id: "C" } },
    { id: "d", eb: true, animation: { Rha: false, priority: 5, id: "D" } },
  ];
  const sja0 = () => 0;
  const D10 = dkPartition(cands, false, sja0);
  eq("S10 d", D10.d, ["a", "c"]); // c==false: !eb (a) or Rha (c) usable, L674
  eq("S10 f", D10.f, ["b", "d"]); // equal priority appends (Aua: >= keep, > reset)
  eq("S10 g", D10.g, ["c"]);
  const D10c = dkPartition(cands, true, sja0); // c==true: all usable
  eq("S10c d all", D10c.d, ["a", "b", "c", "d"]);
  eq("S10 e pick", D10.e, "b");
  eq("S10 ukb", D10.ukb, "C");
  out.scenarios.push({ id: "S10-dk-partition", e: D10.e, ukb: D10.ukb });

  // S12: DK tail-branch matrix (L674). Objects carry the fields the tail reads:
  // MS/R1 (jJa), index (Nsb), animation.type/E_ (zY/jza).
  const mkA = () => ({ Ukb: null, zY: null, jza: null });
  const mkE = (id, o) => Object.assign({ id, MS: false, R1: 0, index: 0, E_: 0, animation: { type: "T" + id } }, o);
  {
    const rec = { pkb: [], nsb: [], jja: [], ukb: [] };
    // S12a: d nonempty, e null -> Pkb(d), no push
    const A1 = mkA(), x = mkE("x"), y = mkE("y");
    const R1 = dkTail(A1, [x], null, null, 0, rec);
    eq("S12a pkb", rec.pkb, [[x]]);
    eq("S12a no-push", R1.d, ["x"]);
    eq("S12a e null", R1.e, null);
    // S12b: d nonempty, e set -> push then Pkb
    const A2 = mkA();
    const R2 = dkTail(A2, [x], y, null, 0, rec);
    eq("S12b push+pkb", rec.pkb[1], [x, y]);
    // S12c: d empty, e null -> nothing fires
    const A3 = mkA(), nPkb = rec.pkb.length, nN = rec.nsb.length, nJ = rec.jja.length;
    dkTail(A3, [], null, null, 0, rec);
    eq("S12c silent", [rec.pkb.length, rec.nsb.length, rec.jja.length], [nPkb, nN, nJ]);
    eq("S12c a untouched", [A3.zY, A3.jza, A3.Ukb], [null, null, null]);
    // S12d: d empty, e MS -> jJa + zY/jza
    const A4 = mkA(), jm = mkE("m", { MS: true, R1: 7, animation: { type: "Atk" }, E_: 3 });
    dkTail(A4, [], jm, null, 0, rec);
    eq("S12d jja", rec.jja, [["m", 7]]);
    eq("S12d side", [A4.zY, A4.jza], ["Atk", 3]);
    // S12e: d empty, e non-MS -> Nsb + zY/jza
    const A5 = mkA(), ns = mkE("n", { MS: false, index: 5, animation: { type: "Blk" }, E_: 9 });
    dkTail(A5, [], ns, null, 0, rec);
    eq("S12e nsb", rec.nsb, [["n", 5]]);
    eq("S12e side", [A5.zY, A5.jza], ["Blk", 9]);
    // S12f: g pick -> Ukb recorded before branch
    const A6 = mkA();
    dkTail(A6, [], null, "Ganim", 0, rec);
    eq("S12f ukb", [rec.ukb, A6.Ukb], [["Ganim"], "Ganim"]);
    out.scenarios.push({ id: "S12-dk-tail", pkb: rec.pkb.length, nsb: rec.nsb, jja: rec.jja, ukb: rec.ukb });
  }

  // S11: misc verbatim — G0 map, kwb latch, SZa, health clamp, FightNone
  eq("S11 G0", ["Attack", "Block", "Invulnerable", "Invisible", "X"].map(feG0), [4, 5, 6, 7, 0]);
  const kw = { Wx: -1 }; kwb(kw, 5); kwb(kw, 5);
  eq("S11 kwb latch", kw.Wx, 5);
  eq("S11 SZa", [SZa([{ name: "x" }], ["y"]), SZa([{ name: "x" }], ["x"])], [false, true]);
  const Bb11 = { JP: "j", aI: "a", se: true, Ub: true, Yi: true, block: false, bR: 50, Zi: 50, ep: false, Iza: false };
  const G11 = cgbGuards(Bb11, { a3: false, gd: 20, fightNone: false, wdVc: false, sn: false, auHdName: "fists", ownHdName: "sword", Dga: false });
  eq("S11 clamp", [G11.Bb.Zi, G11.Bb.Iza], [20.01, true]);
  const G11b = cgbGuards({ JP: "j", aI: "a", se: true, Ub: true, Yi: true, block: false, bR: 5, Zi: 5, ep: false, Iza: false },
    { a3: false, gd: 100, fightNone: true, wdVc: false, sn: false, auHdName: "fists", ownHdName: "sword", Dga: false });
  eq("S11 fightnone", [G11b.Bb.se, G11b.Bb.Ub, G11b.Bb.Yi], [false, false, false]);
  out.scenarios.push({ id: "S11-misc", ok: true });

  out.selftest = { pass, fail, failures };
  out.verdict = fail === 0 ? "GREEN" : "RED";
  console.log(JSON.stringify(out, null, 1));
  if (fail !== 0) { console.error(`FAILURES: ${JSON.stringify(failures, null, 1)}`); process.exit(1); }
  else console.error(`GREEN: ${pass} asserts passed`);
}
