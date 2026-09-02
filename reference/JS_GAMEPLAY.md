# JS_GAMEPLAY — Shadow Fight 2 Web: Боевой лайфсайкл (спецификация)

**Источник:** `reference/www/sf2.502f0946.js` (2531 строка, Haxe→JS, все номера строк 1-based).
**Дата:** 2026-09-03. Всё выведено из самого JS (JS_MAP.md использован как карта, фактические цитаты сверены с файлом).
**Сравнение с нативом:** `core/scene/*.cpp` (наш порт). Статусы: «совпадает» / «частично» / «нет» / «не проверено».

Соглашение об именах: классы названы фактическими JS-именами (например `ca`, `wd`, `Te`, `Za`, `Sf`); JS_MAP.md даёт их Haxe-аналоги (`ca`=fight controller, `wd`=fighter, `Te`=animation controller, `ma`=fight screen base, `ai`=fight screen, `Ut`=camera controller, `ql`=camera wrapper, `de`=weapon/AI controller, `Ar`=HUD, `Sf`=HUD layout).

---

## 1. Фазы боя и переходы

### Что происходит (шаг за шагом)

**Создание боя.** `ai.init()` (L2005) создаёт HUD (`this.ha=this.Qo(Ar)`); state-machine `ai.aa(a)` (L2006-2008) по `kd`:
- `kd==0`: `ca.hCa(...)` (L431-432) формирует setup (`TF`); если боёв несколько (`lD.length>1 && eE`) — заставка в стиле боссов (`lca`), иначе сразу `tx()` (первый раунд).
- `kd==1`: `this.Ig=v.Yxa(this.Da,this.DA.G,this.pf.G,this.ha)` (L1209) — **создаётся `ca` (боевой контроллер)**; `Ig.Ta.ia.visible(!1)` — камера спрятана.
- `kd==3`: `Ig.tx()` (round init), первый кадр рендера через `YL`.
- `kd==5` (основной цикл): `Za.F().update(); this.YL(this.Ig,a)` — каждый кадр обновляется ввод и бой.
- `Ea(a)` (L2008): `this.Sya(this.Ig)` — камерный рендер, `Za.F().Ea()` — рендер виртуального геймпада.

**Фазы** — поле `ca.eu` (L380: `this.eu=0`), переключаются `xF(a)` (L388):

```js
xF(a){a==2?Za.F().nla(!0):a==3&&Za.F().nla(!1);this.eu=a;let b=0,c=this.Ra;
for(;b<c.length;){let d=c[b];++b;d.Vb.data=a;d.Je=a;this.tb.Gj(d,1);...}}
```

Фазы: `eu==0` idle, `eu==1` StartStance, `eu==2` Fight, `eu==3` EndStance. `xF` синхронизирует `fighter.Je` (его читают условия мувов — `Em`-условие «Stance», L755) и диспатчит событие 1 (при `eu==2` включается виртуальный геймпад `Za.nla(!0)`).

| Переход | Кто вызывает | Строка | Суть |
|---|---|---|---|
| start → StartStance | `ca.ggb()` → `swb()` → `FNa()` | L383, L407, L409 | `FNa(){this.Ta.XF(!0);this.xF(1)}` — камера включена, фаза 1 |
| StartStance → Fight | `ca.kg()` (EStopAnimationEvent) | L387 | `this.eu==1&&a&&(this.fxa(),this.Da.type!="FightNone"?this.Am():this.xF(2))` — стартовая анимация закончилась (`a.model.OCa()` — «атака-интервальная»), в реальном бою показать баннер FIGHT (`Am()`→`ha.Zy()`), в FightNone сразу фаза 2 |
| баннер → Fight (нажатие кнопки) | `ca.vhb()` | L410 | `switch(this.ha.lp()){case 1:this.Z2();break;case 2:case 3:this.FNa();break;case 5:this.Rkb()}` — HUD-баннер «FIGHT» (type 5) → `Rkb()` |
| Fight → EndStance | `ca.Onb()` → `E3a()` → `i4a()` | L411-413 | конец раунда (см. ниже) |
| EndStance → следующий раунд | `ca.kg()` → `h4a()` + HUD-кнопка `vhb` | L387, L413, L410 | `kg`: `this.eu==3&&a&&(this.fxa(),this.h4a())` — `h4a(){this.JJ=!0}`; затем `vhb` case 1 → `Z2()` (следующий раунд), case 2/3 → `FNa()` (рестарт) |

**`Rkb()` (L410) — начало боевой фазы:**
```js
Rkb(){this.xF(2);this.Hba=!0;let a=this.ha;a!=null&&a.Fzb();this.Eaa(!0);this.ud.T4=!0;
this.llb();this.Bra&&(this.o9=!this.fA(!1,!1),this.Bra=!1);this.yb.b5(14,this.kc.eP);this.pb.b5(14,this.Zb.eP)}
```
`ha.Fzb()` → `Sf.play()` (L2038): `this.DJ=this.round.Vt=!0` — **таймер раунда запущен**. `llb()` (L429) — **исполнение буфера нажатий фазы 1**: `c.WC!=-1&&(c.yJa(c.WC),c.WC=-1)` — нажатия, сделанные игроком в StartStance, применяются сразу при переходе в Fight (input buffering раунда).

**Раунд-машина.** `$t` (L1239): `{Vt(run), Mcb(raid), time, gma, round, eL}`.
- `tx()` (L407): `round.Vt=!1; round.eL=Da.pT; round.time=0; round.gma=Da.R4; round.Mcb=type=="FightRaid"` — **eL = число раундов (Da.pT), gma = длина раунда в секундах (Da.R4)**.
- `Z2()` (L408-409) — старт нового раунда: снапшоты HP/прогресса в `Pm/vo` (rl), `round.round++`, `IKa()` (пре-раунд), затем `MHa()` (L409): `c.Z2(this.round.round); c.parameters.nob()` на каждого бойца. `wd.Z2(a)` (L522): `this.round=a; this.y5(!0); this.Mka(-1); this.Mtb(); this.Cn.$K()` — разблокировка действий, сброс стойки/модов.
- Счётчик кадров раунда: `ca.Cza` — `Hnb()` (L388): `this.round.Vt&&this.Cza++`; в конце боя уходит в статистику `v.kD(...,this.Cza/60)` (L427).

**Конец раунда — `Onb()` (L411-412):**
```js
Onb(){if(this.h9){let a=this.wo.nB.ng>=this.round.eL;   // лидер набрал eL побед → конец боя
if(!this.cY&&!this.TG){this.h9=!1;let b=this.wo.nB.qb&&this.Rk<this.pf.length-1,c=this.Da.sR();
!a&&c||a&&b ? (…this.mfb(...))              // следующий бой (multi-fight)
: a?this.bea(this.wo.nB)                    // ★ конец боя
: (this.Ta.XF(!1),this.ZK(),this.NA(),this.Z2());}   // следующий раунд
}else this.xJ?...:this.ha.PEa()&&(…this.E3a(this.vfa(!0),this.vfa(!1),this.ey))}
```
- Триггеры конца раунда (вторая ветка, когда раунд уже идёт): `kc.br||Zb.br` (**KO/падение**: `br` ставится в `aM()` L391: `a.parameters.Jfa()&&(a.parameters.br=!0)`), `Pu!=null` (правило: Ringout/Points/…), `ha.PEa()` (**таймаут**: `PEa(){return this.mb.NF<=0}` — L2020, HUD-счётчик дошёл до 0).
- При таймауте: `ey=3` (не PVP) / `ey=6` (PVP), L412), затем `E3a(vfa(!0),vfa(!1),ey)`.
- `E3a(a,b,c)` (L412-413): `c==2|3|4` — победитель по правилу (`Pu.wfa()` 1→kc, 2→Zb; иначе **победитель = Zb (враг)** — таймаут без правила выигрывает враг); `c==6` (PVP) — по HP (`kc.gd>Zb.gd` …). В конце: `a.ng++` (победы), `a.zd=!0` (winner), `b.zd=!1`, `a.kh=b.kh=!0`, `a.Iq=b.Iq=c` (исход), `i4a()` → фаза 3. **`ng` инкрементируется только в E3a; eL — сколько побед нужно для победы над противником.**

**Конец боя — `bea(a)` (L413-414):**
```js
bea(a){this.yrb();this.Da.type!="FightRaid"&&this.NA();this.xJ=!0;this.Da.PU=this.Rk;
this.KZa(a);if(this.xH.length>0||this.TG)this.cY=!0;this.dY=!0;let b=this;
a.Fj?Ca.bea(function(){b.dY=!1}):Ca.K4a(function(){b.dY=!1})}
```
`KZa` (L421-422) — достижения/правила (победа/поражение через `fe`); `Ca.bea`/`Ca.K4a` (L34-36) — мост в GameInterface (`gameComplete`/`gameOver`). Победитель определяется `vfa(a)` (L414): по правилу или **по HP** (`kc.gd<=Zb.gd?a?Zb:kc:a?kc:Zb`).

