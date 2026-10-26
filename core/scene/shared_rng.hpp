#pragma once

// The ONE unseeded stream: JS `Math.random` (`at.Nlb` L115, `uf.OKa.RGa()`).
// The differential harness pins it to `mulberry32(0xC0FFEE)`
// (reference/traces/README.md `harness.mathRandom`), and the JS uses it for
// EVERYTHING that is not `Da.pg`:
//   - `uf.sja` (L115) - the `Gc.DK`/`Aua` picks (`e = f[uf.sja(f.length)]`) and
//     the `am.ab` RandomSound name pick;
//   - `uf.RJa` (L22104/L22107) - the crit/shock rolls in `wd.R8a`;
//   - `oa.eT` (L97603) - the particle systems: the LOCATION emitters
//     (`jh.update` -> `Nvb`, 2 + 8 draws per particle) and the hit sparks
//     (`av` ctor, 2 draws per particle).
// Sharing ONE stream is load-bearing: the AI's `uf.sja` pick lands on a
// different value depending on how many particle draws happened before it, so
// a private RNG in the particle layers desyncs every AI `Aua` pick.
namespace sf2::scene {

// JS `Math.random()` pinned to `mulberry32(0xC0FFEE)` (see `fight.cpp`).
float shared_math_random01();

// The differential harness re-pins the stream at the FIRST live fight frame
// (`trace_oracle.js` -> `reseedMathRandomForFight`): the navigation to the
// fight is wall-clock in the oracle and frame-stepped in the port, so the two
// sides consume a different number of draws before the fight (measured 725 vs
// 1176) and every AI `uf.sja` pick would be shifted by that constant. Both
// sides re-seed ONCE per process (the same idea as `Da.pg`'s
// `kRoundClockSeed`). Measured in-fight cadence after the re-pin: oracle
// 2.46 draws/frame, port 2.49.
constexpr std::uint32_t kFightMathRandomSeed = 0xC0FFEEu;
void reseed_shared_math_random(std::uint32_t seed);

}  // namespace sf2::scene
