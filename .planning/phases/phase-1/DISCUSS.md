# Discussion: Full Engine Reverse Engineering Plan

**Phase:** 1
**Date:** 2026-07-22
**Topic:** Full reverse engineering and 1:1 port of Shadow Fight 2 engine

## Preflight Evidence Used

- Tech stack: C++23, CMake, OpenGL ES 2.0, GLFW 3.4, ZLIB v1.3.1
- Prior decisions loaded: yes (PLAN_1TO1.md, PLAN_SYMBIAN.md, PLAN_PORT.md exist)
- Questions suppressed by evidence: 0 (only 1 question asked before proceeding to plan)

## Decisions

D-01: Target Game — Shadow Fight 2 (not Shadow Fight 1). User confirmed they meant SF2 when they said "shadow fight 1". The repo (reSF2) is already set up for SF2 reverse engineering with binaries, decompiled functions, and 40-50% engine implementation complete.

## Answered Recommendations

RQ-01: Which game are you targeting?
  Recommendation: Shadow Fight 2 — this repo is already set up for it
  User choice: "я имел в виду перенести движок shadow fight 2 1 в 1, а не shadow fight 1"
  Rationale: All repo artifacts (binaries, decompiled functions, JS reference, existing plans) target SF2
  Asked by: discusser
  Stage: discuss
  Timestamp: 2026-07-22

## Suppressed Questions

(Questions that were answered by repo evidence and not asked of the user)
- "What tech stack?" → answered by: .codebase/STACK.md (C++23, CMake, OpenGL ES 2.0)
- "What RE tools are available?" → answered by: .codebase/STACK.md (Ghidra, objdump, Unicorn, jadx, apktool)
- "What binaries are available?" → answered by: reverse/binaries/README.md (4 binaries listed)
- "What's the current engine state?" → answered by: .codebase/CONCERNS.md (~40-50%, DZ type-4 blocked)
- "Is there a Symbian port?" → answered by: PLAN_SYMBIAN.md (skeleton only)

## Open Questions

- Scope: what "FULL reverse engineering" means (complete C++ reimplementation? full binary documentation? both?)
- Primary target platform: Windows? Symbian? Modern mobile? All?
- Priority order among the 7 phases
- Whether RE tools need to be installed/configured
- Acceptance criteria / minimum viable milestone

## Next Steps

Proceed to /fd-plan to create an implementation plan. The existing PLAN_1TO1.md provides a strong starting foundation. Additional D-XX decisions can be captured during the planning process.
