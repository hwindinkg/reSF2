# PORT_AUDIT_UI — 1:1 UI port audit (JS `sf2.502f0946.js` vs `core/app/screens.cpp`)

Static audit only. No production code changed. Sole source of truth:
`reference/www/sf2.502f0946.js` (2533 lines, 1-based, minified — every `L###`
below is a JS line). Native side: `core/app/screens.cpp` (4666 lines),
`screens.hpp`, `screen_manager.{hpp,cpp}`. Art / font facts cross-checked
against `reference/www/res/ui/*`, `reference/www/res/fight/ui.*`.

Companion specs (DO NOT duplicate; link instead):
`UI_EXCLUSIVITY.md` (hint-bar vs modal, nav-button baked text),
`FLOW_STATIC.md` (screen flow, modes, results totals), `SHOP_STATIC.md`
(shop/economy), `FONT_METRICS.md` (BMF advances), `PAUSE_STATIC.md`
(freeze/pause), `DOJO_BG_STATIC.md` (dojo layer stack),
`JS_RENDER.md` (fight render order), `JS_GAMEPLAY.md` (gamepad).

Verdict legend:
- **FAITHFUL** — node type/frame/pos/anchor/scale/draw-order match the JS.
- **APPROXIMATED** — same intent, wrong numbers/frames/order.
- **INVENTED** — no JS counterpart; hand-made widget the original never draws.
- **MISSING** — JS surface with no native port.

---

## 0. JS symbol map + corrections to `screens.hpp`

| Symbol | L | Kind | Role |
|---|---|---|---|
| `$d` | 119 | class | screen-state base (`state/time/elements/Mr`, `jI`) |
| `mc` | 122 | class | screen manager (`K.stack`, `aa`/`Ea`) |
| `Db` | 27 | class | named scene container |
| `Ea` | 1605 | class | node/transform |
| `ea` | 1711 | class | text node (BMF) |
| `R` | 1663 | class | sprite base (`R.$`, `R.Ed`, `R.XI`) |
| `le` | 1663 | class | frame/atlas sprite (`le.frame.dL` rotation) |
| `db` | 1837 | class | button base (`xz` icon-button, `vt` frame) |
| `Bb` | — | class | text button (`EButtonWhite/Dark`, `V()`) |
| `Le` | 1848 | class | **tab/nav button** (`db` subclass, badge) |
| `Wb` | 926 | class | modal manager (`Xob`, `txa` call, `openDialog`) |
| `Xc` | 929 | class | dialog factory (`Xhb/Bia/rIa/Uhb`) |
| `od` | 1894 | class | base dialog panel (9-slice `fc`, 2340×1300) |
| `He` | 1042 | class | quest-dialog record → builds `Xc` dialogs |
| `Ib` | 1905 | class | hint bar (scroll + sensei image + OK button) |
| `Tf` | 1969 | class | **Dojo screen** (`dJ()==3`) |
| `za` | 1972 | class | **persistent top chrome**: top bar + vertical nav column + widgets |
| `xr` | 1984 | class | energy widget |
| `wr` | 1985 | class | level widget |
| `yr` | 1990 | class | money/gems widget |
| `gk` | 1996 | class | scroll frame (`orientation 0=H 1=V`) |
| `Fg` | 1868 | class | content frame (2 rail panels) |
| `Uf` | 2003 | class | progress bar |
| `fu` | 448 | class | gamepad **button cluster** (punch/kick/…) |
| `Za` | 453 | class | virtual controls (`th`=joystick, `sg`=fu) |
| `ai` | 2004 | class | **Fight screen** (`dJ()==6`) |
| `Ar` | 2016 | class | fight HUD host (`mb`=Sf, `Se`=Cr) |
| `Br` | 2010 | class | one health bar (base/fill/hit) |
| `Sf` | 2033 | class | HUD health+timer+pause (`Id`/`je`=lk) |
| `lk` | 2027 | class | one fighter HUD plate (bar/name/rounds/avatar/dmg/combo) |
| `Er` | 2021 | class | round pips |
| `Cr` | 2022 | class | fight overlay/banner (`image` id 1310, `round` id 1298) |
| `Ex` | 2015 | class | HUD timer text |
| `kk` | 2057 | class | **result dialog** (inside Fight) |
| `Fh` | 2054 | class | result data model (`lXa`) |
| `Dr` | 2065 | class | pause dialog |
| `Ya` | 2124 | class | **Map screen** (`dJ()==5`) |
| `qe` | 2140 | class | map node container (backdrop + `Qr` buttons) |
| `Qr` | 2092 | class | **map node button** (`BattleBtn*`) |
| `Rr` | 2098 | class | battle-info side panel |
| `Xr` | 2133 | class | map status list panel |
| `vb` | 2189 | class | **Profile screen** (`dJ()==7`) |
| `cs` | 2188 | class | profile tab strip (4 `Le` on id 258) |
| `Oa` | 2285 | class | **Shop screen** (`dJ()==4`) |
| `ss` | 2283 | class | shop tab strip (5–7 `Le` on id 248) |
| `Eg` | 1851 | class | bottom tab-strip host (`Le` row) |
| `Rd` | 2095 | class | act/intro screen (steps 0..7) |
| `xn` | 1167 | class | **screen-name enum only** (`iOa`/`jOa`) |