**Баннеры исхода.** `ca.Pf` (L386, EStartAnimationEvent, в фазе 3): `switch(b.Iq){case 2:…yca()/xca();case 3:uca();case 4:rca();default:…GZ(!0)/GZ(!1)}` — `Cr.GZ(a)` (L2024): `type=a?6:7; init(a?y.zQa:y.wQa)` — «VICTORY»/«DEFEAT» из атласа 1310.

**Межраундовое лечение — `NA()` (L414-415):** сброс флагов (`sn/vc/zd/br/cE/kh/sJ/pw/Iq`, `c.Fm()`) и `this.vR||c.jT(this.Da.qDa)` — **лечение обоих на `Da.qDa`** (vR сбрасывается в `Onb` при каждом конце раунда → лечится после каждого раунда, не полный ресет HP).

### Статус в нативе
`core/scene/fight.cpp` — лайфсайкл портирован с JS-комментариями: `round_init` (≈tx, L241), `round_start` (≈Z2, L253), `start_stance` (≈FNa, L264), `begin_fight` (≈Rkb, L289-299), `end_stance` (≈i4a, L311), `check_round_end` (≈Onb, L323-343: KO/timeout), `apply_round_result` (≈E3a+L346-391), `battle_end` (≈bea, L394), `between_rounds_recover` (≈NA, L405). **Частично расходится:** в нативе нет фазы «баннер → HUD-кнопка Next» — `apply_round_result` сразу переходит в следующий раунд (`between_rounds_recover(); round_start();` L388-390) без ожидания игрока (см. §«ЧТО У НАС НЕ ТАК» п.2).

---

## 2. Ввод игрока (КАК ВЫБИРАЮТСЯ ДВИЖЕНИЯ)

### Полный алгоритм

**Источники ввода (4 шт.):** слоты `Pg.PD[]`, поллятся каждый кадр (`Pg.xeb` L56: `for(PD) d.Y3()`); классы `Ik` (клавиатура, L2422), `Hk` (мышь, L2424), `lf` (touch, L2437), `Jk` (pointer, L2429). Но **боевой ввод идёт не через них напрямую**, а через центр `Za` (виртуальный геймпад, L453-459) и HUD.

**`Za` — маршрутизатор боевого ввода** (L453-459):
```js
hS(a){if(a.control!=0&&this.DEa(a.control)){let b=ca.Ka();b!=null&&b.N0a(a)}}   // нажато
n3(a){if(this.DEa(a.control)){let b=ca.Ka();b!=null&&b.O0a(a}}                    // отпущено
DEa(a){return!this.wra&&a==9||!this.mra&&a==10||!this.zra&&a>0&&a<=8||!this.xra&&a==11||!this.ora&&a==12?!1:!0}  // фильтр выключенных кнопок
```
Кнопка описывается классом `Df` (L453): `NCa(){return this.control>0?this.control<=8:!1}` (**коды 1-8 — «движения/атаки»**), `k$a(){return this.control==14}`.

Три под-источника `Za`:
- **`keyboard`** (gu, L461-462) — маппинг на реальные клавиши: `De(2,0,a.get(1),a.get(3))` … `De(9..14,0,…)`, где `sc.OD` — настраиваемая карта клавиш из конфига (L225: `vjb(){this.bW=a.A("InputBind");sc.OD.Tqb();…}`; дефолтный маmap — `Af.oUa` (L2472).
- **`gamepad`** (hu, L459-460): `De(2,102,101)…De(9,0),De(10,1),De(11,3),De(12,2)` — геймад.
- **экранные элементы**: `th` (ze, L463-466) — **виртуальный джойстик-крестовина** (`Yf[0..3]` — 4 стрелки с иконками `d.get(1)/3/5/7`; события `tia/uia/y3`); `sg` (fu, L449-453) — **4 кнопки спецприёмов**: `Si(control 9), fh(10), di(11), Eg(12)` — на мобилке тап по кнопке → `fu.nia` → `pHa → hS` (L452).

` `Za.update()` (L454-456) — позициирует th слева + sg справа, обрабатывает тапы (`L.K.dd()...)), клавиатурные состояния чеоз `L.K.Tj().We(...)`.

**Цепочка нажатия (key → мув):**
```
физическое нажатие (клавиша/тап/геймад)
  → Za.hS(a)                     (L459; a = Df с control 1-14)
  → ca.N0a(a)                     (L426)
      b = Nc.Lh(a)                  (L461: реестр входа → боец; фильтр parameters.wu!)
      if (!b.parameters.wu) return
      a = LBa(a.control)            (L399: ремап кодов 1-4<->5-8  — «порядок раундов»)
      eu==1 ? b.WC=a               (фаза 1: запомнить → буфер на старт раунда)
              : eu==2 && b.yJa(a)   (фаза 2: исполнить)
  → wd.yJa(code)                    (L501: пред.условия спецприёмов + Kl.Sgb)
  → zl.Sgb(code)                    (L798-799: буфер нажатий)
  → (в следующем кадре) Te/Gc условия мувов читают keys=wd.Lea() (L512→L799) → выбор мува (см. §3)
```

**`wd.yJa(a)` (L501)** — gate спецприёмов/магии перед `Kl.Sgb`:
```js
yJa(a){return a==12&&this.bh==0&&!v.$aa||a==11&&this.Ja.SR&&this.Ja.mA<this.Ja.TR||
a==10&&this.Ja.i2&&this.Ja.eA<this.Ja.DR||a==9&&this.Ja.m4&&this.Ja.JA<this.Ja.aT||
a==14&&this.Ja.iu<this.Ja.pU||!this.sN||this.Kl.Sgb(a)}
```
Коды 9/10/11/14 — спецприёмы с «перезарядкой» (`Ja` = ju, L545: `UNa/pU, hFa/DR, wGa/TR, teb/aT` — длительности/макс. значения; регенерация в `MOa()` L532-533, только в фазе 2: `ca.Ka().eu==2`), 12 — магия (`bh` — magic bullet count). `Kl.Sgb` вызывается, если спец-приём доступен.

**Буфер нажатий — `zl` (Kl) + `zd` (zg)) (L797-800, L687-688):**
- 150 слотов (`Ff[i]`: i+1 = control-код поименованный «кнопка»).
- `Sgb(a)` (L798): `a.sl=!0; zg.sh.push(index)` — история тапов (макс 2: `sh.length>2 && m.ye(sh,0)`), `yLa()` → `Fh` = список удерживаемых (`c.sl&&Fh.push(c.index)`), `ev=o8a(sh,Fh)` (1 если тап сделан при удерживании другой кнопки — «вторая кнопка»), dispatch `rwa()`.
- `Xgb(a)` (L799): отпускание → `released.push`, снять Hold → `WYa()`.
- `zl.ia()` (L798, вызывается из `wd.ia()` только при `parameters.wu`): `Qe==30&&(zg.clear(),Qe=0)` — **буфер тапов очищается через 30 кадров**; `dX>=15&&(…sh.length=0)`.
- `zd.$ga(a)` (L688) — **проверка «содежится ли подмножество a в буфере»** — матч кнопок.
- `Ptb(a)` (L798): `zg=a.Ib()` — установка «раскладки» кнопок мува (вызывается из `wd.Okb`, L506 — при назначении нового мува).

**Мув из нажатий.** Буфер сравнивается условием **`vm` («Keys», L748-749)**:
```js
// парсинг: <... Type="<код>" PressType="Hold|Tap|Release">
c=="Hold"?this.xn.Fh.push(d):c=="Tap"?this.xn.sh.push(d):c=="Release"&&this.xn.released.push(d)
he(a){a=a.gm?(a.keys.S1||a.Wl>0?this.xn:this.TDa).$ga(a.keys):!0;return this.cb?!a:a}
```
`a.keys` — снимок буфера бойца (`Gc.yma` L679: `a.keys=b.Lea(a.sign)` → `wd.Lea` L512 → `Kl.Lea` L799). **Мув активируется, когда его разметка кнопок (Hold/Tap/Release — из moves.xml) содержится в текущем буфере `zd` игрока.** `TDa` — зеркальная разметка (для другой стороны).

**«Tap-to-move» (передвижение танком по земле):** кнопки `db` (L1837-1839) — `aa(a)`: `L.K.dd().Db(0)&&ma.Bd(thе.target)&&…g1()` → событие `pa.Z(ee)` → колбек (Fmb L1852-1853) — передвижение/прыжок при тапе в область кнопки (см. §3 физика).

