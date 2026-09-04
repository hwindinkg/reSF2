#!/usr/bin/env node
/*
 * spatial_golden.js — spawn / camera formula golden (static-first).
 *
 * Transcribes the PURE spawn/camera expressions 1:1 from
 * reference/www/sf2.502f0946.js (same language — expressions are copied
 * verbatim, only the object plumbing is stubbed). Every block cites its JS
 * line. Numbers come from read-only disk XML:
 *   reference/www/res/locations/dojo/dojo_params.*.xml (spawn + Width)
 *   reference/extracted/xml/res/internal_settings.xml (Camera node)
 * Run: `node reference/tools/spatial_golden.js` → JSON vectors on stdout,
 * self-test asserts on stderr, exit 0 = GREEN.
 *
 * HONEST SCOPE (same as combat_golden.js): this verifies the transcription
 * of the formulas. Per-frame values (animated body-part segments for Bz,
 * live camera state) need runtime traces and stay OPEN in COMBAT_STATIC.md.
 */
"use strict";

// ---- Bf spawn parse (L476): ModelsViewer Player/EnemyPosition{X,Y} ----
function parseSpawn(attrs) {
  return {
    Yia: { x: attrs.PlayerPositionX, y: attrs.PlayerPositionY, z: 0 }, // L476
    B_: { x: attrs.EnemyPositionX, y: attrs.EnemyPositionY, z: 0 },     // L476
  };
}

// ---- fight setup assignment (L381): kc.position=Yia, Zb.position=B_ ----
function spawnAssign(loc) {
  const kc = { x: loc.Yia.x, y: loc.Yia.y, z: loc.Yia.z };
  const Zb = { x: loc.B_.x, y: loc.B_.y, z: loc.B_.z };
  return { kc, Zb };
}

// ---- Bf.z9a midpoint (L475): ((Yia+B_)/2) ----
function z9a(Yia, B_) {
  const a = { x: Yia.x + B_.x, y: Yia.y + B_.y, z: Yia.z + B_.z };
  a.x *= 0.5; a.y *= 0.5; a.z *= 0.5;
  return a;
}

// ---- Bf.oCa half-extent (L475): (width/2, height/2, 0) ----
function oCa(width, height) { return { x: width / 2, y: height / 2, z: 0 }; }

// ---- camera kJa (L827): |b|+Vva>a ? -sign(b)(|b|-a+Vva)c : 0 ----
function kJa(a, b, c, Vva) {
  return Math.abs(b) + Vva > a ? -(b > 0 ? 1 : -1) * (Math.abs(b) - a + Vva) * c : 0;
}

// ---- camera clamp (L826-827): d=(Lb.width-oGa)*Bj*.5-nC*.5; Io∈[-d,d] ----
function camClamp(Io, LbWidth, oGa, Bj, nC) {
  const d = (LbWidth - oGa) * Bj * 0.5 - nC * 0.5;
  const a = Io, b = -d;
  return { d, Io: a < b ? b : a > d ? d : a };
}

// ---- arena clamp Nma (L391): c$=a-e$/2+width/2; d$=a+e$/2+width/2 ----
function nma(a, e$, width) {
  return { c$: a - e$ / 2 + width / 2, d$: a + e$ / 2 + width / 2 };
}

// ---- map widget scale constant (L2488) ----
const QE_UM = 1.5003663003663004;

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

{
  const out = { scenarios: [] };

  // S1: dojo spawn (disk: PlayerPositionX=690 Y=-93; EnemyX=973 Y=-110; Width=1960)
  const loc = parseSpawn({ PlayerPositionX: 690, PlayerPositionY: -93, EnemyPositionX: 973, EnemyPositionY: -110 });
  eq("S1 Yia", loc.Yia, { x: 690, y: -93, z: 0 });
  eq("S1 B_", loc.B_, { x: 973, y: -110, z: 0 });
  const sp = spawnAssign(loc);
  eq("S1 kc=Yia", sp.kc, { x: 690, y: -93, z: 0 });
  eq("S1 Zb=B_", sp.Zb, { x: 973, y: -110, z: 0 });
  out.scenarios.push({ id: "S1-dojo-spawn", kc: sp.kc, Zb: sp.Zb });

  // S2: midpoint z9a + half-extent oCa (dojo Width=1960 Height=560)
  eq("S2 mid", z9a(loc.Yia, loc.B_), { x: 831.5, y: -101.5, z: 0 });
  eq("S2 half", oCa(1960, 560), { x: 980, y: 280, z: 0 });
  out.scenarios.push({ id: "S2-midpoint", mid: z9a(loc.Yia, loc.B_) });

  // S3: kJa with Vva=100 (Camera BindingLength, internal_settings.xml)
  eq("S3 inside", kJa(500, 100, 2, 100), 0);                       // 100+100<500
  eq("S3 outside+", kJa(500, 450, 2, 100), -(450 - 500 + 100) * 2); // -100
  eq("S3 outside-", kJa(500, -450, 2, 100), 100);                   // sign flip
  eq("S3 boundary", kJa(500, 400, 3, 100), 0);                      // 400+100==500, not >
  out.scenarios.push({ id: "S3-kja", ok: true });

  // S4: camera clamp with oGa=50 (Camera MaxWidthDelta)
  const C4 = camClamp(2000, 960, 50, 1, 100);
  near("S4 d", C4.d, (960 - 50) * 0.5 - 50, 1e-9); // 405
  eq("S4 clamped", C4.Io, C4.d);
  const C4b = camClamp(100, 960, 50, 1, 100);
  eq("S4 inside", C4b.Io, 100);
  const C4c = camClamp(-5000, 960, 50, 1, 100);
  eq("S4 neg", C4c.Io, -C4c.d);
  out.scenarios.push({ id: "S4-camclamp", d: C4.d });

  // S5: Nma arena bounds (L391), dojo width 1960
  eq("S5 nma", nma(0, 200, 1960), { c$: -100 + 980, d$: 100 + 980 });
  eq("S5 nma-offset", nma(300, 400, 1960), { c$: 300 - 200 + 980, d$: 300 + 200 + 980 });
  out.scenarios.push({ id: "S5-nma", ok: true });

  // S6: qe.uM constant passthrough (L2488)
  eq("S6 uM", QE_UM, 1.5003663003663004);
  out.scenarios.push({ id: "S6-qe-um", uM: QE_UM });

  out.selftest = { pass, fail, failures };
  out.verdict = fail === 0 ? "GREEN" : "RED";
  console.log(JSON.stringify(out, null, 1));
  if (fail !== 0) { console.error(`FAILURES: ${JSON.stringify(failures, null, 1)}`); process.exit(1); }
  else console.error(`GREEN: ${pass} asserts passed`);
}
