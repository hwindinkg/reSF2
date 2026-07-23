# Phase 2 — Binary-Level RE Verification

## Batch 1 (Target A only — FUN_101661d0)

### Task 1: Fix test_dz_first_byte CWD bug
- **Edit:** `tests/CMakeLists.txt` line 178 — add `WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"` to `add_test()`
- **Build:** `cmake --build build --target test_dz_first_byte --parallel`
- **Run:** `cd build && ctest -R test_dz_first_byte --output-on-failure`
- **Verify:** Confirm whether test passes (CWD fix) or still fails (genuine decoder bug)
- **Classification gate:** If it passes → NOT a decoder issue. If it fails → decoder bug but still out of RE scope.

### Task 2: Reverse FUN_101661d0 (Vec3→world formula)
- Use Ghidra to get decompiled output for address `0x101661d0` (FUN_101661d0 — ModelAnimation::playInfo)
- Decompile and analyze the function (already done preliminarily)
- Produce candidate C++ code for the Vec3→world consumption formula
- Call @re-verifier with the address + candidate code
- Iterate up to 3 rounds if verdict is not GREEN

### Success Criteria
- [ ] test_dz_first_byte passes after CWD fix (or decoder bug confirmed)
- [ ] @re-verifier returns GREEN on FUN_101661d0 candidate
- [ ] Full round-by-round report provided

### Plan Tasks
1. Fix test_dz_first_byte CMake → rebuild → re-run
2. Decompile FUN_101661d0 (Ghidra) 
3. Produce candidate C++ for Vec3→world formula
4. @re-verifier call (runs checklist + cpp_call_branch_count)
5. Iterate if needed (max 3 rounds)
6. Report full history