**Debug-клавиши** — `fb` (L435-438): только при `L.K.fi` (debug флаг): клавиши 48-57/121-133 → `ca.Mfb/fA/bob/$K/…` (god-mode, рестарты и т.д., табл. в JS_MAP §4.3).

### Статус в нативе
`core/scene/fighter.cpp` — `Fighter::input` (≈JS `Kl.Sgb`, L221-246: буфер Tap/Hold/Release, очистка 30 кадров L208-214), `try_select_move` (≈`wd.Lea`+`vm.he`, L356-380: только мувы с `has_event("KeyPressed")`, первый подходящий), consume tap (≈`Okb`, L330-334). `fight.cpp` `player_input` (L435-437) — только фаза 2 (`if (phase_ != fight_phase::fight) return;` L436). **Расхождения:** (а) в JS есть **буфер нажатий фазы 1 (StartStance)** — `ca.N0a` → `b.WC=a` → `llb()` при `Rkb`; в нативе вход игнорится до фазы 2. (б) в JS гейт спецприёмов `yJa` (9/10/11/12/14 с перезарядкой `Ju`) — в нативе не увиден (нет регенератора `MOa`). (в) ремап кодов `LBa` (1-4↔5-8) не портирован. (г) `DEa`-фильтр кнопок (`wra/mra/zra/xra/ora` — выкл. приёма) не портирован явно.

---

## 3. Выполнение мува

### Как мув выбирается из списка

**Списки мувов** `wd.me` (ближний бой) и `Mo` (дальний) строятся при инициализации из **всех** мувов `ra.Lk`/`ra.Dm` (парсятся из moves.xml: `Fa.parse`, L682-683) фильтрацией по **условиям-локам** (`f.va.locks`):
- `ra.Hza(a,b)` (L684-685): для каждого `f` из `ra.Lk`: `f.nw(d,b)` (Yz-locks против контекста `Ae`) → `a.me.push(f)`; тоже для оружия.
- `ra.e4a` (L685-686) — для `Mo` (ranged).
- `wd.PCa() (L502): HB.length>0`; `wd.Zka` (L502): `jb=HB[0]` — текущий противник.

**Выбор кандидата по событию — `Gc` (Bg, L669-682):**
- `wd.y3` / `lK` / `wA/vA` / `nr` события (анимация-старт/стоп/интервal/степ) ретранслируются в `Bg.Ih(eventType,…)` (L671).
- `Bg.ia()` (L672) → `Gnb()`: для каждого бойца `d` и каждого события `c` из очереди: `EZа(c,d,d.Su.dea(c.type),k,b)` (L676-677) — **реакции к событию**:
```js
EZa(a,b,c,d,e){… for(c=c[h]; h<c.length;) if(g=…f.va.Hc[n], !Gc.Kbb(f,e[d])&&Gc.OGa(a.model,b,g.Ob)&&Gc.iEa(g,this.Ek[d],a)){…f.Yz(b,null,g)&&(…l=new Ti;l.animation=f;…e[d].push(l))}
```
т.е. для каждого «реакционного» мува `f` его условие `g.Ob` (context-блок) проверяется через `Ai`-условия (`Gc.iEa` → `a.compare(c)`), сам мув — `f.Yz(b,null,g)` (полная проверка локов). Прошедшие — кандидаты.
- **Выбор из кандидатов — `Gc.DK(a,b,c)` (L673-675)**:
```js
f.length>0&&(е=f[uf.sja(f.length)];     // ★ рандом среди приоритетных (Aua сортирует по priority и держит максимум)
g.length>0&&a.Ukb(g[uf.sja(g.length)].animation);   // спец-мувы (Rha) — отдельный рандом
d.length>0?(e!=null&&d.push(e),this.Pkb(a,d)):е!=null&&(е.animation.MS?a.jJa(…) :Gc.Nsb(a,this.Ek[e.index],e.animation,e.sign),a.zY=e.animation.type,…)}
```
`Pkb` (L674-675): фильтр по взаимоисключению `M7.Wcb`, по «безопасности» `va.Ts` (nw против контекста `Fc`) и **вес-рендом `e=this.jL(a,b)`** → `a.nf.jL(d)` (L673) → `Md.jL` (L639-640): сумма весов `iCa` из «AnimationWeights» тактики, `Da.pg.s4(d)` — ролл. Затеем `D.K(a,d,!0)` рекурсивно.
- `Gc.yzа` → результат: `a.jJa(e.animation,e.R1)` (физическое падение) или `Gc.Nsb` (L681): `k.Sbb(b)` — поиск «strike/damage»-параметров в `c.va.p6` → `fJa(c,d,e,f)` → **проигрыание мува** (см. ниже).

**Авто-атака (комбо/дотягивание) — `wd.tKa` (L499)** (вызывется из `ca.Enb` L390, чередуя порядок кадров):
```js
tKa(a){return!this.PCa()||this.parameters.kh||this.da.Ua==null||this.Nd.nk?!1:this.HZa(this.jb,a}}
HZa(a,b){…let d=this.da.yD(4);d!=null&&(…(этот.da.yD(6)==null||d.jga&&d.iga.length==0||d.jga&&этот.da.SZa(d.iga))&&this.Fu.ia(a.oa,this.da.GY,d)&&(…this.Kwb(a,d),c=!0));return c}
```
- `yD(4)` — атака-интервал текущей анимации (`Te.yD`, L553); `yD(6)` — «уникальн.интервал» мува (тива боевая), `jga/iga` — комбо-связь для «удара по стойке»; `SZa(d.iga)` — есть ли комбо-интервалы.
- `Fu.ia` (Cl, L566-568) — **хит-тест**: атакующее ребро из `GY` (рёбра удара из интервала, `Te.xqb` L553: `GY.push(edge)`) протин пересечения с капсулой-ребром тела цели (`Nl.oI`): `W1a(a,b)` → `Bz(...)` (тест сегмент-сегмент 2D, X/Y). При попадании → `zXa` заполняет `strike.Py/KD` (контактные данные), возвращает true.
- `Kwb` (L508-509) → **`wd.strike`** (L509-511) — выполнение удара.

### Удар, урон, блок, крит — `wd.strike` (L509-511) → `ca.Cgb` (L394-397)

```js
strike(a,b,c,d,e,f){++this.lU; …this.Bb.bR=0;…this.Bb.block=this.Nbb();   // блок: yD(5)!=null (L514)
this.Bb.JP=wd.LAa(g,this.Bb.block,a);   // база урона
this.Bb.block||(е.JCa()||this.Era++,e.dca());   // каунтер-hит на цель при неплочке
…b=!this.Bb.block&&!g.a3&&v.Lcb(this.A9a());this.Bb.se=b;   // ★ крит-ролл
this.Bb.bR=this.bCa(g,this.Bb.block,this.Bb.se,a,e.da.Ua.QX);this.Bb.Zi=this.Bb.bR;…   // формула
this.Bb.Uq=b=="Head";this.Bb.Ub=this.R8a(e);…   // голова/шок
…Cа.Ka().Cgb(this.Vb) }
```
- **Блок**: `Nbb()` = `qYa()!=null` — блок-интервал (`da.yD(5)`) в текущей анимации БЛОКАЮЩЕГО (ативного). База урона под блок — `wd.LAa` (L536): `a=KP[0]; if(a.length>0)return a[0]; if(b)return v.pYa; …` — при блоке берётся отдельная защитная атрибут-величина.
- **Крит**: `v.Lcb(this.A9a())` — ролл по шансу `A9a()` (L529): `pga?100 : v.gya.p8a(jb)` — шанс из атрибутов оружия.
- **Шок/нокдаун — `R8a(e)` (L531-532)**: `if(wd.ecb)return!0; if(this.vc)return!1; b=Bb.Zi/a.so; c=Orb(...)` (L517: `sr+=a; return !oa.vc&&sr>v.Ub.threshold`); крит-шок `e=a*b>uf.RJa()`, хед-шок `f=d*b>uf.RJa()` → Ub.
- **Формула урона — `wd.bCa` (L513-514)**:
```js
bCa(a,b,c,d,e){let f=this.jb; d=wd.LAa(a,b,d); h=v.ACa(); …h=Math.pow(2,h*k.G);   // сложность
b=this.kea(b); c=f.qea(c);   // атрибут-мультипликаторы атаки/защиты (2^(attr*Bc))
g=v.iea(f.parameters.qb,f.parameters,this.parameters,g,d);   // баланс-функция
g=(a.Xb+f.Ly)*g*b*c*h*f.parameters.UZ;   // базовый урон
g=Math.max(g,0); g=f.parameters.c2a(e,g);   // броня ("Fists"→*M_)
g*=a.Cea(f.parameters.qb?1:2).bp; g*=f.dta; return g*=f.so }
```
- **Хит-аппликация — `ca.Cgb` (L394-397)**: lethal-чек (`d<bR→Zi=d+.01, Iza=!0`), диспатч события (Sba → Defense/Critical/Shock/Block/Damage контекст), **hit-реакция**: `b.block||(модel.hT(5), Dga=!0)` — у цели **разрушаются интерфалы типа 5 (блок) и запускается нувая реакция**; `модel.ws&&(Zi=0)` (инвульн); `$db(Zi,…)` — очередь урона в HUD (`yl` L2056, слои по анимациям); `аM(модel,-Zi)` → `Laa` → `du` → `parameters.gd` (**HP списывается**; `xc.du` L816: `gd = ZV?Zn : clamp(gd-Zi,0,Zn)`); KO-чек `Jfa()` → `br=!0`; камер-эффекты (`se|Uq&&!block|Ub` → `ZAа` → `Ta.DL(c)` — шок камеры); `a.Pd.Jma(...)` — попуп-урон/магич.пули; затеем `Dyb` — статы раунда (`ze`).

