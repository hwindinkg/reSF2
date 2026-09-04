# UI_EXCLUSIVITY — hint-bar vs modal + nav-button labels (web build)

Support research for Stream 1's Dojo bugfix round. Static-only, from
`reference/www/sf2.502f0946.js` (2533 lines, 1-based) + on-disk art
(`reference/www/res/ui/*.json`, `reference/www/res/fight/ui.*`,
`assets/1536/textures/buttons/*` — all read-only).

## 1. Hint-vs-modal exclusivity

Two separate surfaces: the **hint bar** (`Ib`, L1905-1912: bottom-scroller
panel with Sensei image + label + optional OK button, auto-timer `SK`)
and **modal dialogs** (`Wb` stack, L926-928: `Xc.Xhb/Bia/rIa` Regular
dialogs that halt the `Yb` quest chain until a button fires,
FLOW_STATIC §1.3).

The switch is threefold (no single step flag — behavior emerges):

1. **Close-on-modal-open**: `Wb.Xob` calls `Ib.txa()` on EVERY modal open
   (L927). `txa` (L1912): `Ib.Hb.dr && close(!1)` — closes the bar iff
   `dr` (scroller-engaged/visible state, set via `i3/NLa` L1907/L2001,
   cleared on collapse in `Lfb`, L1911). Net: a visible hint bar is
   forcibly closed the moment a modal opens.
2. **Screen gate `gYa()`** (L1912): bar may show only when NOT
   `(screen 0 Preloader / 10 / fight-6 with a real `FightNone`--excluded
   fight loaded)`. Hidden during real fights; allowed on Dojo(3)/Map(5)/
   Shop(4)/Profile(7).
3. **Button gate `Ib.RP`** (L1910; init false L2484): `He` modal sets
   `RP=qUa` (`DisableNotificationsButtons`, L1045), clears on dismiss
   (`gf`, L1062; `dhb` paths L1051/1061). While set, the hint bar renders
   **without** its OK button (`Ib.RP?b=!1`, L1910) — visibility unaffected.

Dialog-type routing (FLOW_STATIC §1.3): `Notification` type posts to the
`Ib` queue (`Qhb`, L1050/L1907) and continues the chain immediately;
`Regular/Stranger/NoAvatar/Multiline/ShowLoot` build modal objects that
block. Tutorial `Notification`s (move/punchbag/sweep/block) are
fire-and-forget; `Regular`s (training-fight buttons, boss hello,
buy-knives) are modal.

### Truth table (Dojo tutorial, per `_$StoryTutorialStep`)

| Step | Hint bar (Ib) | Modal (Wb) | Mechanism |
|---|---|---|---|
| `NotStarted` (Welcome) | YES first (move → punchbag notifications, 5s each) | YES after (training-fight Regular); bar closed by `txa` | sequential `Yb`; 1+3 |
| `FIGHT` (ReturnToFight) | no | YES (Regular training-fight) | 3 |
| `STEP_BUY_ITEM` | no (Shop scene) | YES on Shop (buy-knives Regular) | 3 |
| `MAP` (BossFight) | no (Map scene) | YES (Lynx Regular) | 3 |
| `SHOW_DOUBLE_SWEEP`@Dojo | YES only (`tutorial_try_move` Notification) | no | 2 |
| `SHOW_BLOCK` | Profile scene (same split) | — | 2 |
| `END` | Map scene | — | 2 |

Rule for the shell: **never render both for one step** — if a modal is
up, close/hide the bar (`Xob→txa`); the bar additionally requires
`gYa()` true. Native bug was showing both; fix = apply 1+2.

## 2. Baked text in buttons — per-frame verdicts

Code evidence: nav buttons are `Le` (L1978-1979: `Dojo/Map/Shop/Profile/
Settings` normal/pushed/active sprites + badge, **no text node**) and the
pause button is `db.xz` icon-only (L2034); only `Bb`-class buttons create
`ea` text labels (`V()`, L1842). So JS draws **no text** on these frames.

Pixel evidence (PIL, alpha>40):
- `assets/1536/textures/buttons/dojo/batchButtonsDojo.png` (512²): 4
  icon circles, no text bands/strips anywhere in any quadrant (fine-grid
  scan of top-left cell: icon blob only).
- `reference/www/res/fight/ui.62bee150.png` + `ui.4c9e126b.json`:
  `FightPause` 166²@(2,2) = circle + `||` bars; `Pause_pressed` = plain
  circle. No alphabetic content.
- Atlas: `menu.aaef83fb.json` holds all 8 nav frames
  (`Dojo/Map/Shop/Profile × normal/active`, +pushed); manifest id 262 =
  `ui/menu.{image}` (E.get(262) source for the `Le` buttons, L1978-1979);
  pause frames live in `fight/ui.*` (E.get(1294)).

| Frame | Verdict | Action |
|---|---|---|
| `Dojo_normal` | **baked-text: NO** (icon circle, no label art, no `draw_text`) | remove our draw_text |
| `Map_normal` | **baked-text: NO** (same sheet/family) | remove our draw_text |
| `Shop_normal` | **baked-text: NO** (same) | remove our draw_text |
| `Profile_normal` | **baked-text: NO** (same) | remove our draw_text |
| `FightPause` | **baked-text: NO** (`\|\|` icon only) | remove our draw_text |

Caveat: pushed/active variants not pixel-scanned (same family, code path
identical — no text nodes in any `Le`/`db` branch). If a future variant
shows glyphs, the atlas JSON rects above locate them.

## OPEN (needs runtime trace)

1. `Ib.dr` exact setter path (scroller `i3` event semantics, L1907/L2001).
2. `Qhb`→completion callback for `WaitNotificationClose=1` (FLOW_STATIC).