#### Corrections to `screens.hpp` header comments
1. `screens.hpp:14` claims "GeneralMenu (screen 8, `xn` L1167)". **Wrong.**
   `xn` L1167 is the static screen-name mapper, not a screen. No class has
   `dJ(){return 8}` (grep: only `0/3/4/5/6/7` at L1967/1970/2005/2125/2189/2286).
   → **GeneralMenu is a native invention.**
2. `screens.hpp:12` "the four top-tab buttons are `cs` (L2188)" — `cs` is the
   **Profile** tab strip (id 258), not a main menu.
3. `screens.cpp:1085-1087` ("no `ru.fnt` on disk") is **false**:
   `res/ui/font-ru.32eaddc0.fnt` + `font-ru.3338e715.png` ship.
4. `za` (L1972) is **not** "the GeneralMenu". It is the shared top chrome that
   `ma.D1()` (L1831) mounts on **every** `ma` shell screen (Dojo/Map/Shop/
   Profile). The "menu" screens.hpp describes is this persistent chrome.

#### `dJ()` returns (the real screen set)
`Rg`→0 (Preloader), `Tf`→3 (Dojo), `Oa`→4 (Shop), `Ya`→5 (Map), `ai`→6
(Fight), `vb`→7 (Profile). `ad` = Loader (case 3/4/5/6/7 dispatch, L1969).
No 2/8/9 screen class in this build.

---

## 1. Screen inventory (JS entry cites + native counterpart)