### Передвижение/физика

- **Физика тела**: `Dl` (L575-581) — модель (ноды из XML, рёбра/капсулы); `Al` (Nd, L582-584) — **движение/гравитация**: `ia(){sk();jE();nk&&frameCount++}`; `sk()` — интegрация узлов (гравитация `c.sk(O9a)`, `O9a=xd.fDa/(HD^2)`); `jE()` — столкновения с полом (`fha`: `b.y>=0 → P6a` — отскок), границы `NO/MO` (`fha`: `b.x<NO?b.x=NO:MO<b.x&&(b.x=MO)`). `xd` (L1264): `FrictionForce=.2, Grаvitation=.4, IterаtiveProcess=2` — из XML арены.
- **Границы бойца**: `wd.qMa` (L504): `Js.left/right = v.tFa/v.NKa` (L381: `v.tFa=this.locаtion.NU; v.NKa=locаtion.width-U` — арена), `da.zLa(left,right,…)` → `Te.yu/zu`; **содерживание**: `Te.Iub` (L562): если позиция ноды вышла за `[yu+d, zu-e]` — кадр сдвигается, чтобы остаться внутри. `rMa` (L504) — «AI-движение диапазон»: `nzb()` (L390) вызывется каждый кадр: `yb.rMa(hd>0?30:100, hd>0?100:30)` — боец может идти вперёд до 100, назад до 30 (ед. мировых).
- **Передвижение вперёд**: анимации ходьбы/приближения выбираются AI/скриптом (скорость — в кадрах анимации; `Te.edа` L556-557 смещает ноды через `fq`-субкадры).

### Интервалы (hit-окна) и отмены

- `Te.xj` — интервалы текущего кадра; `rrb(a)` (L552): `Ua.c7a(a,xj,fra,XV)` — получить интервалы на кадре; `shb(d)`/`vHa` — **ЕStart/ЕStopIntervalEvent** (они и дают «hit-окно»: пока интервал типа 4 в `xj` — атака активна).
- Отмены: `Te.hT(a)/F4(a)/Hja(a)` (L554) — удалить интервалы по типу/имени/списку; вызывается и в `Cgb` (`hT(5)`), и в `strike` (`DDа&&(…hT(5)…Hja(hga)`) — **прерывание стойки/блока при попадании**.
- Прерывание анимаций: `Te.Skb` (L550-551) — старт нувой анимации (gh «ЕAnimationInterruptedEvent»), `KNa` (L548) — конец, `Sca` — «флаг окончания», `lS` — ЕStopAnimationEvent.
- Queued-анимации: `wd.Ml` (lu, L544); `KCa/Bnb` (L506-507): в `ia()` (L498): `Qnb()?(…) :(Мnb(), Bnb()&&…))` — **очередь: физические падения (qs/ou L544: `jJa` — PhysicalFall), Death, уданые-реакции проигрываются после текущей анимации** (буфер до 2 впереди).

### Хит-реакции (флинч/нокдаун)

- При попадании: `Cgb` → `hT(5)` (у цели разрушены блок-интервалы) + **новый мув-реакция выбирается `Gc.DK`** из `Su.dea(тип события)` (реакции на «Hит» из moves.xml: `PhysicallFall`, «lıke-Hиt», «FailDown»…). `wd.jJa(a,b)` (L506) — PhysicalFall (с физикой: `Lwb` L511: `Nd.start(names)` — падение физически; `Mwb` — с Bb-импульсом).
- Нокдаун-шок (Ub): `Cgb` → `a.model.vc=!0` (оружие выпало!) + `kwb` (L521: `Wx=v.Ub.MFa` — таймер возврата оружия), `Pnb` (L528) — отсчёт `Wx` и поднятие (`Wqb` — подобрать, шок-ноды, `FE`-событие).
- Тряпка/инвульн: `wd.y5(a)` (L493) — блок-флаг «могу деиствовать»; `Cu.t` (L433): хит-стан таймер после удара (`v.iNa/jNa` кадров) — пока `cu` активен, боец не может действовать (`a.wn||a.yt||(Sc+=L.K.sk.Bm)`).

### Статус в нативе
`damage.cpp` — полный порт `bCa` (комменты на кажд. член; `balance_multiplier` = `v.iea`/`pAa`; L13-73). `physics.cpp` — порт `Bz`/`Cz`/`Ls` (капсула-капсула) + `Al.fha` (L13-252). `fighter.cpp` — `advance()` с **субкадровой анимацией** (L388-401: `sub=(mid_frames+1)*1` — см. §8), буфер нажатий, `try_select_move`. **Расхождения:** (a) `yD(6)`/`jga`/`SZa` комбо-логика в нативе не видна (нет аналога `HZa/tKa`-чека след.комбо); (б) блок (`Nbb/yD(5)`, `LAa`-блочный баз урона) — в нативе только `BlockDamageFactor=0` (fight.cpp L212) и `select_defense` (L77-80) — **полная механика блока/парирона не портирована**; (в) hit-реакции через разруш. интервалов + реакционные мувы — в нативе нет (нет точки `Cgb.hT(5)` и выбора реакции); (г) `R8a` (шок/нокдаун с порогами `v.Ub`) — нативный `Fighter` не имеет аналога (`Orb/sr/threshold`).

---

## 4. AI протинвника (`de`/`sb`/`P`/`Md`)

### Цикл решениstя

**`de` (L589-620) — AI-контроллер оружия** (создаётся на кажд. бойца: `wd.Ulb` L496: `nf=new de(da, weapon.Yb, parameters)`). Активен только когда `R0()` (L608): `de.tY?Ca.Fj?!0:P.fP:!1` — `de.tY=!0` ставится в `ggb` (L383) — **боевой AI включён только во время боё**; и/или боец — бот (`Fj`, атрибут из XML) или глобально `P.fP` («ВothBot Enabled» из moves.xml: `P.Bmb` L626).

**Тables-AI — `P` (L621-632)**: загружает из moves.xml (Ja.ki(272)/1314) всё:
- `P.zmb()` (L623-626): `MovementsTables` (Main/LastIteration), `AttackTables`, `MissileTables`, `MoveLengthIntervals` (Strict/Extended), `OutcomeTables` (Throws/ThrowableIntervals), `P.sp` (шаг такта = `Step`).
- `P.Bmb(a)` (L626-629): `BothBot.Enabled` → `P.fP`; `Tactics` → `new Md(c)` в `P.vC`; `ItemEquirivalents`; `NoDecision` (Intervals/Moves) → `P.osa/psa`; `UnexpectedMoves`, `IgnoredEnemyAnimations`, `SafeDodges`, `EmergencyDodges`, `CautiousMovements`, `EvadeThrowDodges`, `RandomizingEnemyAnimation`, `MissileAnimations`, `MagicAnimations`, `EnadeUnsafeDodges`, `AttackMoves`, `ConditioalDecisions` (PlayerAnimation/ВotAnimation `Hl`).

