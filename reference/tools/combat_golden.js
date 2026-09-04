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
  // S14: exact Fh.lXa (L2054-2056 + Kx ctor; pk EAa order).
  // Verbatim C++ twin: sf2::scene::fh_lxa (fight.hpp). Replaces S13: the
  // additive reading is disproven by the lXa body (all terms gated by
  // Fh counters; fresh Fh pays base only).
  function prizeVk(a, kq) {
    kq == null && (kq = 0);
    let div = 1;
    for (let i = 0; i < kq; i++) div *= 10;
    return Math.ceil(a / div);
  }
  function fhLxa(fh, prize, coins, gems, Ia, epF, UiF, UbF, pk, kq) {
    const oc = { rva: 0, py: 0, oy: 0, p3: 0, ep: 0, ui: 0, dz: 0, ub: 0, m6: 0, mOa: 0 };
    oc.rva += prize;
    oc.py += prizeVk(coins, kq);
    oc.oy += gems;
    oc.p3 += prizeVk(Math.trunc(Math.ceil(prize * Ia) * fh.d6 + .5), kq);
    oc.ep += prizeVk(Math.trunc(Math.ceil(prize * epF) * fh.c6 + .5), kq);
    oc.ui += prizeVk(Math.trunc(Math.ceil(prize * UiF) + .5), kq) * fh.jU;
    oc.dz += prizeVk(Math.trunc(Math.ceil(prize * pk[fh.b6]) + .5), kq);
    oc.ub += prizeVk(Math.trunc(Math.ceil(prize * UbF) * fh.e6 + .5), kq);
    oc.m6 = oc.py + oc.p3 + oc.ep + oc.ui + oc.dz + oc.ub;
    oc.mOa = oc.oy;
    return oc;
  }
  const PK = [0, 3, 6, 9, 12, 15];
  const lxa0 = fhLxa({ d6: 0, c6: 0, jU: 0, e6: 0, b6: 0 }, 70, 70, 0, 5, 2, 1, 3, PK, 0);
  eq("S14 fresh m6", lxa0.m6, 70);
  const lxa1 = fhLxa({ d6: 0, c6: 2, jU: 0, e6: 0, b6: 0 }, 70, 70, 0, 5, 2, 1, 3, PK, 0);
  eq("S14 c6 m6", lxa1.m6, 350);
  const lxa2 = fhLxa({ d6: 0, c6: 0, jU: 0, e6: 1, b6: 0 }, 70, 70, 0, 5, 2, 1, 3, PK, 0);
  eq("S14 e6 m6", lxa2.m6, 280);
  const lxa3 = fhLxa({ d6: 0, c6: 0, jU: 0, e6: 0, b6: 0 }, 70, 70, 5, 5, 2, 1, 3, PK, 1);
  eq("S14 kq m6", lxa3.m6, 7);
  out.scenarios.push({ id: "S14-lxa", m6: [lxa0.m6, lxa1.m6, lxa2.m6, lxa3.m6] });
  // S15: HUD style meter (L2090-2092; TNa=0.5/tya=0.08/ZIa=2/SNa=1).
  // Verbatim C++ twin: style_credit/style_vma/style_decay (fight.hpp).
  function styleCredit(t, st, anim, rna) {
    let b = 0;
    if (anim in st.vs) { b = st.vs[anim] + 1; st.vs[anim] = b; }
    else st.vs[anim] = 0;
    const sna = t.sna[Math.min(Math.max(0, st.level), t.sna.length - 1)];
    let pow = 1;
    for (let i = 0; i < b; i++) pow /= t.zia;
    return t.tna * sna * pow * rna;
  }
  function styleVma(st, credit, levels) {
    const total = st.frac + credit;
    let b = st.level + Math.trunc(total), frac = total - Math.floor(total);
    if (b >= levels) { b = levels - 1; frac = 1.0; }
    if (b < 0) { b = 0; frac = 0.0; }
    st.level = b; st.frac = frac;
    if (st.level > st.best) st.best = st.level;
  }
  function styleDecay(st, tya) { st.frac = Math.max(0, st.frac - tya / 60); }
  const ST = { tna: 0.5, tya: 0.08, zia: 2, sna: [1, 1, 1, 1, 1, 1] };
  const stm = { level: 0, frac: 0, vs: {}, best: 0 };
  const c15a = styleCredit(ST, stm, "HighPunch", 1.1);
  styleVma(stm, c15a, 6);
  const c15b = styleCredit(ST, stm, "HighPunch", 1.1);
  styleVma(stm, c15b, 6);
  styleDecay(stm, 0.08);
  eq("S15 credit1", c15a, 0.55);
  eq("S15 credit2", c15b, 0.275);
  eq("S15 level", [stm.level, stm.best], [0, 0]);
  eq("S15 frac", stm.frac, 0.55 + 0.275 - 0.08 / 60);
  styleVma(stm, 2.5, 6);
  eq("S15 levelup", [stm.level, stm.best], [3, 3]);
  out.scenarios.push({ id: "S15-style", credit: [c15a, c15b],
    level: stm.level, best: stm.best, frac: stm.frac });
  // S16: perk hit-action decider (PERKS_STATIC 5.2, L1290-1300).
  // Verbatim C++ twin: decide_hit_perks/tick_active_mods (perks.hpp).
  function decideHitPerks(perks, rec) {
    const o = { fc: false, hc: false, fb: false, hb: false, fs: false, hs: false,
      fd: false, hd: false, dmg: 0, add: 0, heal: 0, ix: 1, iy: 1, iz: 1,
      attrs: [], clears: [], collOff: false, dots: [], log: [] };
    const num = (a, k, d) => (k in a.num ? a.num[k] : d);
    for (const a of perks) {
      const t = a.type;
      if (t === "SetHit") {
        if ("Critical" in a.num) { o.fc = a.num.Critical !== 0; o.hc = true; }
        if ("Block" in a.num) { o.fb = a.num.Block !== 0; o.hb = true; }
        if ("Shock" in a.num) { o.fs = a.num.Shock !== 0; o.hs = true; }
        if ("Disarm" in a.num) { o.fd = a.num.Disarm !== 0; o.hd = true; }
        if ("Damage" in a.num) { o.dmg = a.num.Damage; o.hdmg = true; }
      } else if (t === "Lifesteal") {
        o.heal += num(a, "DamagePart", 0) * rec.final_damage;
      } else if (t === "ChangeAdditionalDamageValue") {
        o.add += num(a, "Value", 0);
      } else if (t === "ChangeImpulse") {
        o.ix = num(a, "MultiplierX", 0); o.iy = num(a, "MultiplierY", 0);
        o.iz = num(a, "MultiplierZ", 0);
      } else if (t === "ModAttributes") {
        for (const k of Object.keys(a.num)) o.attrs.push([k, a.num[k]]);
      } else if (t === "DisableInterval") {
        let ty = -1;
        if (a.str.IntervalType === "Attack") ty = 4;
        else if (a.str.IntervalType === "Block") ty = 5;
        else if (a.str.IntervalType === "Invulnerable") ty = 6;
        else if (a.str.IntervalType === "Invisible") ty = 7;
        o.clears.push([ty, a.str.IntervalName || ""]);
      } else if (t === "TurnOffCollision") {
        o.collOff = num(a, "Off", 1) !== 0;
      } else if (t === "ModHealthChange") {
        o.dots.push({ name: a.str.Name || "dot",
          frames: num(a, "Frames", 60) | 0, per: num(a, "PerFrameValue", 0) });
      } else {
        o.log.push("perknoop " + t);
      }
    }
    return o;
  }
  function tickDots(dots, hp, maxHp) {
    for (let i = 0; i < dots.length;) {
      hp += dots[i].per;
      if (hp < 0) hp = 0;
      if (hp > maxHp) hp = maxHp;
      if (--dots[i].frames <= 0) { dots[i] = dots[dots.length - 1]; dots.pop(); }
      else i++;
    }
    return hp;
  }
  const rec16 = { final_damage: 10 };
  const perks16 = [
    { type: "SetHit", num: { Critical: 1, Damage: 25 }, str: {} },
    { type: "Lifesteal", num: { DamagePart: 0.5 }, str: {} },
    { type: "ChangeAdditionalDamageValue", num: { Value: 3 }, str: {} },
    { type: "ChangeImpulse", num: { MultiplierX: 2, MultiplierZ: 0.5 }, str: {} },
    { type: "ModAttributes", num: { ShockCriticalHitChance: 0.1 }, str: {} },
    { type: "DisableInterval", num: {}, str: { IntervalType: "Block" } },
    { type: "ModHealthChange", num: { Frames: 3, PerFrameValue: -2 }, str: { Name: "burn" } },
    { type: "Switch", num: {}, str: {} },
    { type: "AddBullets", num: {}, str: {} },
  ];
  const o16 = decideHitPerks(perks16, rec16);
  eq("S16 sethit", [o16.fc, o16.hc, o16.dmg, o16.hdmg], [true, true, 25, true]);
  eq("S16 lifesteal", o16.heal, 5);
  eq("S16 add", o16.add, 3);
  eq("S16 impulse", [o16.ix, o16.iy, o16.iz], [2, 0, 0.5]);
  eq("S16 attrs", o16.attrs, [["ShockCriticalHitChance", 0.1]]);
  eq("S16 clears", o16.clears, [[5, ""]]);
  eq("S16 dot", [o16.dots[0].name, o16.dots[0].frames, o16.dots[0].per],
    ["burn", 3, -2]);
  eq("S16 noop", o16.log, ["perknoop Switch", "perknoop AddBullets"]);
  const dots16a = o16.dots.map(d => ({ ...d }));
  let hp16 = 50;
  hp16 = tickDots(dots16a, hp16, 100);
  eq("S16 tick1", [hp16, dots16a.length, dots16a[0].frames], [48, 1, 2]);
  const dots16b = [{ name: "burn", frames: 3, per: -2 }];
  let h = 50;
  h = tickDots(dots16b, h, 100); h = tickDots(dots16b, h, 100);
  h = tickDots(dots16b, h, 100);
  eq("S16 expiry", [h, dots16b.length], [44, 0]);
  out.scenarios.push({ id: "S16-perks",
    sethit: [o16.fc, o16.hc, o16.dmg, o16.hdmg], heal: o16.heal, add: o16.add,
    impulse: [o16.ix, o16.iy, o16.iz], attrs: o16.attrs, clears: o16.clears,
    dot: [o16.dots[0].name, o16.dots[0].frames, o16.dots[0].per],
    noop: o16.log, tickHp: hp16, expiry: [h, dots16b.length] });
  // S17: Bl.strike split (L582): b=min(1,|hit-p1|/rest), full-vector
  // node displacements, knockback decay. Verbatim C++ twin: apply_impulse
  // + decay_knockback (physics.hpp).
  function blSplit(p1, hit, rest, w1, w2, imp) {
    const dist = Math.hypot(hit.x - p1.x, hit.y - p1.y);
    const b = rest < 1e-6 ? 1 : Math.min(1, dist / rest);
    return { b,
      n1: { x: imp.x * (1 - b) / w1, y: imp.y * (1 - b) / w1 },
      n2: { x: imp.x * b / w2, y: imp.y * b / w2 } };
  }
  function kbDecay(offs, f) {
    return offs.map(v => {
      const r = { x: v.x * f, y: v.y * f };
      if (r.x * r.x + r.y * r.y < 1e-6) { r.x = 0; r.y = 0; }
      return r;
    });
  }
  const s17 = blSplit({ x: 0, y: 0 }, { x: 30, y: 0 }, 100, 2, 1, { x: 10, y: 4 });
  eq("S17 b", s17.b, 0.3);
  eq("S17 n1", [s17.n1.x, s17.n1.y], [3.5, 1.4]);
  eq("S17 n2", [s17.n2.x, s17.n2.y], [3, 1.2]);
  const s17c = blSplit({ x: 0, y: 0 }, { x: 500, y: 0 }, 100, 2, 1, { x: 10, y: 4 });
  eq("S17 clamp", [s17c.b, s17c.n1.x, s17c.n2.x], [1, 0, 10]);
  const s17d = kbDecay([{ x: 10, y: 0 }, { x: 0, y: 5 }], 0.9);
  eq("S17 decay", [s17d[0].x, s17d[0].y, s17d[1].x, s17d[1].y], [9, 0, 0, 4.5]);
  const s17e = kbDecay([{ x: 1e-4, y: 0 }], 0.9);
  eq("S17 decayzero", [s17e[0].x, s17e[0].y], [0, 0]);
  out.scenarios.push({ id: "S17-blstrike",
    b: s17.b, n1: [s17.n1.x, s17.n1.y], n2: [s17.n2.x, s17.n2.y],
    clamp: [s17c.b, s17c.n1.x, s17c.n2.x],
    decay: [s17d[0].x, s17d[0].y, s17d[1].x, s17d[1].y],
    decayzero: [s17e[0].x, s17e[0].y] });
  // S17b: verbatim Bz (L12) incl. Cz-collinear (e=a start), raw-gb sum,
  // n$/o$ split. C++ twin: capsule_capsule_overlap.
  function bzLine(p, q) {
    const dx = p.x - q.x, dy = p.y - q.y, d = Math.hypot(dx, dy);
    const a = (p.y - q.y) / d, b = (q.x - p.x) / d;
    return { a, b, c: -(a * p.x + b * p.y) };
  }
  function czHit(a, b, c, d) {
    const z = v => Math.abs(v) < 1e-10;
    if ((z(a.x - b.x) && z(a.y - b.y)) || (z(c.x - d.x) && z(c.y - d.y))) return null;
    const f = b.x - a.x, g = b.y - a.y, h = d.x - c.x, k = d.y - c.y;
    const l = a.x - c.x, n = a.y - c.y, q = k * f - h * g;
    const hh = h * n - k * l, ff = f * n - g * l;
    if (z(q)) {
      if (z(hh) || z(ff)) {
        let q2, g2, l2, n2;
        if (a.x < b.x) { q2 = a.x; g2 = b.x; } else { q2 = b.x; g2 = a.x; }
        if (c.x < d.x) { l2 = c.x; n2 = d.x; } else { l2 = d.x; n2 = c.x; }
        if (q2 > n2 || l2 > g2) return null;
        if (a.y < b.y) { q2 = a.y; g2 = b.y; } else { q2 = b.y; g2 = a.y; }
        if (c.y < d.y) { l2 = c.y; n2 = d.y; } else { l2 = d.y; n2 = c.y; }
        if (q2 > n2 || l2 > g2) return null;
        return { x: a.x, y: a.y };
      }
      return null;
    }
    const hq = hh / q;
    const inR = (v, x, y) => (v >= x && v <= y) || (v >= y && v <= x);
    if (!inR(hq, 0, 1) || !inR(ff / q, 0, 1)) return null;
    return { x: a.x + hq * (b.x - a.x), y: a.y + hq * (b.y - a.y) };
  }
  function bz(atk, tgt) {
    const a = tgt.p1, b = tgt.p2, d = atk.p1, e = atk.p2;
    const c = tgt.r + atk.r;
    const inR = (v, x, y) => (v >= x && v <= y) || (v >= y && v <= x);
    const z = v => Math.abs(v) < 1e-10;
    const w8 = { x: 0, y: 0 }, x8 = { x: 0, y: 0 };
    if (z(c)) {
      const p = czHit(a, b, d, e);
      if (p) return { hit: true, n: p, o: p };
      return { hit: false };
    }
    const l = bzLine(atk.r1, atk.r2), k = bzLine(tgt.r1, tgt.r2);
    const n = l.a * a.x + l.b * a.y + l.c, f = l.a * b.x + l.b * b.y + l.c;
    if (n * f >= 0 && c < Math.abs(n) && c < Math.abs(f)) return { hit: false };
    const q = k.a * d.x + k.b * d.y + k.c, r = k.a * e.x + k.b * e.y + k.c;
    if (q * r >= 0 && c < Math.abs(q) && c < Math.abs(r)) return { hit: false };
    if (q * r < 0 && n * f < 0) {
      const t = q / (q - r);
      const g = { x: d.x + t * (e.x - d.x), y: d.y + t * (e.y - d.y) };
      return { hit: true, n: g, o: g };
    }
    const proj = (s, pt, seg) => {
      if (Math.abs(s) > c) return null;
      const o = { x: pt.x - s * (seg === l ? l.a : k.a),
        y: pt.y - s * (seg === l ? l.b : k.b) };
      const ok = (inR(o.x, seg === l ? d.x : a.x, seg === l ? e.x : b.x) &&
        inR(o.y, seg === l ? d.y : a.y, seg === l ? e.y : b.y)) ||
        (Math.hypot(pt.x - (seg === l ? d.x : a.x), pt.y - (seg === l ? d.y : a.y)) <= c) ||
        (Math.hypot(pt.x - (seg === l ? e.x : b.x), pt.y - (seg === l ? e.y : b.y)) <= c);
      return ok ? o : null;
    };
    let o = proj(n, a, l);
    if (o) return { hit: true, n: { x: a.x, y: a.y }, o };
    o = proj(f, b, l);
    if (o) return { hit: true, n: { x: b.x, y: b.y }, o };
    o = proj(q, d, k);
    if (o) return { hit: true, n: { x: d.x, y: d.y }, o };
    o = proj(r, e, k);
    if (o) return { hit: true, n: { x: e.x, y: e.y }, o };
    return { hit: false };
  }
  const capA = { p1: { x: 0, y: 0 }, p2: { x: 10, y: 0 }, r: 1,
    r1: { x: 0, y: 0 }, r2: { x: 10, y: 0 } };
  const capX = { p1: { x: 5, y: -5 }, p2: { x: 5, y: 5 }, r: 1,
    r1: { x: 5, y: -5 }, r2: { x: 5, y: 5 } };
  const capN = { p1: { x: 9, y: 0.5 }, p2: { x: 20, y: 0.5 }, r: 1,
    r1: { x: 9, y: 0.5 }, r2: { x: 20, y: 0.5 } };
  const capM = { p1: { x: 50, y: 0 }, p2: { x: 60, y: 0 }, r: 1,
    r1: { x: 50, y: 0 }, r2: { x: 60, y: 0 } };
  const capC = { p1: { x: 3, y: 0 }, p2: { x: 20, y: 0 }, r: 0,
    r1: { x: 3, y: 0 }, r2: { x: 20, y: 0 } };
  const capZ = { p1: { x: 0, y: 0 }, p2: { x: 10, y: 0 }, r: 0,
    r1: { x: 0, y: 0 }, r2: { x: 10, y: 0 } };
  const bx = bz(capA, capX);
  const bn = bz(capA, capN);
  const bm = bz(capA, capM);
  const bc = bz(capZ, capC);
  eq("S17b cross", [bx.hit, bx.n.x, bx.n.y, bx.o.x, bx.o.y],
    [true, 5, 0, 5, 0]);
  eq("S17b near", [bn.hit, bn.n.x, bn.n.y, bn.o.x, bn.o.y],
    [true, 9, 0.5, 9, 0]);
  eq("S17b miss", [bm.hit], [false]);
  eq("S17b collinear", [bc.hit, bc.n.x, bc.n.y, bc.o.x, bc.o.y],
    [true, 3, 0, 3, 0]);
  out.scenarios.push({ id: "S17b-bz",
    cross: [bx.hit, bx.n.x, bx.n.y, bx.o.x, bx.o.y],
    near: [bn.hit, bn.n.x, bn.n.y, bn.o.x, bn.o.y],
    miss: [bm.hit],
    collinear: [bc.hit, bc.n.x, bc.n.y, bc.o.x, bc.o.y] });  // S18: trigger match + conditions (PERKS 5.3/5.5). Verbatim C++ twin:
  // match_hit_event / eval_cond (trigger.hpp).
  function matchHit(e, v, entry, fired) {
    if (e.ob === 1 && entry !== fired) return false;
    if (e.ob === 2 && entry === fired) return false;
    const num = (k, d) => (k in v.num ? v.num[k] : d);
    const str = k => (k in v.str ? v.str[k] : "");
    if (e.defense && e.defense !== str("Defense")) return false;
    if (e.animation && (str("Animation") === "" || str("Animation") !== e.animation)) return false;
    if (e.critical > -1 && e.critical !== (num("Critical", 0) !== 0 ? 1 : 0)) return false;
    if (e.shock > -1 && e.shock !== (num("Shock", 0) !== 0 ? 1 : 0)) return false;
    if (e.block > -1 && e.block !== (num("Block", 0) !== 0 ? 1 : 0)) return false;
    const dmg = num("Damage", 0);
    if (e.dmgMin > -1 && dmg < e.dmgMin) return false;
    if (e.dmgMax > -1 && dmg > e.dmgMax) return false;
    return true;
  }
  function evalCond(c, owner, foe) {
    const m = c.ob === 2 ? foe : owner;
    const other = c.ob === 2 ? owner : foe;
    let r = false;
    if (c.kind === "PerkStart") r = true;
    else if (c.kind === "Random") r = c.chance >= 1 || m.draw < c.chance;
    else if (c.kind === "Style") {
      const lv = { Turtle: 0, Hard: 1, Brutal: 2, Aggressive: 3, Crazy: 4, Fantastic: 5 };
      const lo = c.min in lv ? lv[c.min] : 0, hi = c.max in lv ? lv[c.max] : 5;
      r = m.style >= lo && m.style <= hi;
    }
    else if (c.kind === "Combo") r = (c.min === null || m.combo >= c.min) &&
      (c.max === null || m.combo <= c.max);
    else if (c.kind === "Health") r = (c.min === null || m.hp >= c.min) &&
      (c.max === null || m.hp <= c.max);
    else if (c.kind === "ModExists") {
      if (c.ns) {
        if (!c.name) r = Object.values(m.modsNs).includes(c.ns);
        else r = m.modsNs[c.name] === c.ns;
      } else r = m.mods.includes(c.name);
    }
    else if (c.kind === "Round") r = m.round === c.n;
    else if (c.kind === "Bullets") {
      if (c.sType === "MagicBullet") {
        r = (c.min === null || m.bullets >= c.min) &&
          (c.max === null || m.bullets <= c.max);
      } else if (c.sType === "RaidChargeBullet") {
        r = (c.min === null || m.raid >= c.min) &&
          (c.max === null || m.raid <= c.max);
      } else r = false;
    }
    else if (c.kind === "MagicCharge") r = (c.min === null || m.charge >= c.min) &&
      (c.max === null || m.charge <= c.max);
    else if (c.kind === "Operator") {
      if (c.op === "Or") r = c.nested.some(n => evalCond(n, owner, foe));
      else r = c.nested.every(n => evalCond(n, owner, foe));
    } else if (["Less", "LessEqual", "Greater", "GreaterEqual", "Equal"].includes(c.kind)) {
      r = evalCmp(c, m, other);
    }
    return c.neg ? !r : r;
  }
  function evalOperand(t, m, o) {
    // minimal twin: number | ?PlayerParameter[Me|Enemy].Health |
    // ?Hit[].Damage | ?Variable[name] | ?Abs[x] | +,-,*,/,(,)
    const num = s => { const v = parseFloat(s); return isNaN(v) ? null : v; };
    function qref(s, pos) {
      let i = pos + 1, name = "";
      while (i < s.length && /[A-Za-z0-9_]/.test(s[i])) name += s[i++];
      while (s[i] === " ") i++;
      let arg = "";
      if (s[i] === "[") {
        let d = 1; i++;
        const st = i;
        while (i < s.length && d > 0) {
          if (s[i] === "[") d++;
          if (s[i] === "]") d--;
          i++;
        }
        arg = s.slice(st, i - 1);
      }
      while (s[i] === " ") i++;
      let field = "";
      if (s[i] === ".") {
        i++;
        while (i < s.length && /[A-Za-z0-9_]/.test(s[i])) field += s[i++];
      }
      if (name === "Abs") {
        const v = expr(arg, 0).v;
        return v === null ? null : { v: Math.abs(v), i };
      }
      if (name === "Hit") return field === "Damage" ? { v: m.hit, i } : null;
      if (name === "Variable") {
        const v = m.q3[arg];
        return { v: v === undefined ? 0 : v, i };
      }
      if (name === "PlayerParameter") {
        if (arg !== "Me" && arg !== "Enemy") return null;
        const mm = arg === "Enemy" ? o : m;
        if (field === "Health") return { v: mm.hp, i };
        if (field === "MagicBullet") return { v: 0, i };
        return null;
      }
      return null;
    }
    function expr(s, pos) {
      const term = (s, pos) => {
        const fact = (s, pos) => {
          while (s[pos] === " ") pos++;
          if (s[pos] === "-") {
            const r = fact(s, pos + 1);
            return r.v === null ? r : { v: -r.v, i: r.i };
          }
          if (s[pos] === "(") {
            const r = expr(s, pos + 1);
            if (r.v === null || s[r.i] !== ")") return { v: null, i: r.i };
            return { v: r.v, i: r.i + 1 };
          }
          if (s[pos] === "?") return qref(s, pos);
          let j = pos;
          while (j < s.length && /[0-9.]/.test(s[j])) j++;
          if (j === pos) return { v: null, i: pos };
          const v = num(s.slice(pos, j));
          return v === null ? { v: null, i: j } : { v, i: j };
        };
        let r = fact(s, pos);
        if (r.v === null) return r;
        for (;;) {
          while (s[r.i] === " ") r.i++;
          if (s[r.i] === "*") {
            const q = fact(s, r.i + 1);
            if (q.v === null) return q;
            r = { v: r.v * q.v, i: q.i };
          } else if (s[r.i] === "/") {
            const q = fact(s, r.i + 1);
            if (q.v === null || q.v === 0) return { v: null, i: q.i };
            r = { v: r.v / q.v, i: q.i };
          } else return r;
        }
      };
      let r = term(s, pos);
      if (r.v === null) return r;
      for (;;) {
        while (s[r.i] === " ") r.i++;
        if (s[r.i] === "+") {
          const q = term(s, r.i + 1);
          if (q.v === null) return q;
          r = { v: r.v + q.v, i: q.i };
        } else if (s[r.i] === "-") {
          const q = term(s, r.i + 1);
          if (q.v === null) return q;
          r = { v: r.v - q.v, i: q.i };
        } else return r;
      }
    }
    const r = expr(t, 0);
    let j = r.i;
    while (t[j] === " ") j++;
    return j !== t.length ? null : r.v;
  }
  function evalCmp(c, m, o) {
    const v1 = evalOperand(c.v1, m, o), v2 = evalOperand(c.v2, m, o);
    if (v1 === null || v2 === null) return false;
    if (c.kind === "Less") return v1 < v2;
    if (c.kind === "LessEqual") return v1 <= v2;
    if (c.kind === "Greater") return v1 > v2;
    if (c.kind === "GreaterEqual") return v1 >= v2;
    return v1 === v2;
  }
  const ev18 = { ob: 1, defense: "", animation: "", critical: 1, shock: -1,
    block: -1, dmgMin: 5, dmgMax: -1 };
  const vars18 = { num: { Critical: 1, Block: 0, Damage: 10 }, str: {} };
  const m18a = matchHit(ev18, vars18, 0, 0);
  const m18b = matchHit(ev18, { num: { Critical: 0, Damage: 10 }, str: {} }, 0, 0);
  const m18c = matchHit(ev18, { num: { Critical: 1, Damage: 3 }, str: {} }, 0, 0);
  const m18d = matchHit(ev18, vars18, 0, 1);
  eq("S18 match", [m18a, m18b, m18c, m18d], [true, false, false, false]);
  const own18 = { style: 2, combo: 3, hp: 50, round: 2, draw: 0.2, mods: ["Icon"],
    modsNs: { Icon: "" }, hit: 15, q3: { X: 1 } };
  const foe18 = { style: 0, combo: 0, hp: 100, round: 2, draw: 0.9, mods: [],
    modsNs: {}, hit: 15, q3: {} };
  const c18 = [
    evalCond({ kind: "Random", chance: 0.3, neg: false }, own18, foe18),
    evalCond({ kind: "Random", chance: 0.3, neg: false }, foe18, own18),
    evalCond({ kind: "Style", min: "Brutal", max: "Crazy", neg: false }, own18, foe18),
    evalCond({ kind: "Combo", min: 1, max: 5, neg: false }, own18, foe18),
    evalCond({ kind: "Health", min: null, max: 40, neg: false }, own18, foe18),
    evalCond({ kind: "ModExists", name: "Icon", neg: false }, own18, foe18),
    evalCond({ kind: "ModExists", name: "Icon", neg: true }, own18, foe18),
    evalCond({ kind: "Round", n: 2, neg: false }, own18, foe18),
    evalCond({ kind: "Operator", op: "Or", neg: false, nested: [
      { kind: "Round", n: 9, neg: false }, { kind: "PerkStart", neg: false }] },
      own18, foe18),
    evalCond({ kind: "Operator", op: "And", neg: false, nested: [
      { kind: "Round", n: 2, neg: false }, { kind: "Round", n: 9, neg: false }] },
      own18, foe18),
    evalCond({ kind: "LessEqual", v1: "?PlayerParameter[Enemy].Health",
      v2: "60", neg: false }, own18, foe18),
    evalCond({ kind: "GreaterEqual", v1: "?Hit[].Damage", v2: "10", neg: false },
      own18, foe18),
    evalCond({ kind: "Equal", v1: "?Variable[Y]", v2: "0", neg: false }, own18, foe18),
    evalCond({ kind: "Less", v1: "?Abs[0-5]+10", v2: "20", neg: false }, own18, foe18),
    evalCond({ kind: "Greater", v1: "?Variable[X]+1", v2: "1", neg: false },
      own18, foe18),
    evalCond({ kind: "ModExists", name: "Act", ns: "NS", neg: false },
      { style: 0, combo: 0, hp: 1, round: 1, draw: 0.5, mods: ["Act"],
        modsNs: { Act: "NS" }, hit: 0, q3: {} }, foe18),
    evalCond({ kind: "ModExists", name: "Act", ns: "NS", neg: false },
      { style: 0, combo: 0, hp: 1, round: 1, draw: 0.5, mods: ["Act"],
        modsNs: { Act: "other" }, hit: 0, q3: {} }, foe18),
  ];
  eq("S18 conds", c18,
    [true, false, true, true, false, true, false, true, true, false,
      false, true, true, true, true, true, false]);
  out.scenarios.push({ id: "S18-trigger", match: [m18a, m18b, m18c, m18d], conds: c18 });
  // S18c: bus routing (Gj/v_a/UKa-lY): slot filter + event + conds gate,
  // drain yields the queued actions. C++ twin uses the real TrigBus.
  function busFire(triggers, slot, vars, fired, ctx0, ctx1, stage, frame) {
    const out = [[], []];
    const ctxs = [ctx0, ctx1];
    for (let s = 0; s < 2; s++) {
      for (const t of triggers[s]) {
        if (!t.enabled) continue;
        if (!t.events.some(e => e.type === slot)) continue;
        let gate = false;
        for (const e of t.events) {
          if (e.type !== slot) continue;
          const ev = { ob: e.ob, defense: e.defense || "", animation: e.animation || "",
            critical: e.critical ?? -1, shock: e.shock ?? -1, block: e.block ?? -1,
            dmgMin: e.dmgMin ?? -1, dmgMax: e.dmgMax ?? -1 };
          if (matchHit(ev, vars, s, fired)) { gate = true; break; }
        }
        if (!gate) continue;
        if (!t.conds.every(c => evalCond(c, ctxs[s], ctxs[1 - s]))) continue;
        for (const a of t.actions) out[s].push(a);
      }
    }
    return out;
  }
  const trig18 = { enabled: true,
    events: [{ type: 6, ob: 1, critical: 1 }],
    conds: [{ kind: "ModExists", name: "Icon", neg: true },
      { kind: "Random", chance: 0.3, neg: false }],
    actions: ["ModIcon"] };
  const ctxA = { style: 0, combo: 0, hp: 1, round: 1, draw: 0.2, mods: [] };
  const ctxB = { style: 0, combo: 0, hp: 1, round: 1, draw: 0.2, mods: [] };
  const varsC = { num: { Critical: 1, Block: 0, Damage: 4 }, str: {} };
  const q1 = busFire([[trig18], []], 6, varsC, 0, ctxA, ctxB, 2, 100);
  const q2 = busFire([[trig18], []],
    { num: { Critical: 0, Damage: 4 }, str: {} }, 0, ctxA, ctxB, 2, 100);
  const ctxC = { style: 0, combo: 0, hp: 1, round: 1, draw: 0.2, mods: ["Icon"] };
  const q3 = busFire([[trig18], []], 6, varsC, 0, ctxC, ctxB, 2, 100);
  eq("S18c route", [q1[0], q1[1], q2[0].length, q3[0].length],
    [["ModIcon"], [], 0, 0]);
  out.scenarios.push({ id: "S18c-bus", q: [q1[0], q1[1], q2[0].length, q3[0].length] });
  // S19: ranged/magic state (bh/dO/my, hZ/Hwa/LA, recharge, tables).
  // Verbatim C++ twin: bullets_add/charge_add/la_normalize/magic_aq/
  // magic_recharge + lp/sp eval (damage.hpp/trigger.hpp).
  const clamp01 = v => Math.min(1, Math.max(0, v));
  const hz = (bh, n) => bh + n;
  const hwa = (bh, my, v) => (bh === 0 ? clamp01(my + v) : my);
  const laNorm = (bh, my, noRepl) => {
    if (my >= 1 && !noRepl) { bh += 1; my = 0; }
    if (bh > 1) bh = 1;
    return [bh, my];
  };
  const recharge = (e, b, c, zi) => Math.pow(2, e) * b * c * zi;
  const magicAq = (base, attr) => (attr === null ? base : base * attr);
  const lpCtx = (bh, dO) => ({ style: 0, combo: 0, hp: 100, round: 1, draw: 0.5,
    mods: [], modsNs: {}, hit: 0, q3: {}, bullets: bh, raid: dO, charge: 0 });
  const spCtx = my => ({ style: 0, combo: 0, hp: 100, round: 1, draw: 0.5,
    mods: [], modsNs: {}, hit: 0, q3: {}, bullets: 0, raid: 0, charge: my });
  eq("S19 lp", [
    evalCond({ kind: "Bullets", sType: "MagicBullet", min: 1, max: null, neg: false },
      lpCtx(1, 0), foe18),
    evalCond({ kind: "Bullets", sType: "MagicBullet", min: 1, max: null, neg: false },
      lpCtx(0, 0), foe18),
    evalCond({ kind: "Bullets", sType: "RaidChargeBullet", min: null, max: 2, neg: false },
      lpCtx(0, 2), foe18),
    evalCond({ kind: "Bullets", sType: "X", min: null, max: null, neg: false },
      lpCtx(9, 9), foe18)], [true, false, true, false]);
  eq("S19 sp", [
    evalCond({ kind: "MagicCharge", min: null, max: 0.5, neg: false }, spCtx(0.7), foe18),
    evalCond({ kind: "MagicCharge", min: null, max: 0.5, neg: false }, spCtx(0.3), foe18)],
    [false, true]);
  out.scenarios.push({ id: "S19-magic",
    hz: hz(0, 1), hwa: [hwa(0, 0.2, 0.5), hwa(1, 0.2, 0.5)],
    la: [laNorm(0, 1.2, false), laNorm(0, 0.5, false),
      laNorm(3, 0, false), laNorm(0, 1.2, true)],
    recharge: recharge(1, 1, 1, 10),
    aq: [magicAq(0.0001, null), magicAq(0.0001, 10000)],
    lp: [
      evalCond({ kind: "Bullets", sType: "MagicBullet", min: 1, max: null, neg: false },
        lpCtx(1, 0), foe18),
      evalCond({ kind: "Bullets", sType: "MagicBullet", min: 1, max: null, neg: false },
        lpCtx(0, 0), foe18),
      evalCond({ kind: "Bullets", sType: "RaidChargeBullet", min: null, max: 2, neg: false },
        lpCtx(0, 2), foe18),
      evalCond({ kind: "Bullets", sType: "X", min: null, max: null, neg: false },
        lpCtx(9, 9), foe18)],
    sp: [
      evalCond({ kind: "MagicCharge", min: null, max: 0.5, neg: false }, spCtx(0.7), foe18),
      evalCond({ kind: "MagicCharge", min: null, max: 0.5, neg: false }, spCtx(0.3), foe18)] });
  // S20: modes resolve (tournament fight, survival waves, rewards, series).
  // Verbatim C++ twin: modes.hpp resolve_*/reward_for/advance_series.
  const pool20 = ["Ninja_A", "Ninja_B", "Ninja_C"];
  const wars20 = [{ template: "T0", number: 1, groups: [] },
    { template: "", number: 3, groups: [{ pool: pool20, random: true }] }];
  function resolveSurv(wars, wave, draw, used) {
    let w = wave, slot = null;
    for (const cand of wars) {
      if (w < cand.number) { slot = cand; break; }
      w -= cand.number;
    }
    if (!slot) return { ok: false };
    const out = { ok: true, template: slot.template };
    for (const g of slot.groups) {
      let pick = Math.floor(draw() * g.pool.length) % g.pool.length;
      out.template = g.pool[pick];
      break;
    }
    used.push(out.template);
    return out;
  }
  function rewardFor(type, n, wave, won) {
    if (type === "SURVIVAL") return Math.min(wave < 0 ? 0 : wave, n - 1);
    return (won && n > 1) ? 1 : 0;
  }
  function advance(type, nFights, totalWaves, s, won) {
    if (type === "SURVIVAL") {
      if (s.wave + 1 < totalWaves) return { ...s, wave: s.wave + 1, cont: true };
      return { ...s, cont: false };
    }
    if (won && s.fight + 1 < nFights) return { ...s, fight: s.fight + 1, cont: true };
    return { ...s, cont: false };
  }
  const sw0 = resolveSurv(wars20, 0, () => 0, []);
  const sw2 = resolveSurv(wars20, 2, () => 0.7, []);
  const sw9 = resolveSurv(wars20, 9, () => 0, []);
  eq("S20 surv", [sw0.ok, sw0.template, sw2.ok, sw2.template, sw9.ok],
    [true, "T0", true, "Ninja_C", false]);
  eq("S20 reward", [rewardFor("SURVIVAL", 10, 3, true), rewardFor("SURVIVAL", 10, 99, true),
    rewardFor("TOURNAMENT", 2, 0, true), rewardFor("TOURNAMENT", 2, 0, false),
    rewardFor("TOURNAMENT", 1, 0, true)], [3, 9, 1, 0, 0]);
  eq("S20 adv", [
    advance("SURVIVAL", 1, 4, { fight: 0, wave: 2 }, true).cont,
    advance("SURVIVAL", 1, 4, { fight: 0, wave: 3 }, true).cont,
    advance("TOURNAMENT", 3, 0, { fight: 1, wave: 0 }, true).cont,
    advance("TOURNAMENT", 3, 0, { fight: 1, wave: 0 }, false).cont,
    advance("TOURNAMENT", 3, 0, { fight: 2, wave: 0 }, true).cont,
  ], [true, false, true, false, false]);
  out.scenarios.push({ id: "S20-modes",
    surv: [sw0.ok, sw0.template, sw2.ok, sw2.template, sw9.ok],
    reward: [rewardFor("SURVIVAL", 10, 3, true), rewardFor("SURVIVAL", 10, 99, true),
      rewardFor("TOURNAMENT", 2, 0, true), rewardFor("TOURNAMENT", 2, 0, false),
      rewardFor("TOURNAMENT", 1, 0, true)],
    adv: [
      advance("SURVIVAL", 1, 4, { fight: 0, wave: 2 }, true).cont,
      advance("SURVIVAL", 1, 4, { fight: 0, wave: 3 }, true).cont,
      advance("TOURNAMENT", 3, 0, { fight: 1, wave: 0 }, true).cont,
      advance("TOURNAMENT", 3, 0, { fight: 1, wave: 0 }, false).cont,
      advance("TOURNAMENT", 3, 0, { fight: 2, wave: 0 }, true).cont] });
  out.scenarios.push({ id: "S11-misc", ok: true });

  out.selftest = { pass, fail, failures };
  out.verdict = fail === 0 ? "GREEN" : "RED";
  console.log(JSON.stringify(out, null, 1));
  if (fail !== 0) { console.error(`FAILURES: ${JSON.stringify(failures, null, 1)}`); process.exit(1); }
  else console.error(`GREEN: ${pass} asserts passed`);
}
