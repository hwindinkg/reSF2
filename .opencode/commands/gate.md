---
description: Run the full reSF2 gate set (harness/gates.py --full) and print the table
---
Run the `gate` tool with `mode: "full"` (it runs `python harness/gates.py --full`).

Then:
1. Print the gate table verbatim (gate | baseline | now | verdict).
2. For every row whose verdict is not OK/IMPROVEMENT, print its `baseline -> now`,
   the verdict, and the first divergent field(s) the report shows.
3. State the exit code and the total duration.
4. Do not change any code in this turn. If a gate regressed, report it and stop.