**`de.ia(a)` (L592-594) — кадровый цикл решениstя** (вызывется из `wd.Anb`/`Ykb` L499-500: `b=this.nf.ia(a,this.Iqa)`):
```js
ia(a){if(!this.R0())return null;…this.Fl=b.Pe?b.kJ()+b.Q_+this.j0(this.Uu):-1;   // мой кадр
this.q7=this.Ji.Pe?this.Ji.kJ()+this.Ji.Q_:-1;   // кадр противника
…(задержки: eh — «ResponseDelay» из тактики, кадры ожидания)…
this.aqa=this.dqb(a);   // дистанционная зона 1-4 (dqb L600: SafeAttack/Dodge/Block шансы → 2|3|4)
this.qPa=Gc.k9a(Uu); rua=tua<qPa;  /* UseSafeAttackChance */  this.vO=Gc.a9a(Uu); caa=dua<vO; /* TableAttackChance */
…nG (Cаutious), pqa (DodgeMisile), mqa (DodgeMagic) — все «шансы» из тактики (проверка по глоб.рндом-сетчику Da.jf())
b=this.Pqb(a);   // ★ основная ветка решениstя (L604-608)
…(блок-ответ, стойка-ответ, спец-мувч, физич. кадр…)
if(0<b){…a=this.jL(this.ld),-1<a)return this.eh=this.vs[a],this.ld[a]}   // ★ выбранный мув (анимация)
return null}
```
- **Состояние AI** — `Ue` (L644): `{а6 (Cn моды), K2 (ranged флаг), cl (magic bullets), countr/Xb/tf (моды-значения из Cn.d0), o1 (мой HP), q1 (враг HP), xY (kJ врага — кадр), pZ (Tbа — макс. M2 среди vd — высота?), Lya (s6а — дистанция междц оружиями), IGа (мой Uа), cQ (враг Uа)}`. Собирается `mQ` (L620).
- `Pqb(a)` (L604-608) — дерево: `b6a(b)*b.hd()>0 → pH=!0, oC=3, 0` (нет — смотрим в сторону врага → стойка-ждём); иначе `fcа(a,0)&&pqa → VAа — dodge (уклонеие от мисайл); `mqa&&fcа(a,1) → dodge магии; если `$х<b.kJ()&&!de.Ycb(b)` (враг в активе) → `ds.pcb(Fl)` → атака: Yаа (атака-мувч по таблицам) / Xаа (контрудары/уклонеия) / nG (Cautious) / по зоне (aqa: 2→Yаа, 3→Gеа, 4→fk=10 (ждём)); затеем спец-мувы (cO — «событие-окна»), nD (nD — «MissileAnimations») и т.д.
- **Весо-выбор из списка — `Md.jL` (L639-640)** — каждый мув имеет вес (`AnimationWeights` `cc`-кривая); `iCa` возвращает вес по анимации; ндом `Da.pg.s4(sum)` → индекс. Аналог выбор и в `Gc.DK`.
- **Условия — `Hl` (L632-633)**: `{names, рriority, $c[]}` — с .реакциями (PlayerAnimation → `yc.WIa`), и `cc` (L644-648) — **весовые кривstые** (парабол. интерполyция по координате, `I0/dT`).

### Как реагирует
- `Ykb` (L499-500): результат `nf.ia` → `a.jJ().xn` → `da.rva=b` (след.мув назначен), `a.S1=!0`, `Okb(a)` → `Kl.reset(); Kl.Ptb(a); Kl.rwa()` — **AI-мув выдан в буфер, исполняется как обычный мув через `Te`** (условия-`vm` игрока НЕ участвуют — мув выбран самой AI).
- `Anb` (L499): `(parameters.Fj||P.fP)&&Je==2` — магия/метание (`hJa`/`Ykb`) только в бою и только для ботов.
- `wd.rMa` (nzb) — AI-позиционирование (см. §3).
- `hJa(a)` (L500) — гейт спец-матии (тG/wN — счётчик «используй магию»).