| Screen | JS class | JS entry lines | Native | `screens.cpp` | Verdict |
|---|---|---|---|---|---|
| Splash/Preloader | `Rg` | 1967 (`dJ()==0`) | — | boot log `app.cpp:430-438` only | **MISSING** |
| Loader | `ad` | 1969 (`load()`), art `res/splash/loading-en.*` | — | — | **MISSING** |
| Dojo hub | `Tf` | 1969-1972 (+`Yv` asset list 1970) | `DojoScreen` | 1267-1798 | **APPROXIMATED** (scene ok, chrome invented) |
| Top chrome | `za` | 1972-1984 | (folded into each screen) | 969-1069 / 1267+ | **INVENTED** (as bottom row) / layout wrong |
| GeneralMenu | — | **none** | `MainMenuScreen` | 969-1069 | **INVENTED** |
| Map | `Ya` | 2124-2132 | `MapScreen` | 1804-2178 | **APPROXIMATED** |
| Map node button | `Qr` | 2092-2095 | flat quad | 2104-2155 | **INVENTED** |
| Shop | `Oa` | 2285-2302 | `ShopScreen` | 3597-4048 | **INVENTED** (layout) |
| Shop tabs | `ss` | 2283-2284 | flat text tabs | 3597-3640 | **APPROXIMATED** |
| Profile | `vb` | 2189-2201 | `EquipmentScreen` | 4050-4384 | **INVENTED** (wrong screen's content) |
| Profile tabs | `cs` | 2188-2189 | — | — | **MISSING** |
| Fight | `ai` | 2004-2010 | `FightScreen` | 2179-3381 | **APPROXIMATED** |
| Fight HUD | `Ar`/`Sf`/`lk`/`Er` | 2016-2041 | inline | 3255-3398 | **APPROXIMATED** |
| Fight banner | `Cr` | 2022-2026 | menu-font text | 572-633 | **APPROXIMATED** (documented gap) |
| Gamepad | `Za`/`fu` | 448-459 | `GamepadLayout` | 236-317, 2458-2672 | **FAITHFUL** (geometry) |
| Results | `kk`/`Fh` (in Fight) | 2054-2061 | `ResultsScreen` (id 10) | 3382-3596 | **INVENTED** screen / **APPROXIMATED** |
| Pause | `Dr`/`Cr.pause` | 2018, 2022-2027 | inline 3-btn menu | 2790-2870, 3300-3360 | **INVENTED** (see PAUSE_STATIC.md) |
| Quest/Sensei dialog | `He`→`Xc`→`od` | 1042-1063, 1894-1900 | `draw_quest_modal` | 136-157 | **INVENTED** (flat panel) |
| Hint bar | `Ib` | 1905-1912 | flat panel 780×64 | 1681-1752 | **INVENTED** |
| Act/boss intro | `Rd` | 2095-2098 | `ActPlayer` overlay | 2160-2176 | **APPROXIMATED** |
| Settings | (dialog/options) | `Xc`/`Qg` | `SettingsScreen` (id 11) | 4385-4455 | **INVENTED** screen |
| Moves | Profile tab (`qv`,`To.kOa`=11) | 2190-2193 | `MovesScreen` (id 12) | 4456-4540 | **INVENTED** screen |
| Bracket | `Xr` (map panel) | 2133-2136 | `BracketScreen` (id 13) | 4541-4630 | **INVENTED** screen |

---

## 2. Per-screen node spec (wire-spec for a faithful rewrite)

### 2.1 Persistent top chrome `za` (L1972-1984) — THE dominant visual gap
Every shell screen mounts `za` via `ma.D1()` (L1831: `this.kA=this.Qo(za)`).

Constructor (L1972-1974):
- `PL = R.$(E.get(260), y.QRa, node)` — topPanel backing (misc atlas id 260).
  `PL.Rc(!1)` (raycast off).
- `Pr` = widget strip; children in order left→right:
  `ap = wr` (level, L1985), `Ov = xr` (energy, L1984), `Tb = yr` (money, L1990).
- `LW = R.Ed(16711935,1,1,node)` magenta debug rect, `LW.R(!1)`.
- optional `Dj = db.xz(null,y.Wna)` skip-tutorial (only `L.K.fi && L.K.eU`),
  scaled `.5`, `Kd("skipTutorial")`.

Top bar layout `odb()` (L1975-1976):
- `PL.xc(rect.w); PL.Pb(min(rect.h*0.13, mobile?230:100))` → **full-width top bar**
  height `min(H*0.13,100)` desktop.
- `Sp = PL.qa()*0.78` (bar visual height).
- widget height `b = Sp*0.65`; widgets laid at `x=0`, then `+d`, gap `c=50*lc`.
- `Pr.C((rect.w - Pr.node.za())/2); Pr.D((Sp - b*Pr.node.Eb)/2)` → **centered row**.

Nav column layout `ndb()` (L1976-1977):
- `scroll.node.C(100*(0.2 + ((lc<.5?.5:lc>2?2:lc)-.5)/1.5*0.8))`;
  `scroll.node.D(Sp)` → **just below the top bar, at left x≈100*scale**.
- `b = buttons[0].Y.fa.y*0.85` (row height); column height `c = b*N+45`.
- `d = max(.1, min(W,H)*0.35/430)`; `e=430*d`, `f=c*d` (clamp to
  `rect.v - Sp - 100`); `qka = 50*d`.
- `wc.ba(e,f,qka)` (content frame `Fg(400,800,0,50)`), `scroll.ba(e,f,90*d)`.
- per button `g`: `g.la(d); g.C(wc.Gv/2); g.D(a + f*b*d)` → **VERTICAL stack**,
  x centered in the column, y increasing down. `a=b/2*d` inset.

Buttons `Aub()` (L1977-1980) — five `Le(E.get(262), normal, active, pushed)`
(menu atlas id 262), all `EL=!0`, added to `wc.content` and `this.buttons`:
| # | field | normal/active/pushed | nav (`d1`) | listener |
|---|---|---|---|---|
| 1 Dojo | `xba` | `y.dRa/eRa/cRa` | 3 | `Ofb` |
| 2 Map | `$va` | `y.gRa/hRa/fRa` | 5 | `Qfb` |
| 3 Shop | `zba` | `y.oRa/pRa/nRa` | 4 | `Wfb` |
| 4 Profile | `$Y` | `y.jRa/kRa/iRa` | 7 | `Rfb` |
| 5 Settings | `vYa` | `y.mRa/lRa/(null)` | — (spinner `Bi` + `G.load([250..253])`) | `Vfb` |

Each `Le` (L1848) = sprite + `badge = Dg` (L1850: icon `y.V6/LRa`, label
count). `xyb(screen)` (L1982) highlights the matching button (`AT` → `Nf=!1`
+ `tL()`); `Nf=!1` = pushed frame.
Disciple toggle `zq = db.xz(E.get(262), y.Tna:Sna)` (L1983), scaled `.75`,
placed in `layout()` at `D(a/2 + PL.qa() + scroll.Af.height)`.

**Native**: `MainMenuScreen::render_impl` (1033-1069) and
`DojoScreen::render_impl` (1754-1773) draw a **horizontal** 4-up row
(`xs = 0.28/0.46/0.64/0.82 * W`, `y = 0.72*H`, 220×120) of
`Dojo_normal/Map_normal/Shop_normal/Profile_normal`. Positions, axis (H vs V),
button count (4 vs 5), sizes, badges, active/pushed states, and the level/
energy/money widgets are all **INVENTED/APPROXIMATED**. `draw_dojo_hud_bar`
(1496-1528) hard-codes topPanel at `(640,42,1280,84)`, level icon `(110,42)`,
level bar `(210,42)`, energy `(330,42)`, money `(740,42)`, ruby `(930,42)`,
AddMoney `(1100,42)` — none computed by the `odb` formula.

### 2.2 Dojo `Tf` (L1969-1972)
- `Yv()` asset preload (L1970): `753,754,755`; then
  `BJ ? {314,1354,196,1317} : {315,1355,10,11,1317,1316,Oea(),248,249,
  338,339,829,830,266,267}`; then `262,263 / 268,269 / 1306,1307 / 260,261 /
  244,245 / 264,265 / 254,255 / 12,13`.
- `init` (L1971): builds `this.Ig = v.m1a(FightNone viewer)` — the dojo hub is a
  **ModelViewer** on the `FightNone` battle (the idle stance figure), not a bag.
- `aa()` (L1972): `Za.F().update()` + `this.YL(this.Ig,a)` + secret-code
  detector (`Kwa`/`bwa`).
- `Ea()`: `super.Ea(a); Sya(this.Ig); Za.F().Ea()` — no bespoke art beyond the
  shared `za` + `Za`.
**Native**: `DojoScreen` renders the `locations/dojo` layer stack through
`assets.dojo.render_layers` (1556-1572) + hub camera `default_camera`
(1566-1570). The layer stack is right; the **idle figure** (1591-1630) is a
hand-built capsule strip placed at `kFigX=400`, `kFeetY=650` (invented); the
**hanging punchbag** (1631-1675) does not exist on the JS hub at all (**invented**);
the 4 buttons + gear + disciple chrome are invented.

### 2.3 Map `Ya` (L2124-2132)
- `dJ()==5`. `mmb()` (L2130): `ue = Qo(qk)` map content (`Vr` battle list +
  `Ur` panel). `Sya` camera (1833).
- Node container `qe` (L2140-2145): `background = R.$(E.get([336,334,332,330,
  328,326,324][a]), "map"+a, node)` where `a = parseInt(fileName.split(".")[1])-1`
  (`W0a` L2143) — **backdrop index is the zone file number, not file order**.
- Node button `X0a` (L2144): `d = Sxa(c)`; `d.la(y5a()=150/225*qe.uM)`;
  `d.C(c.position.x*qe.uM + background.fa.x/2)`;
  `d.D(-c.position.y*qe.uM + background.fa.y/2)`;
  `d.D(d.node.ra-50)`.
- `Qr` (L2092-2095): frames
  `BattleBtn{Base|Active|Lock|LockActive|Pressed}/<suffix>` where suffix is
  `a.eR + state` (`uea`) or `a.{U9a/V9a/...}()` (`vea`), i.e. **per-node art**;
  label `cC = ea` at `(-50,65)` size 60 black.
- `Rr` info panel (L2098-2101): `wc = Fg(430,800,0,50)` right-docked; title
  `oq = ea`; fight button `Bb("EButtonWhite")`.
- `Xr` status panel (L2133-2136): `label = ea` (Z.sc), `Ox` rows with
  `y.Yna/FRa/ERa` lock/base/cleared frames.

**Native** (1804-2178): backdrop `map<N>` where `N = part` assigned by
**file order** (`load_zone_map:759-762`) — JS uses the fileName number.
Node pos `x = X*1.0 + view_w/2`, `y = view_h/2 - Y*1.0` (729-730) — JS uses
`background.fa` dims + `uM` + `-50`. Node art hard-coded
`BattleBtnActive/active_lynx` (`2108`) for **every** node (JS per-node). Zone
tab strip `(130+i*140, 38)` (1913/2040-2061) and BRACKET corner button
(2141-2155) — **no JS equivalent** (zone nav is `Vr` scroller + `Rr` panel).

### 2.4 Shop `Oa` (L2285-2302) + tabs `ss`/`Eg`
- `dJ()==4`. `f5(a)` (L2286-2288) switches category, sets `Za.uw` anchors
  `(300,220|400|280|320)`, build lists `xaa/u7/U8/s$/G9/gC/QV`.
- `layout()` (L2293-2295): a responsive `gb` rect split (left/right/top/bottom),
  `Oa.gg` compact branch; 3D card viewer `Za = Oe`, side panels `hi`
  (`bc`/`MJ`/`op`), detail `Pi`.
- `ss` tab strip (L2283-2284): `Eg` bottom host; `Tw=[0,1,2,3,4]` (+5 iap,
  +7 if `L.K.Yja`); buttons `a(n, y.KSa, y.MSa, y.LSa)` … = `Le(E.get(248))`,
  badge at `(node.w-13, node.h-12)` size 65.
- Item cell `ns` (`ff` subclass, L2303-2308): icon `R.$(E.get(260), y.PRa)` +
  item image `Rf(Ye.qI(fileName))` from `res/items/images-1x/…`, name `av`,
  price, lock/sale tags.

**Native** (3597-4048): fixed card grid `X0=0.25W`, `Y0=200`, `Dx=330`,
`Dy=170`, `CardW=300`, `CardH=150` (3610-3619) — invented. Tabs flat at
`X0=290, Y=100, W=140` (3620-3623). Card art `attributes/*` (3623-3631) — JS
uses the item image, not attribute icons.

### 2.5 Profile `vb` (L2189-2201) + tabs `cs`
- `dJ()==7`. Tab strip `cs` (L2188): 4 `Le(E.get(258))` `Tw=[0,1,2,3]` id 258
  (profile atlas), listeners `hla`.
- Sub-views (L2193): `Rl=ds` (skills), `qv=es` (stats), `Zr=fs`, `lv=gs`;
  `Zu=Qx`, header `XB=ei`. Layout (L2195-2196) same `gb` split pattern.
- `To.kOa` (L2201) maps the profile sub-screen to `za` screen names
  (`10/11/12/13/15`) — Moves is a **Profile tab**, not a screen.

**Native**: `EquipmentScreen` (4050-4384) shows 5 slots + owned grid + header
EXP bar — none of which is the JS Profile's tabbed content. Profile tabs
**MISSING**; Moves as a separate screen **INVENTED**.

### 2.6 Fight HUD `Ar`/`Sf`/`lk`/`Er` (L2016-2041)
- `Ar.init` (L2017): `mb = appendChild(Sf)`; `Se = appendChild(Cr)`.
- `Sf` (L2033-2038):
  `Jn = db.xz(E.get(1294), y.IQa)` pause button, `Xc.la(1.75)`.
  `Id = lk(0)` player, `je = lk(1)` enemy.
  `OA/Kp = ea(E.Na())` shadow + timer text, size 128, `Kp` color 16767392.
  `layout()`: `d = clamp((W-J)/(W-P), .4, 1.5)`; `c = min(W,H)/2/675*g`;
  bar centers `(J+N)/2 ∓ 520*c*e` (`e` clamp .1..1.1); bar Y
  `P + 150*c + f*g`; `Kp.D(Id.node.ra-120*c)`; `Jn.node.C(b/2)`; `Jn.node.D(…*25)`;
  `RH` = pause hit rect.
  `iPa()`: `--xU; NF = xU/60|0`; `reset()`: `xU = round.gma*60+1`.
- `lk` (L2027-2033): `al = Br` bar `uL(330)`, `Sh = Fr` name frame `uL(165)`,
  `S4 = Er` pips, `Hf = oe` avatar, `Jh = Gr` damage stack, `SH = Hr` combo.
  `bMa()`: `al.node.C(type?130:-460)`, `Sh.node.C(type?130:-295)`.
- `Br` (L2010-2015): bar frames `E.get(1294)` (`y.UU` base, `y.JQa` hit,
  `y.Qna` full), `BL(25*side)` mirror, width `uL(425)`, Y `krb()=43`.
- `Er` (L2021-2022): `e = (type==2?40:32)`, `f=e/2`, spacing `e+f`, `Pb(43)`,
  `BL(25*side)`, frames `y.UU`/`y.LQa`.

**Native** (3255-3398): `bar_w=440, bar_h=25, bar_y=115.7`, centers
`±520*0.5867*1.1` — hard-coded 0.5867 ≈ the `c` formula at 1280×720 but not
computed; JS width 425 / height 43, native 440 / 25. Timer native `digits.fnt`
at `y=44`, scale `.75`; JS `ea` size `120*c`. Round pips native 18×18 at
`px=78+i*22`; JS `e=32`, spacing `1.5e`, y `43`. All **APPROXIMATED**.

### 2.7 Fight banner `Cr` (L2022-2026)
- `Qa = R.$(E.q1a(),…)` dim; `image = R.$(E.get(1310),…)` banner atlas;
  `round = ea(E.get(1298),…)` round-number font (id 1298), size 64,
  `ua(fontSize*1.6)`.
- `init(frame,b)`: show `image` frame + optional round; `layout()`:
  `content.setPosition(ma.Kq.F5a())`, scale `min(800,min(W,H))/image.w*0.6`.
- Frames: `y.BQa` round, `y.uQa` FIGHT, `y.zQa/wQa` victory/defeat,
  `y.DQa/AQa/Kna/Lna` etc. (L2023-2024).
**Native** (572-633): menu-font text, no atlas frames — **APPROXIMATED**
(documented gap).

### 2.8 Gamepad `Za`/`fu` (L448-459) — reference implementation for FAITHFUL
- `fu` offsets: `fh`(kick) `(-50,198)`; `Si`(punch) `(120,28)`;
  `di` `(-120,-28)`; `Eg` `(50,-198)`; hit `x²+y²<13225` (=115²) (L452-453).
- `Za.update` (L454-456): `c=H*.05` (`H*.04` if `lc<1`), `d=H*.03`
  (`H*.1`), `e = mobile?H*.4 : max(150,H*.2)`;
  joystick scale `e/sNa`, center `(f+c+BK, rect.y+rect.v-f-d)`;
  buttons node scale `e/440`, center `(rect.x+rect.w-440*e/2-(c+BK),
  rect.y+rect.v-596*e/2-d)`.
**Native** `GamepadLayout` (294-317) mirrors these exactly; button offsets
(311-314) match `Si(120,28)`/`fh(-50,198)`; hit `kPadBtnHitR=115` (271).
**FAITHFUL** (draw-size 0.9 is a native nudge).

### 2.9 Dialogs `od`/`He` + hint `Ib`
- `od` (L1894-1900): `AV=new fc(2340,1300)` base; 9-slice `XN[0..2] =
  R.$(E.get(254), y.eoa/lSa/eoa)` (`Hr(!0)` on the third); title
  `Vc = ea(E.Na())` `Fa(1560,160)`, `ua(152)`, color `Z.W6`; body `Cd`;
  up to 2–3 buttons `Bb`/`od.gi`. `layout()` (L1898): `Ne.D(-Md/2)`,
  `Vc.D(-(a+Vc.pfa().y))`.
- `He.S` (L1045-1051): builds dialog by `type` via `Xc.Xhb` (Regular),
  `Bia` (Stranger), `rIa` (NoAvatar), `Uhb` (ShowLoot); `Notification` posts
  to `Ib.F().Qhb` (L1050); `Ib.RP` set from `DisableNotificationsButtons`
  (L1045). **UI_EXCLUSIVITY.md**.
- `Ib` (L1905-1912): `scroll = gk(600,250,50,0)` **horizontal**;
  content `Fg(600,250,1,30)`; `image = R.$(E.get(12), b)` sensei;
  `label = ea` `Fa(600-image.w+20,150)`; OK `button = Bb(Zva)` at `(450,185)`;
  `layout()`: node scale `min(W*.75,H*.75)/scroll.width` clamp [.2,1.1],
  `node.C(W - scroll.width*scale)`, `node.D(H*.1)`.
**Native**: `draw_quest_modal` (136-157) — flat 900×220 quad + title + 3 lines
+ "TAP TO CONTINUE" (**INVENTED**). Dojo hint (1681-1752) — flat 780×64 quad,
sensei portrait drawn from a `sensei_portrait` texture + **procedural 28-seg
green ring** (1728-1744, ring draw site OPEN in JS); JS uses atlas id 12 +
`Z.W6` border, no ring. **INVENTED**.

### 2.10 Act `Rd` (L2095-2098)
- `hf = Fc.Ed(-16777216, mc.K.cf)` black fader; `label = ea` centered,
  `Fa(W/scale*(.9+(lc-.4)/1.6*-.5), 400)`, `ua(130)`, `Kc(.7)`.
- Steps 0..7 (fade in, label, timed lines `lines[i].value/60`, hold, fade out,
  callback, end).
**Native** (2160-2176): flat dim + "BOSS" + line + "TAP TO SKIP" — approximate.

---

## 3. Per-screen C++ verdict table

| # | Surface | Native cite | Verdict | Key evidence |
|---|---|---|---|---|
| 1 | Dojo backdrop layers | `screens.cpp:1556-1572` | **FAITHFUL** | `assets.dojo.render_layers` + `default_camera` (JS `Tf.Ig` viewer) |
| 2 | Dojo idle figure | `screens.cpp:1591-1630` | **APPROXIMATED** | invented `kFigX=400/kFeetY=650`; JS viewer transform not derived |
| 3 | Dojo punchbag | `screens.cpp:1631-1675` | **INVENTED** | JS hub = `FightNone` viewer; bag is a quest Training dummy |
| 4 | Dojo 4-up buttons | `screens.cpp:1267-1300,1754-1773` | **INVENTED** | JS `za` nav = vertical column, 5 `Le` + badges |
| 5 | Dojo top HUD bar | `screens.cpp:1496-1528` | **APPROXIMATED** | invented coords vs `za.odb` (1975-1976) |
| 6 | Dojo gamepad | `screens.cpp:1533-1549` | **FAITHFUL** | `Za` update L454-456 |
| 7 | Dojo hint panel | `screens.cpp:1681-1752` | **INVENTED** | flat quad + procedural ring vs `Ib` L1905-1912 |
| 8 | Dojo settings gear / disciple | `screens.cpp:1777-1795` | **INVENTED** | JS Settings=`za` button #5; disciple=`za.zq` L1983 |
| 9 | MainMenu | `screens.cpp:969-1069` | **INVENTED** | no JS screen 8; `dJ()` never returns 8 |
| 10 | Map backdrop | `screens.cpp:697-767,1997-2008` | **APPROXIMATED** | file-order part vs `a=fileName#-1` (L2143-2144) |
| 11 | Map node position | `screens.cpp:729-730` | **APPROXIMATED** | view/2 + uM≈1 vs `bg.fa/2 + pos*uM` + `-50` (L2144) |
| 12 | Map node art | `screens.cpp:2104-2128` | **INVENTED** | hard-coded `base_lynx`/`active_lynx` for all |
| 13 | Map zone tabs | `screens.cpp:1913,2040-2061` | **INVENTED** | no JS tab strip; JS uses `Vr`+`Rr` |
| 14 | Map bracket button | `screens.cpp:2141-2155` | **INVENTED** | no JS screen 13; `Xr` is a panel |
| 15 | Map boss act | `screens.cpp:2160-2176` | **APPROXIMATED** | flat text vs `Rd` L2095-2098 |
| 16 | Shop layout | `screens.cpp:3610-3623,3915-4048` | **INVENTED** | fixed grid vs `Oa.layout` L2293-2295 |
| 17 | Shop tabs | `screens.cpp:3597-3640` | **APPROXIMATED** | frame names guessed; JS `ss`/`Eg` L2283 |
| 18 | Shop card art | `screens.cpp:3623-3631` | **INVENTED** | `attributes/*` vs item image `Rf` L2307 |
| 19 | Profile/Equipment | `screens.cpp:4050-4384` | **INVENTED** | slots+grid vs `vb` tabbed Profile L2189-2201 |
| 20 | Profile tabs `cs` | — | **MISSING** | L2188 |
| 21 | Fight HUD bars | `screens.cpp:3260-3345` | **APPROXIMATED** | 440×25@115.7 hard-coded; JS 425×43, formula L2036 |
| 22 | Fight timer | `screens.cpp:3347-3374` | **APPROXIMATED** | `digits.fnt`@y44 vs `ea` size 120*c |
| 23 | Fight round pips | `screens.cpp:3376-3398` | **APPROXIMATED** | 18px@x78 vs `Er` e=32 L2021 |
| 24 | Fight banner | `screens.cpp:572-633` | **APPROXIMATED** | text vs atlas id 1310 L2022 |
| 25 | Fight pause | `screens.cpp:2790-2870,3300-3360` | **INVENTED** | flat 3-btn menu vs `Dr` dialog L2018 |
| 26 | Fight Next button | `screens.cpp:3170-3187` | **INVENTED** | uses `FightPause` frame; JS has no Next button |
| 27 | Fight gamepad | `screens.cpp:236-317,2458-2672` | **FAITHFUL** | L448-459 |
| 28 | Results | `screens.cpp:3382-3596` | **INVENTED** screen / **APPROXIMATED** layout | JS `kk` in-Fight L2057-2061 |
| 29 | Quest/Sensei modal | `screens.cpp:136-157` | **INVENTED** | flat panel vs `od` 9-slice L1894-1900 |
| 30 | Settings screen | `screens.cpp:4385-4455` | **INVENTED** | no `dJ()==11`; JS dialogs/options |
| 31 | Moves screen | `screens.cpp:4456-4540` | **INVENTED** | JS Profile tab (`To.kOa`=11) L2201 |
| 32 | Bracket screen | `screens.cpp:4541-4630` | **INVENTED** | no `dJ()==13` |
| 33 | Splash/Preloader | — | **MISSING** | `Rg` L1967; `app.cpp:430-438` skips |
| 34 | Loader | — | **MISSING** | `ad` L1969; `res/splash/loading-en.*` unused |
| 35 | `draw_flat_button` | `screens.cpp:442-458` | **INVENTED** | used as the universal fallback for missing atlas art |

### 3.1 Why art is "flat" — decode path
The comment at `screens.cpp:22-26` / `1108-1110` blames ASTC/CRUNCH atlases
(`menu.*`, `map/part*`, `shop.*`, `profile.*` are `.ktx`/`.dds`). The
**misc** (`misc.1fba039c.ktx/.dds/.json`), **menu** (`menu.b4e55ab9.ktx`),
**controller** (`controller.6eb77c83.ktx`) and **fight/ui** atlases are the
ones the `try_draw_atlas_button` path needs. Whether the pipeline can decode
them is the pivot's first gate — see the loader in `core/data` (out of scope
here). The wire-spec above is valid regardless of decode status.

