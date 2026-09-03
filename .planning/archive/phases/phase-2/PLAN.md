> ⚠️ УСТАРЕЛО. Этот документ описывает реверс нативного/Unity билда SF2,
> НЕ веб-версии. Не использовать как источник истины для порта.
> Валидный оракул: reference/www/sf2.502f0946.js + shell/OracleShell.

# Phase 3 — Comprehensive RE Comparison & Fixes

## Objective
Find all functions in binaries (s86, JS, IL2CPP) responsible for:
1. Controls and character movement
2. Dialogs and zones  
3. Shop
4. Fight/combat system

Compare with current C++ code, identify discrepancies, fix them.

## Step 1: Research — Extract JS game logic
- Read sf2_pc/www/sf2_beautified.js for key systems
- Map function names: input handling, movement, dialog, zones, shop, combat
- Document algorithms and magic numbers

## Step 2: Research — Review C++ implementation
- Read game.cpp for all 4 problem areas
- Document current state, what works, what's wrong

## Step 3: Research — Locate in s86 (Ghidra)  
- Find matching functions in Shadow Fight 2.s86 (30630 functions)
- Cross-reference with IL2CPP dump.cs

## Step 4: Gap Analysis & Plan
- Compare all three implementations
- List specific discrepancies
- Create fix plan with priorities

## Step 5-8: Fix implementations
- Fix controls/movement
- Fix dialogs/zones rendering
- Implement real shop
- Improve fight system

## Step 9: Enhance F1 debug overlay

## Success Criteria
- All 4 problem areas mapped to original functions
- Current C++ compared against JS and s86
- Gap list with addresses and line numbers
- Debug overlay enhanced