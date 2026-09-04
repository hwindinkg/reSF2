#!/usr/bin/env node
/*
 * ai_golden.js — Phase 6 Node-harness golden (static-first methodology).
 *
 * Transcribes the PURE decision-math functions 1:1 from
 * reference/www/sf2.502f0946.js (same language — expressions are copied
 * verbatim, only the `this`/statics plumbing is stubbed). Every block cites
 * its JS line. Run: `node reference/tools/ai_golden.js` → JSON vectors on
 * stdout. The C++ mirror (`app/ai_golden`) prints the same vectors; the gate
 * is 0 divergences (PRNG/zones/roulette bit-exact; Gb within 1e-5 —
 * C++ uses float, JS doubles).
 *
 * HONEST SCOPE: this verifies the transcription + the C++ port agree. It
 * cannot catch a spec misreading shared by both (that stays Phase 8's
 * runtime-trace job).
 */
"use strict";

// ---- dz (L596-area? L2352-region helpers) ----
function dzDlb(a) { return Number((BigInt(a) & 4294967295n) + (BigInt(12345) & 4294967295n) & 4294967295n); }
function dzNr(a, b) { return Number((BigInt(a) & 4294967295n) * (BigInt(b) & 4294967295n) & 4294967295n); }

// ---- Xx (L2366): constructor(a){this.mf=0;this.sL(a)} ----
function Xx(seed) { this.mf = 0; this.sL(seed); }
Xx.prototype.sL = function (a) { this.mf = a; };                       // sL(a){this.mf=a}
Xx.prototype.Olb = function (a) { return dzDlb(dzNr(a, 1103515245)) % 2147483648; };
Xx.prototype.B0 = function () { return this.mf = this.Olb(this.mf); }; // B0(){return this.mf=this.Olb(this.mf)}

// ---- Rk (L2352) ----
function Rk(seed) { this.JGa = new Xx(seed); }
Rk.prototype.B0 = function () { return this.JGa.B0(); };
Rk.prototype.sL = function (a) { this.JGa.sL(a); };
Rk.prototype.jf = function () { return this.B0() / 2147483648 + this.B0() / 2147483648 / 2147483648; };
Rk.prototype.s4 = function (a) { return this.jf() * a; };
Rk.prototype.dT = function (a, b) { return a + this.s4(b - a); };
Rk.prototype.cT = function (a, b) { b == null && (b = 100); return this.s4(b) < a; };
// Da.cT shortcut (L2352): b==null&&(b=100);return a>b?!0:Da.pg.cT(a,b)
function daCT(pg, a, b) { b == null && (b = 100); return a > b ? true : pg.cT(a, b); }

// ---- cc.Gb (L646-647) + NYa/QYa (L647-648), arb (L648) ----
function arbFactorType(s) { return s === "Linear" ? 1 : s === "Exponential" ? 0 : 1; }
function ccGb(c, a) {
  let r = a.counter * c.b8 + a.Xb * c.l8 + (1 - a.o1) * c.Uqa + (1 - a.q1) * c.wqa +
    a.xY * c.Toa + a.cl * c.Mra + a.K2 * c.Zra + a.tf * c.V8 + a.pZ * c.Epa +
    a.Lya * c.kqa + c.Fk;
  // BM / mAa / zZ-Opa terms: zero in the golden vectors (no strike memory).
  return c.y8 === 0 ? ccNYa(c, r) : c.y8 === 1 ? ccQYa(c, r) : 0;
}
function ccNYa(c, a) { return 0 <= a ? c.BW + (c.eq - c.BW) * Math.pow(2, -a) : c.dV + (c.eq - c.dV) * Math.pow(2, a); }
function ccQYa(c, a) { return 0 <= a ? c.eq + (c.BW - c.eq) * Math.min(1, a) : c.eq + (c.dV - c.eq) * Math.min(1, -a); }

// ---- Md.I0 (L643): static I0(a,b){return Da.pg.dT(a,b)} ----
function mdI0(pg, a, b) { return pg.dT(a, b); }
// de.gfa (L597): Gc.gfa(a)+1 ; de.Aea (L597): Gc.Aea(a) ; both I0|0.
function deGfa(pg, lo, hi) { return (mdI0(pg, lo, hi) | 0) + 1; }
function deAea(pg, lo, hi) { return mdI0(pg, lo, hi) | 0; }

// ---- Ju.$_ frame pick (L648-649) + XAa horizon (L611) ----
function juFrame(fl, rda, count) {
  const k = fl - rda;
  if (k < 0 || k >= count) return -1;
  return k;
}
function horizonPass(wait, horizon) { return wait <= horizon; }
function dqb(pg, CZ, bda, tba) {
  let b = pg.jf();
  let a = CZ;
  if (b < CZ) return 2;
  a += bda; if (b < a) return 3;
  a += tba; return b < a ? 4 : 1;
}

// ---- Md.jL (L640) + iCa: weighted roulette; g=pg.s4(d); walk weights ----
function mdJL(pg, weights) {
  let d = 0, e = 0;
  for (; e < weights.length;) d += weights[e++];
  if (0 < d) {
    let g = pg.s4(d);
    for (e = 0, d = 0; e < weights.length;) {
      g -= weights[e++]; if (g < 0) return d; ++d;
    }
  }
  return -1;
}