---

## 4. Ranked rewrite list (visual impact)

### HUGE
1. **Shared `za` top chrome** — replace BOTH the MainMenu 4-up row and the
   Dojo 4-up row with one `za` implementation: full-width topPanel (id 260)
   at `min(H*.13,100)`, 3 widgets (`wr`/`xr`/`yr`) centered, and a **vertical**
   nav column of five `Le` (id 262) at `x≈100*scale`, `y` below the bar,
   row height `0.85*frame.h`, spacing `50*scale`, with badges + active/pushed.
   Mount on Dojo/Map/Shop/Profile (JS `ma.D1`). Delete `MainMenuScreen`.
2. **Dojo hub** — render `FightNone` viewer framing (not a hand-placed capsule
   at 400/650); delete the hanging punchbag, the FIGHT button, the gear, and
   the disciple chrome; keep the location layer stack.
3. **Fight HUD** — compute `c=min(W,H)/2/675*g`, centers `∓520*c*e`,
   bar Y `P+150*c+f*g`; bar width 425/h 43 frames (`y.UU/JQa/Qna`); pips
   `e=32`; timer `ea` size `120*c`; banner from atlas id 1310 + round font id
   1298 (`Cr` L2022-2026).
4. **Map nodes** — per-node `BattleBtn{Base|Active|Lock|LockActive|Pressed}/
   <node art>`; positions `pos*uM + bg.fa/2`, `uM`, `-50` offset; backdrop by
   `fileName#`. Remove the tab strip and BRACKET corner.