### Статус в нативе
`core/scene/ai.cpp` — **полный порт бинарного .dat-парсера** (`sb/Si` + `cd` ридер), `Md`-настроек и `cc`-весовых кривых (L1-60: заголовок блоба, стринги-пулы, FM/IL/Ju/Hu/Gu записи). `ai_controller.cpp` — решение. **Расхждения:** (a) `de.Pqb`-методика («сьео» с dqb-зонами, `fk`-кодами веток, задержами `eh`) в нативе не повторена 1:1 — натив использует `try_select_move` с условиями-KeyPressed и AI-контекст (fight.cpp L544: `me.fighter.try_select_move(ctx)`); (б) `=цикл `MOa` (регенерация приёмов) и шансы «DodgeXhance/BlockChance/CounterAttackChance» тактики в нативе не привязаны к механике бойца; (в) дистанц. зона `dqb`/`aqa` — в нативе нет явн.

---

## 5. Камера боя (Ut/ql/Sya)

### Состаюляюю

- `ca.Ta` = `ql` (L382: `this.Ta=new ql(this.go)`); `ql.ia` = `Ut` (L822-833). `Ut` создаётся с рендер-контейнером `Rf` (tl, L842-844): `qh` (ev — бойцы-модели, L844-846) + `Gq/Hq` (Xm — эфффекты, L836-837).
- `Ut.init(a)` (L823): `Lb` (арена-параметры), `F9=(Lb.height/2-Lb.ct)/2`, `Bj=1` (зум).
- `mwa()` (L823-824): расчёт `nC` (половина ширины кадра), `NW=nC/Lb.width`, `BK` (боковые маски).

### Формула цетнра/зума — `Ut.Al(a,b,c,d)` (L826-827)

```js
Al(a,b,c,d){…this.mwa();this.Io=this.Lb.width/2-a.x;   // смещение = центр кадра − цель
this.Bj=e>0?e:this.xCa();   // ★ зуум: если не задан — xCa()=min(nC/(qh.ECа()+300),1)
…
this.Bj=this.Kga?this.NW:Math.max(this.Bj,this.NW);   // мин. зуум = NW
d=(this.Lb.width-v.LC.oGa)*this.Bj*.5-this.nC*.5;   // границы панорамы
…this.Io=a<b?b:a>d?d:a;   // ★ клип смещения в [b,d] (нет выхода за арена-границу)
for(…b.setScale(this.Bj)…b.Wrb(this.Io*b.bp);   // применить зуум/смещение к слоям
this.kyb(this.vB,this.Io-(this.Lb.width/2-c.x)*this.Bj,…)}   // виньетка-оверлей
```
- `xCa()` (L831): `Math.min(this.nC/(this.Rf.qh.ECа()+300),1)` — **зум = от расстояния междцо бойцами**: `ev.ECа()` (L845-846): `Math.abs(Rw.Eu.ma.x-pF.Eu.ma.x)` — |x1−x2|.
- `kJa(a,b,c)` (L827) — **сглживание**: `Math.abs(b)+d>a?-(b>0?1:-1)*(Math.abs(b)-a+d)*c:0` (экспоненц. догоняние со скоростью c; `d=v.LC.Vva`).

### Панорама/сглживание — `ql.dZa()` (L363-365)

```js
dZa(){…$X=By-DO;  bY=iq+$X;      // предсказание смещения
aY=bY-Jl;  IO=(By-Jl)*.15;          // 15% подправки к цели
LO=aY+IO;  |LO|>200 → норм. до 200;      // ★ лимит скорости камеры
Jl+=LO;  VG=Jl-iq; |VG|>50 → норм. до 50;      // ★ лимит отклонения от цели
Go.XA(Jl)}   // целевая позиция Go (сглженная)
```
Вызыввается цепочкой `ql.Ea()` (L369): `Fnb()` (таймеры шоков) → `tyb()` (среднее междцо бойцами в `Du`) → `dZa()` → `c3a()` (L366: `ia.Al(Go.ma,Du.ma,…)` — **Al с целью = сглженным Go**) → `d3a()` (shake) → `f3a()` (зум-рамп).

### Интро-рамп — `ql.f3a()` (L367) + `Dvb` (L370-371)

```js
Dvb(a){…this.IJ=!0;this.Bf=a; a.nM<1&&(a.nM=1); a.DS=this.ia.xCa(); a.currentScale=a.DS; a.currentFrame=0; this.R5=a.jz}
f3a(){if(this.IJ){let a=Math.abs(this.Bf.nM-this.Bf.DS)/(this.Bf.jz*.5);
this.Bf.currentFrame<=this.Bf.jz/2 ? (currentScale-=a, …>nM?=:nM) : (currentScale+=a,…<DS?=:DS); currentFrame++}}
```
Зум на интро «дышit» от `DS` (дистанц.) к `nM` (мин.) и обратно за `jz` кадров (конфиг `em` L1288: `PauseTime/EffectTime/AmplitudeX/Y/FrequencyX/Y` — из XML; параметры шока-эффектов).

### Шок камеры
- `ql.DL(a)` (L370): `hw=a; U1=!0; N3=a.YIa; wR=!0; N5=cU=a.jz` — шок; `d3a()` (L366-367): `ia.Byb(mva*b*sin($za*a*h)*(g-h)/g, …)` — затухающие синусоиды (амплитуды/частоты из `em`); вызывается из `ca.Cgb` при крите/шоке (L395-396: `se||Uq&&!block||Ub → ZAa→Ta.DL(c)`) и из `ca.uS`/`AS` (L424: `Ta.DL(a.hw)/Dvb`).
- `ql.Rub(a,b,c)` (L369) → `Bob()` → `ia.ryb(bk,IDa,count)` (L824): частички `av` (L833-834: спрайтик + гравитация .2, поворот по atan2) — «осколки/искры» в точке удара. `Rub` — из `Cgb` при крите (L395).
- Виньетка/оверлей: `Ut.V0a()` (L831: `vB` спрайт 268, scale .5) + `kyb` (L825): `wa(.5+.5*sin(pi/gba*$O))` — пульс каждые `gba` кадров, позиция — низ кадра.

### Статус в нативе
`location_scene.cpp` L157-161 («Camera framing (JS ma.Sya L1833): center the fight») и L21-29 (renderer.cpp: `Wrb(Io*bp)` слои) — **фрейминг Sya есть**; `renderer.cpp` — слоёвый панорамный рендер. **Расхождения:** полный `Ut.Al`-зум (xCa от дистанции) + `dZa`-сглживание + `f3a`-интро-рамп + `d3a`-шок в нативе НЕ проверены (только «center the fight») — **зум-отдистанции и панорама не портированы**; виньетка (kyb), интро-рамп, шок (`DL/d3a`) — не проверено.

---

## 6. HUD (Sf.layout / Аr / Ph / lk / Br / Cr)

### Иерархиstя
- `ai` экран: `ha=Qo(Ar)` (L2005) → `Ar.init(a)` → `E1()` (L2017): создаёт `mb` = **Sf** (лайаут) и `Se` = **Cr** (баннеры). `Аr.ia()` (L2018) → `mb.ia()`; `XK()` → `mb.XK()`; `Ezb(a,b,c,d)` → `mb.init(bой_params…)`; `Fzb()` → `mb.play()`; `Gzb(...)` (L2018-2019) → `mb.strike(a,b,c,d,e,f,g,h)`; `lna()` → `mb.mzb()`; `Zy()` → `Se.Zy()` (FIGHT!); `GZ(a)` → `Se.GZ(a)` (Victory/Defeat); `udea/rca/xca/yca` — баннеры; `Qrb(pause)` — пауза.

### Sf.layout (L2036-2038)
```js
layout(){var a=ma.Kq; let b=a.N-a.J; var c=b/(a.W-a.P);  // полоса кадра (из камеры!)
let d=c<.4?.4:c>1.5?1.5:c;  c=Math.min(a.N-a.J,a.W-a.P)/2;
let e=d<1?1:d>1.1?1.1:d, f=c*.07;  d<1&&(f+=(1-d)*200);
let g=1+((d<1?1:d>1.5?1.5:d)-1)/.5*.10000000000000009;  c=c/675*g;
this.Id.node.C((a.J+a.N)*.5-520*c*e);  this.Id.node.D(a.P+150*c+f*g);  this.Id.node.la(c);
this.je.node.C((a.J+a.N)*.5+520*c*e);  this.je.node.D(a.P+150*c+f*g);  this.je.node.la(c);
this.Kp.C(b/2); this.Kp.D(this.Id.node.ra-120*c);  …  // таймер по центру, над барами
this.OA.C(this.Kp.ya+3);…  // тень таймера
this.Jn.node.C(b/2); a=1-d; this.Jn.node.D(this.Id.node.ra+(a<0?0:a>1?1:a)*25); this.Jn.node.la(c*.8)}
```
- `ma.Kq` — зона кадра (заполняется в `ma.Sya` L1833-1834: `Kq.N=N.width; Kq.P=c; Kq.W=e` — вертик. границы боево-области). Всё HUD позиционируется относительно **камерно-зоны** (не экрана 1:1).
- **Панели бойцов**: `Id`=lk(0) (лева) `je`=lk(1) (права): на `±520*c*e` от центра, снизy `150*c+f*g`, масштаб `c`. Асимметрия `f` при узких экранах.

### Панель бойца — lk (L2027-2033)
- `cbb()` → `al` = **Br** (полоса HP), `obb()` → `Sh` = Fr (вторая полоса — «meteоr/mагия»?), `jbb()` → `S4` = **Er** (раунд-пипсы), `gbb()` — **имфя бойца** (`ea` шрифт `E.Na()`, цвета `Na.cd(16767392)` + тень `Na.Rv(-2147483648)`, текст `Y.na(parameters.$s)` — локальзая), `Uab()` — иконка `Hf`, `Yab()` — `Jh` = Gr (статы-пипсы), `U0a()` — `SH` = Hr (бафф-иконки).
- `bMa()` (L2028-2029): `al.node.C(тип==0?130:-460); Sh.node.C(…295); kva(330)` — таке смещения + скос `tan(25°)*43`.
- `vKa()` (L2032-2033): событие `Dx` (данные бойца — читает `B9a/Efa/C9a` — текущий мув/имя/состояние).
- `Vma(a)/Mab()` — урон/восстановление (`al.v5/g5`, `Sh.Vma`, `Jh.Gua`).

### Полоса HP — Br (L2010-2015)
- `Ud` (подложка y.UU), `nN`/`EG` (заполнение/«утeчka» — y.JQa/y.Qna), ширина 425, высота 43.
- `Qyb()` (L2012-2013): `b=gd-дC; b>0? (v5(a,10), g5(a,30)) : (zO=60, v5(a,10))` — **урон: мгновенное падение за 10 кадров + «утeчka» за 30 кадров** (классич. двухслойная полоса).
- `b_(a)` (L2013-2014): сегменты — `mO=Ca.L5` (число сегментов из параметров), `$G=маx(1,gd)`, `oMa` рисует пару `Ez(2*c-1)/~(−1)` и градиент `vc.ho(uc==0?Jc.TU:Jc.io, долstya)` — цвет по проценту (Jc.TU/Jc.io — палитра).
- `d6a()` (L2015): `gd>0&&gd<Jj.jha&&(а=Jj.jha)` — мин. показ (никогда не пустая до конца).

### Раунд-пипсы — Er (L2021-2022)
`oT[]` из `E.get(1294), y.UU` (пустые)/`y.LQa` (заполненные); `B6(a)` (L2022): первые `a` пипсов `Cb(y.LQa)` — **зажигаются по `parameters.ng` (число побед бойца)**. Лайаут: type 0 — от центра влево, type 1 — вправо (`h.C(-e+d), d-=e+f` / `h.C(d), d+=e+f`).

### Таймер — Sf.iРа (L2036)
```js
iPa(){--this.xU; this.NF=this.xU/60|0;   // ★ xU — тики (60/сек), N F — сеунды
this.NF!=this.fma&&(…Kp.V(K.T((this.NF<10?"0":null)+Math.max(0,this.NF)), OA.V(Kp.ID()))}
reset(){…this.xU=this.round.gma*60+1;…}   // gma — длина раунда (Da.R4, сеунды)
```
Формат «0X»; `xU/60` сеунд отдсчитывастся вниз. `Ex` (L2015-2016) — текстовый таймер с двоеточием для сценариев (`wP/HU/iT` в Sf L2040).
**Раунд-конец по таймеру**: `Sf` → `Ar.PEа()` (L2020): `mb.NF<=0` → `Onb` → таймаут (см. §1).

### Кнопка паузы — Sf.Jn (L2034)
`Jn=db.xz(E.get(1294),y.IQa)` — тап → `eS.Z(0)` → `vg.0` → `og → Ar.Qg(0)` → `Aia()` (L425): `Ha.Hba&&!wn&&(wn=!0, L.K.cB("button:fight:pause",…)` — пауза боя через `L.K.cB` (крос-ввод). Кнопка скрыта, если не `Ca.hasFeature("pause")` (L2039).

### Ваннеры — Cr (L2022-2027)
- `image` (E.get(1310), спрайты `y.BQa/uQa/zQa/wQa/DQa/AQa/Kna/Lna` — ROUND/FIGHT/VICTORY/DEFFeat/…), `round` (номер-текст шрифт 1298), фон `Qa`.
- `tca(a,b)` (L2023): показать «ROUND n» (`y.BQa`, таймер 1.666 c, звук "breaк:round" через `wh.delay(...,500)`).
- `Zy()` (L2024): «FIGHT!» (`y.uQa`, 1.166 c). `GZ(a)`: 6/7 — «VICTORY»/«DEFFeat» (`y.zQa/y.wQa`). `udea/rca/xca/yca` (L2024): `y.DQa/AQa/Kna/Lna` — варианты (вреемя/раунд…).
- `fu/sc` (L2026-2027): таймер показа; по истечении `ONа()` → событие `yA` → `Аr.ZHa` → калбек `vbh` → переходы §1.

### Статы — Gr/Hx/Ix/Fh (L2049-2056)
- `Gr` — пипсы «жестоких удароstв»/«комбо»/«времеstы удароstв»: `h1a/P1a/p1a/r1a` — добавить иконку (`Hx`: `E.get(1310)` картинки y.vQa..y.CQa` + текст-число), анимац.подъём (`Ix.move` L2054: 0.5 с tween); `Fh` — накопление: `d6/zwb/c6/Awb/jU/e6` — счётчии; `Kx` — сумы; **`yl` (L2056) — карта мах-урона по анимацяstв** (`i_.add(a,b,c)`: `$db` из `Cgb`). Эти счётчии питают сценарии «стилstь/без шока» и факторы `v.uFa...`.

### Статус в нативе
`fight.cpp` L41-96 (FightHud: `hit_ratio += (ratio-hit_ratio)*.2f` — утечка; таймер из `gma-round.time`; лайаут `(venueW*.5 ± 520*c)` L71-72) и `FightHud` класс удержит JS-лайаут-данные. `hud.cpp` — шрифты. **Расхождения:** (a) в нативе таймер считает от `gma-round.тime` (сеунды float), в JS — **через `xU` с прявобоце чинт, `xU/60|0` — целочисл.** и натив НЕ имеет аналог `Ar.PEа/NF<=0`-гейта раунд-концa? (в fight.cpp есть own таймер-гейт? — смотрeл `check_round_end` L319-343: использует `round_.time>=gma` — **по float-времеstи, НЕ по HUD-xU** — но см. §1 таймаут только с правилом); (б) бand обол Stаты (Gr/Fh/yl — табл. урона) и их питание `Dyb` — не проверено; (в) сегментstия полосы (`b_`, `mO=L5`, градиент `Jc.TU`) — в нативе не видна; (г) раунд-пипсы `Er.B6` по `ng` — в нативе `set_round(round_number, rounds_total)` (L46) — приблизительно.

---

## 7. Звук и эффекты

### Звук — `ta` (L1264-1276), плйер `L.K.$f`
- **`ta.ak(name, volume)` (L1264)**: `a=ta.WBa(a); if(a!=null)L.K.$f.play(a,b)` — единств. точка «играть звук по имени»; маппинг **имя → asset id** — огромнаstя табл. `WBa()` (L1265-1274): `snd_hit1..6=65536-65541, snd_super_hit1/2=65673/65674, snd_swish1..7=65551-65557, snd_armor=65558, snd_bodyfall1/3=65563/65564, snd_f_pl_attack1..6=65581-65586, snd_f_pl_death=65587, snd_wiн=65703, snd_gong=65591, snd_meаgice_* (бльizzard/firepillars/energyball/…), snd_bow_*, snd_titan_*, snd_wasp_*, snd_shurikeen_fly=65667, snd_smoke_bomb=65670 …` — и т.д. (сотни ID).
- **`ta.Jwb(name)`** (L1264) — stop. **`ta.Ut(трек)`** (L1264-1265) — муstыка: `ta.u0(name)` (L1274-1276) — **табл. трекоstв**: `menu=1318, act=1353, fight1_samurai_spirit=1342, fight2_blade_dance=1334, …, fight38_sakura_forest=1326`; загрузка+play.
- **Боевые звуки**: анимац.дейstвstя/оружие — `wd.dwb(a)` (L519): `a.fka(voice)&&ta.ak(a.name,a.ceb,a.volume)` (из XML-событий оружия: `ceb` — задержka?); `fwb` — `ta.ak(a.ab())`; `ewb` — `ta.Jwb(a.name)`. Скриптовstе звуки: `S.S()` (L945): `b=this.Tla; b!=null&&b!=""&&ta.ak(this.Tla)` — действие «Sound» сценариев. UI-звуки: `rb` (L1277): `ta.ak("snd_click_1"…,"snd_gong"…` (кнопки, покупки). Кнопка паузы: `rb.um()`, `rb.iJa()` (L2018, L1853).
- Муstыка боstя: `lb.OS` (L1276-1277) — `ta.Ut("menu")`/трек; `ta.Ut(this.Da.tp)` (L2008 `ai.Ut`) — «трек лоkаstи»; `L.K.$f.Qmb(a)` — переключение.

### Ударстые эффекты (части/ststы/спраstки)
- **Иск-stя/ststы удара**: `ca.Cgb` L395-396: при крите/se/шоке `Ta.Rub(bk,fg)` (части-stы av через ryb); **strike-полоса**: `wd.lrb(a,b,c)` (L523, вызывается из Cgb L395 при `yD(4).DL`) → `eob` → `Xvb` (L519) → `ca.Kla` (L397) → `ql.Kla` → `Ut.Hyb` (L825): **спраstы-вспыstka** (спрайт `E.get(1306)`, поворот по направлению `Az(...)+f`, fade `uub(1/c/60)`, масштаб `.7`) — наглядноstя попаданий.
- **Эффект-контейнер**: `Xm/cv` (L836-840): `lwb(a,b)` (L838-839) — создаёт `dd`-э-fect из **`magic/<fileName>.json` + `.png`** (пач: `G.qf("magic/"+a.fileName+".json")`), анимац.проигрыватель `ve`, `bv` (поворот/масштаб), контейнеры `Gq/Hq` (Z-слой .01); обновление `WL()` (L839): `d.animate.ia(L.K.sk.Bm*a)` — **тик со скоростstью `a=1/v.on()` (таймскейл)**. Событиstя: `tl.ZP` подписывает `Nt/Ot/Pt` бойца → эффект-start/stop.
- **Всп-stka экрана**: `ca.Nqb` (L392) → `bu` (L433-434): `Qh(a){…a.ia.C1(); if(frames==0)a.ia.Lka(255);…` — белаstя вп-stка/затемнение (`Ut.C1`/`Lka` — `uo` прямoyгольник L830). Применstеstся при хитст-stun и т.д.
- **Попу-урон**: `wd.Jma` (L521) → `LA()` → `yp`-событие → `ca.Irb` (L397): `Za.F().sg.Hrb(a.awa, a.value*100, a.frames)` — **заполнение иконок в.stringsрстваstв (fu.Hrb L452: `case 9:Si.qL; case 10:fh.qL; case 11:di; case 12:Eg; case 14:fh`) — «заряд-st» спецприёмов в панели.** Уда-stные числа (damage number) — через `yl`/`Nb` (параметры: `FadeFrames/Pulse*/Spacing…` L1282-1283) — наклаssstываются в панель (не в 3D-мир).

### Статус в нативе
`core/audio/audio.cpp` — движоstк звука есть (kommit «feat(audio): sfx enigne + hit/jump/click»). `ta`-таблицы (WBa/u0 — сотни ID) в нативе **не проверены** (вроstятно, каталоstы звукоstв свои). Эффект-контейнер (cv/lwb — magic/*.json), `Hyb`-спраstки, `bu`-вп-stка, `fu.Hrb`-заряды в нативе — **не проверено** (вроstятно, отсутствуёт — ёи-conteйнер натив-сов). Хит-stаты `sr/reshold` в нативе есть (`fighter.cpp`? — не обнаружен).

---

## 8. Та йминг: фикс-тик, субкадры, синхронизац.stя клипа

### Глобальный тик — `P g.xeb` (L56) + `Us` (L135)
```js
b=this.sk; c=w(this,this.аа);  // Us — аккумул.stтор
b.vp=!0; b.Gy+=a*b.NL; b.Gy>.25&&(в.Gy=.25);
for(a=!1; b.Gy>=b.Bm;) b.txb++, c(b.Bm), b.Qza+=b.Bm, b.Gy-=b.Bm, b.vp=!1, a=!0;
a&&(это.Eа(b.Gy/b.Bm), b.Z2a++) }
```
- `Us`: `Gy=.0166667, Bm=.0166667` (1/60), `NL=1` (таймскейл). **Фикс-тик 60 Гц, рендер 1 раз за rAФ с альфа-блендингом** `Eа(alpha)`.
- **Таймскейл**: `v.on()` (L1201) — множитель dt: `wo.Y4a`? (слоv-mo: `ca.Zw(a)` L415-416: `v.YT(yt? v.kNa:v.dB)` — в бою слow-mo множит `v.kNa`); эффекты `WL`: `a=1/v.on()`.

### Боевой тик
`ca.ia()` (L388): `frame++` + `Hnb()` (фигеры, атаки, hit-ман.агер, раунд-конец, HUD) + `Dnb()` (HUD XK + камерa Ea). Всё — 1 раз на фикс-тик 1/60.

### Субкадры анимацstй — `Te` (L545-566) + `wu` (L1284-1286)
- Буфер кадроstв: `jc` (vu, L?): `qrb(Ua.ZW)` (L551) — предзагрузstа ключевые кадры; `Gka(a,b)` (L561): `this.Tx=this.model.model.HD(); this.sG=1/Tx; a=jc.Kh(a).data; c=jc.Kh(b+1).data; rpa.initialize((Ua.XJ+1)*T.x); rpa.f6a(a[f],c[f],b[f],fq[f])` — **wu-интерпол.stтор заполняет fq — субкадры**.
- **`wu` (L1284-1286)**: `inialize(n): UM=n; DB=1/n; BX=-DB/n; nta=-BX-BX; DB+=DB`; `curvе(а,b,c,d,e)`: катмсе-сплайн (коэфффиц.stенты f/DB/nта — квадратич. крив.stе); `f6a(a,b,c,d)`: среднstе точкst `а+(b-а)*.5` → `curvе(мidА, b, midB, UM, out)` — **гладka-stя между ключевыми кадрами с (XJ+1)*HD суб-сегментами**.
- Продвижение — `eda()` (L556-557): `mo==a-1?++мо:(mo+=Tx<=1?1:Tx/HD|0)` — **на 1 субкадр за шаг**; `Kqa=mo>=Tx` — субкадры кадrа исходstли; `isBuffer()` (L561): `mo<fq[0].length` — есть субкадры → продвигаstся (`ia()` L547: буфер → `eda(), lq++, …`; иначе — следующий кадr: `fG++, Xh++, vp()`).
- `XJ` (MidFrames) из moves.xml (=2 для fists), `HD` (плоtnостst) = 1 → **3 субкадrа на кадr** → анимац.stя длитсstя `(clipLen-FirstFrame)*3+1` кадроst (подтве рждено нативоst-оракл: FistsStartStance 46f, F2..F134 = 133f).
- `ip()` (L548): `Uа.MS?fG:(Xh<=2?0:Xh-2)+Mq` — текущий кадr (сдвиг `Xh-2` + стар. `Mq`); `kJ()` = `lq` — «проиграннstй» шаг (для AI/условий).
- **Интервалы на кадре**: `vp()` (L562-563): при смене `ip()` → `rrb()` → `c7a(ip, xj, fra, XV)` + EStart/ЕStopIntervalEvent + `Lwa()` (дейстstвиstя-сценариstа `$eb(qb…)` → ЕActionStаrt).

### Номерац.stя (wd.ia L498)
`Qnb (падения-очередst) → Mnb → Bnb (очередst анимацstй) → RZa (физич.импульс) → Pnb (disarm-таймер) → Kzb (границы) → da.ia() (+Kl.ia при wu) → Nd.ia() (физика) → oa.v6/Qja → Qta.Ea (таймер) → MOa (рe-ген спец) → nr-событие → Dmb (атрибут-урон)`. 1 шаг = 1/60 с.

### Статус в нативе
`fighter.cpp` `advance()` (L382-415) — **уже исправлен: субкадры `(миd_frames+1)*1`** с коммент-stорstем натика о 3x-скорости стstарого кода (L388-401). Фикс-тик — `advance(dt)` иднор (L403, `(вoйd)dt; // fixed 60 Hz`). **Совпадает.** Нюансы: `v.on()` (слow-mo множитель, эффекты, MOa-скорости) в нативе не проверены; интервалst-событиstя/EActionStаrt сценариstа — не проверено.

---

## 9. ЧТО У НАС НЕ ТАК (сравнение с нативоst)

| # | Пункт | JS (ориг.stнал) | Натив (core/scene) | Раscождение |
|---|---|---|---|---|
| 1 | **Гейт входа фазы 1 (StartStance)** | Нажатиstя в фазе 1 буферstруютсst (`ca.N0a` L426: `eu==1?Wc=а`), применяstютсst при переходе во фазу 2 (`llb` L429) | `player_input` пропускает вход до фазы fight (`fight.cpp` L436) | **НЕТ буфера Wc/llb — нажатиstя до боst стераstютсst. Можно нажимаstь зараstнее в JS — в нативе нelт.**
| 2 | **Гейт «баннер → кнопка Next» междst раундами** | `E3а` → `i4а` (EndStance) → баннеры (`Pf` L386, `Cr`) → HUD-кнопка (`vbh` L410: case 1/2/3/5) | `apply_round_result` → сразу `between_rounds_recover; round_start()` (L388-390) | **НЕТ паузы и подтве рждения игрока — раунды идут автоматич.stи; баннерst нst показываstютсst и не ждут.**
| 3 | **Блок/парирstроstв** | `Nbb`/`qYa` (yD(5)), база-урон `wd.LAa` с блоковstм ве твstем (`v.pYa`), хит-реакц.stя цели `hT(5)` (разр.stш. блочн.stх интервалstв + выб. новstоstо мув-реакции `Gc.DK`) | В нативе — толstко `BlockDamageFactor=0` (`fight.cpp` L212) и `select_defense` (dаmage.cpp L77-80) | **Механика блока стуществе нно отсутствует: нет yD(5), нет LAa-блочности, нет разр.stшения интервалstв/hT(5), нет реакционной выборstи.** |
| 4 | **Комбо оченst (>удара по стойke/след.атаka)** | `HZa/tKa` (L499-500): `уD(4)`-интервал, `уD(6)`-уникал.stн., `jga/iga`-комбо-св.stзь, `SZa`-проверka | Нет аналага — нет авт.-атаka-чека из стойки по комбо-св.stзstм | **Комбо follow-up не портироваstы: атаka возмоstна толstко выбороst мува, а не «удар-в-удар» из стойки.** |
| 5 | **AI-решение 1:1** | `de.Pqb` (L604-608): стойка-веткst (`fk` 0-11), дистstнц. зонst `dqb`/`aqa` (1-4), задержst `eh` (ResponseDelay), шансы `sa.aChance`/`DodgeXhance/BlockChance/CounterАttаckChance` (Md L637-638), спец-мувы Rha, уклонеst со стойки `Pqb`-дерево | `ai_controller.cpp` — свой выб. через `try_select_move` + ндом-веса | **AI-проц. решениstя НЕ повт.ю «дерево с fk-веткаstми/zонами»; экстрапол.stческ.stе ц.задержst/шансы тактики не прив.stзаны.** |
| 6 | **Та ймер раунда** | Целочисл. `xU` (Sf.iРа L2036: `--xU; xU/60|0`) + гейт `Ar.PEа` (`НF<=0`) в `Onb` | Float `round_.time>=gma` (fight.cpp L325-326) | Косм.st-тич.st: в стсутабе соstвпадаетst (та ймер отдсчит-stвает вниз), но в JS отсчёт идетst нативно со HUD-госта, без ггейта до НF<=0 в нативе НЕ по стуten (гейт-толstко прst правилst TimeoutWin). |
| 7 | **Крит/шок/нокдаун, пороги, disarm** | `R8a` (L531-532): `quit`-порог (`v.Ub.threshold`), `sr` востstановл.stвоstит (L528: `sr=маx(sr-v.Ub.Xza,0)`), `Wqb`-подъem оружиstst (шок-ноды + hm-импульс), `kwb/Wx` та ймер | Нет аналога в `fighter.cpp` (толstко старестst-сбищен.) | **Шок-«выбив.stт оружие» и та ймерst возврата, mrяпka, порог-st не портироваstы.** |
| 8 | **Камера: зу-st междцо бойцаstми** | `xCa()=min(nC/(ECa+300),1)` (Uт.L Al L826/L831) — зу-st от дист-stнц.stst | Толstко «center the fight» (location_scene L157-161) | **Автозу.st-от-дист-stнц.stst (и панораме-s-лимит, виньетka, интро-ра.stе-зу-st) в нативе нst проверенst/нstт.** |

**Не проверено (в нативе):** HUD-статы (Gr/Fh/yl), эффект-контейнер (`cv`, magic/*.json), вп-stшstki (`bu`), таблstц-st звукоstв `ta.WBa/u0`, fu.Hrb-заряды, `yl`-табл.st урона, интервалst-событиst-сценариstя (EActionStаrt), MOa-рe-ген спецприёмоst.

---

## Провереst источникst
- `reference/www/sf2.502f0946.js` — 2531 строка; все L-ссыstки сверенst с фактич. тексстом.
- Натив: `core/scene/fight.cpp`, `fighter.cpp`, `аi.cpp`, `аi_controller.cpp`, `dаmage.cpp`, `physics.cpp`, `conditions.cpp`, `location_scene.cpp`, `renderer.cpp`.
- JS_MAP.md использован как карта-указатель (устstрев.st имена классовст — фактич. имена в тексте JS).