// ---- QJa draw ORDER (L594-595): 1 discarded jf + tua/dua/Bpa/rqa/oqa,
// then Mu=yea (I0, untruncated), lN=j0 (I0|0). $x=gfa(Ol) in jwb (L596-597).
function qjaDraws(pg, muLo, muHi, lnLo, lnHi, rdLo, rdHi) {
  pg.jf();
  const o = { tua: pg.jf(), dua: pg.jf(), Bpa: pg.jf(), rqa: pg.jf(), oqa: pg.jf() };
  o.Mu = mdI0(pg, muLo, muHi);
  o.lN = mdI0(pg, lnLo, lnHi) | 0;
  o.x = deGfa(pg, rdLo, rdHi);
  return o;
}

// ---------------- vectors ----------------
const out = { prng: [], gb: [], dqb: [], jl: [], qja: [], gfa_aea: [] };

for (const seed of [0, 1, 12345, 2147483646]) {
  const pg = new Rk(seed);
  const b = [pg.B0(), pg.B0(), pg.B0()];
  const pg2 = new Rk(seed);
  const j = [pg2.jf(), pg2.jf()];
  const pg3 = new Rk(seed);
  out.prng.push({ seed, B0: b, jf: j, s4_10: pg3.s4(10), dT_2_8: new Rk(seed).dT(2, 8), cT_50: new Rk(seed).cT(50) });
}
// Gb vectors: linear + exponential, +/- totals (field names mirror cc.parse).
const lin = { eq: 0.2, b8: 0.5, l8: 0, Uqa: 0.1, wqa: 0, Toa: 0, Mra: 0, Zra: 0, V8: 0, Epa: 0, kqa: 0.3, Fk: 0.05, BW: 0.9, dV: 0.01, y8: 1 };
const exp = Object.assign({}, lin, { y8: 0 });
const feat = { counter: 2, Xb: 1, o1: 0.8, q1: 0.6, xY: 10, cl: 0, K2: 1, tf: 3, pZ: 5, Lya: 120 };
const featNeg = Object.assign({}, feat, { counter: -4 });
for (const [nm, c, f] of [["lin+", lin, feat], ["lin-", lin, featNeg], ["exp+", exp, feat], ["exp-", exp, featNeg]]) {
  out.gb.push({ name: nm, v: ccGb(c, f) });
}
// Unsaturated vectors (totals inside (limit/anti_limit) range).
const small = { eq: 0.2, b8: 0.5, l8: 0.1, Uqa: 0.05, wqa: 0.02, Toa: 0.01, Mra: 0, Zra: 0, V8: 0.03, Epa: 0, kqa: 0.05, Fk: 0.01, BW: 0.9, dV: 0.05, y8: 1 };
const featS = { counter: 0.2, Xb: 0.5, o1: 0.9, q1: 0.7, xY: 2, cl: 0, K2: 0, tf: 1, pZ: 0, Lya: 1 };
const featSN = Object.assign({}, featS, { counter: -0.6, Xb: -0.4 });
for (const [nm, c, f] of [["linS+", small, featS], ["linS-", small, featSN],
                           ["expS+", Object.assign({}, small, { y8: 0 }), featS],
                           ["expS-", Object.assign({}, small, { y8: 0 }), featSN]]) {
  out.gb.push({ name: nm, v: ccGb(c, f) });
}
// dqb zones with fixed curves (pure zone logic; curves bypassed via constants).
{
  const pg = new Rk(7);
  const draws = [];
  for (let i = 0; i < 12; i++) draws.push(dqb(pg, 0.2, 0.3, 0.25));
  out.dqb.push({ seed: 7, CZ: 0.2, bda: 0.3, tba: 0.25, zones: draws });
}
// jL roulette over separated weights (no knife-edge).
{
  const pg = new Rk(99);
  const picks = [];
  for (let i = 0; i < 6; i++) picks.push(mdJL(pg, [0.1, 0.5, 2.0, 0.0]));
  out.jl.push({ seed: 99, picks });
  const pg2 = new Rk(1234);
  const picks2 = [];
  for (let i = 0; i < 8; i++) picks2.push(mdJL(pg2, [1.0, 1.0, 1.0]));
  out.jl.push({ seed: 1234, picks: picks2 });
}
// QJa full draw order with unit ranges.
out.qja.push(Object.assign({ seed: 4242 }, qjaDraws(new Rk(4242), 0, 10, 0, 5, 1, 4)));
// gfa/Aea trunc + +1 semantics, incl. negative range.
{
  const pg = new Rk(5);
  out.gfa_aea.push({ seed: 5, gfa: deGfa(pg, 1, 4), aea: deAea(pg, 0, 3) });
  const pg2 = new Rk(6);
  out.gfa_aea.push({ seed: 6, gfa: deGfa(pg2, -2, 2), aea: deAea(pg2, -2, 2) });
}
// Ju frame pick + horizon filter vectors.
out.ju = [
  { fl: 7, rda: 4, count: 5, k: juFrame(7, 4, 5) },
  { fl: 4, rda: 4, count: 5, k: juFrame(4, 4, 5) },
  { fl: 3, rda: 4, count: 5, k: juFrame(3, 4, 5) },
  { fl: 9, rda: 4, count: 5, k: juFrame(9, 4, 5) },
  { fl: 4, rda: 4, count: 0, k: juFrame(4, 4, 0) },
  { wait: 12, horizon: 15, pass: horizonPass(12, 15) },
  { wait: 16, horizon: 15, pass: horizonPass(16, 15) },
  { wait: 15, horizon: 15, pass: horizonPass(15, 15) },
];

console.log(JSON.stringify(out));