### MEDIUM
5. **Shop** — `Oa.layout` responsive split + `ns` cells with item images
   (`res/items/images-1x/...`) and an `Eg` bottom tab strip (`Le` id 248).
6. **Profile** — implement `vb` tabbed screen (`cs` tabs id 258: skills/
   stats/…); fold Moves into Profile tab; move equip into the shop detail
   panel (`$o`).
7. **Dialogs + hint** — `od` 9-slice (id 254) with `Vc` title (156×160),
   `He` type routing; `Ib` horizontal scroll (id 12 sensei image, OK at
   450/185) with `gYa()`/`RP` gates (UI_EXCLUSIVITY.md).
8. **Results** — render as `kk` dialog (750-base, id 1310 win/lose) inside
   Fight; drop the standalone screen or make it a thin driver.

### SMALL
9. **Settings** — `za` button #5 spinner/options; not a screen (or keep a
   minimal overlay).
10. **Pause** — `Dr` dialog via `Ar.Aia` per PAUSE_STATIC.md (replace the
    flat RESUME/RESTART/QUIT).
11. **Act/intro** — `Rd` step machine (label `ua(130)`, timed lines).
12. **Splash/Loader** — port `Rg`/`ad` with `res/splash/loading-en.png/fnt`
    (currently skipped).
13. **Font/lang** — RU BMF **does** exist (`res/ui/font-ru.*`); revisit the
    EN-only decision in `ensure_lang` (`screens.cpp:1074-1104`).

---

## 5. OPEN (needs runtime trace / further static work)

1. `qe.uM` numeric value and `background.fa` per zone (map node scale/pos).
2. `y.*` frame-name id → string table (normal/active/pushed for `Le`
   `dRa/eRa/...`, `Qr` `V9a/U9a/...`) — resolves exact atlas frame names.
3. `za.odb/ndb` at 1280×720 (`lc`, `rect`, `Sp`, `qka`, button `d`) — the
   literal pixel layout of the top bar + nav column.
4. `Oe` (shop 3D card viewer) transform/`gb` math — needed for exact shop.
5. `Ar.Aia`/`Dr` pause dialog exact node tree (PAUSE_STATIC OPEN list).
6. Whether `crunch`/ASTC decode is available (the flat-fallback gate).
7. Screen 8/9 (GeneralMenu/Pvp) — no class in this build; confirm they are
   server/native-shell only (so GeneralMenu should be deleted, not ported).
