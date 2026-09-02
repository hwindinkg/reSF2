# JS_FLOW — Оболочка и прогрессия Shadow Fight 2 (web), точная спецификация по оригиналу

**Источник:** `reference/www/sf2.502f0946.js` (2531 строка, Haxe 4.3.7) — номера строк 1-based.
**Ресурсы:** `reference/www/res/` (map/part0..6, locations/, items/, lang/, users_default.xml),
`reference/extracted/xml/res/stages.xml` (зоны/бои), `quests.xml`, `tutorial_quests.xml`, `list.xml` (каталог).
**Статус в нативе:** сверено с `core/app/screens.{hpp,cpp}`, `core/app/screen_manager.hpp`, `core/app/save_system.{hpp,cpp}`, `core/app/app.cpp`. Где натив не проверен — помечено «не проверено».

НЕ ДОВЕРЯТЬ `JS_POSE_PIPELINE.md` / `GENERAL_GAP_MAP.md` — этих файлов нет в репо; всё выведено из JS напрямую.

---

## 0. Ключевые механизмы (прочитать сначала)

| Механизм | Где | Что делает |
|---|---|---|
| `mc.K` | L122-127 | менеджер экранов: `stack[]` из `$d`; `Taa(cls,caller,info)` пушит экран; если у экрана есть недогруженные ассеты (`Pea().length>0`) — сначала пушит загрузочную обёртку `Xg` (=экран `ad`), потом целевой |
| `$d.jI(cls,info)` | L120 | `this.Mr.Taa(cls,this,info)` — пуш нового экрана из текущего |
| `wa.F().mp(id,data,cb)` | L933-934 | **единая точка навигации**: ставит `Td.Tf=id`, `Td.data=data`; для боя (6) зовёт `Ca.Q4a` (=`gameStart`); затем `fLa()` → `Zd.load(id)`; `ghb()` по приходу шлёт квестам `QUEST_EVENT_SCENE_LOADED` |
| `Zd.load(a)` | L1837 | `ad.TGa=a; ad.load()` — глобальный переход (единственный вызыватель `Zd.load` в файле — `wa.fLa`, L934) |
| `ad` (Loader, g="3F4") | L1968-1969 | загрузочный экран с `TGa` (куда идти): `switch(ad.TGa){case 3:jI(Tf);case 4:jI(Oa);case 5:jI(Ya);case 6:jI(ai,{fightList:wa.F().Td.data});case 7:jI(vb)}` |
| `ha.F()` (g="1CD") | L1014-1020 | менеджер квестов/битв-заданий: `Dh[]` очередь, `ta` (Bj) журнал условий, `WO()` регистрирует квесты, `RA(event)` триггерит события (`QUEST_EVENT_FIGHT_END`, `SCENE_LOADED`…) |
| `s` экшены квестов | L1032-1091 | квест-скрипты: `Gn`=Открыть экран, `Sn`=Начать бой, `go`=Открыть магазин, `He`=Диалог/лут, `Tn`=Завершить бой, `qo`=Выбрать на карте |
| `v.kD(...)` | L1213-1215 | **обработка конца боя целиком** (награды, save, переходы) |
| `ca.TYa()` | L424 | в конце битвы в `ca` вызывает `v.JFa` + `v.kD(a,this.Da, …)` |
| `p.F()` (g="5D") | L176-220 | глобальное состояние игры (зоны, бои, предметы, экономика) |
| `p.o` = `xf` (g="6D"?) | L244-285 | сохранённые данные игрока (валюта, прогресс, инвентарь, CurrentZone) |

Нумерация экранов — `xn` (L1167): `0=Preloader, 2=Loader, 3=Dojo, 4=Shop, 5=Map, 6=Fight, 7=Profile, 8=GeneralMenu, 9=Pvp`.

---

## 1. Экраны и флоу

### Карта экранов (классы в JS)

| id | Имя (`xn.iOa`) | Класс JS | Строки | Что показывает |
|---|---|---|---|---|
| 0 | Preloader | `Rg` (g="3F3") | L1967 | заставка+прогресс 0-95% (ассеты 0 + `lang/*.xml` + ассеты Dojo), 95-100% — модули `Ev` |
| 2 | Loader | `ad` (g="3F4") + `Ev`-модули | L1968-1969, L1166 | «Loading N%», потом `ad.load()` → прыжок на целевой экран по `ad.TGa` |
| 3 | Dojo | `Tf` (g="3F5") | L1969-1970 | **домашняя база**: фон дома + бой «Training» (манекен), кнопки Map/Shop/Profile, босс арка отдельно |
| 4 | Shop | `Oa` (g="468") + таббар `ss` | L2286-2290, L2284 | магазин экипировки: табы Weapons/Armor?/Helm?/Ranged/Magic/Enchant/IAP |
| 5 | Map | `Ya` (g="42B") + скроллер `Vr`, ноды `qe` | L2126-2130, L2120-2124 | карта зоны: лента боевых нод на фоне `map/part0..6`, переключение зон |
| 6 | Fight | `ai` (g="3FF", `dJ()=6`) | L2005-2010 | сам бой: VS-интро (`ik`), раунды, HUD, камера |
| 7 | Profile | `vb` (g="445"?) + таббар `cs` | L2189-2198 | профиль/статы/достижения (табы счётчиков `co.uCa/sCa/yi.rCa/vCa`) |
| 8 | GeneralMenu | **класса НЕТ** | L1167 (только enum) | в этом билде не реализован как экран; `jOa("GeneralMenu")→8` существует, но ни один класс не возвращает `dJ()==8` |
| 9 | Pvp | **класса НЕТ** | L1167 (только enum) | то же: `ad.load()` не имеет `case 9`; PVP-бои приходят только как `Td.data` («Duel»-периодика на карте) |

### Как осуществляется переход (шаг за шагом)

1. Кнопка/квест вызывает **`wa.F().mp(screenId, data, callback)`** (L933):
   - `if(!Zd.jZa()||wa.rYa)return!1` — блокировка во время загрузки;
   - `ha.F().ta.nLa=xn.iOa(a)` — журналу квестов сообщают экран;
   - `this.Td.data=b` (для `b!=null` или экранов !=4/7), `Td.Tf=a`, `Td.ge=cb`, `Td.zJa=Td.Tf` (предыдущий);
   - `if(Td.Tf==6) Ca.Q4a(L.K.vFa, fLa)` — **`gameStart(level)`** перед боем, иначе сразу `fLa()`.
2. `fLa()` (L934): `Wb.F().m0a()` (закрыть диалоги) → **`Zd.load(Td.Tf)`**.
3. `Zd.load(a)` (L1837): `ad.TGa=a; ad.load()` — поднимает Loader-экран.
4. `ad.load()` (L1969): `ad.YD=!0; switch(ad.TGa){...}` — **`ma.Jg().jI(КлассЭкрана, info)`** пушит целевой экран поверх.
5. По приходу на экран квесты уведомляются `ha.F().Sf("QUEST_EVENT_SCENE_LOADED")` (в `wa.ghb`, L934) — от этого стартуют сюжетные/туториальные квесты.

Прямых вызовов `wa.mp` (все места): L267 (смена воина/слота → Dojo или `mp(a,0)`), L1032 (`Gn` квест-экшен), L1070 (`Sn` — квест «начни бой», фолбэк `mp(3)`), L1089-1090 (`qo` фокус карты; `go` — `mp(4,new Gj(...))` открыть магазин на предмете), L1042 (`On` — обновить текущий экран `mp(Td.Tf)`), L1213 (`v.qxa` — после поражения `mp(5)`), L1216 (`v.Am` — `mp(6,a)` начать бой), L1981 (Dojo: кнопка сундука `mp(4,new Gj(6,a))`, кнопка Disciple `mp(3)`).

### Dojo (3) — домашняя база

- `Tf.In()` (L1969): `Ca.rna||(Ca.O4a(),Ca.tQ(ta.$D&&ta.ZD))` — **первый вход в Dojo = `gameReady()`** (L34).
- `Tf.init()` (L1970): берёт **текущую зону** `p.V$a().a0("FightNone")[0].g0(0)` — первый тренировочный бой (DUMMY) зоны, `v.i1a(a)` (L1212): если открыт 'Disciple' (`p.o.Y0()`) — вместо него последний бой той же локации `a.lg.Kz()[последний]`; `this.Ig=v.m1a(a)` — создаётся `ca`-контроллер, и **Dojo сам гоняет бой** (`aa()`: `this.Ig!=null&&this.YL(this.Ig,a)`).
- Источник: зона **Punchbag** (stages.xml, `Start="1"`): нода `Training` (Type="DUMMY", Location="dojo"), Fight1 — Воин `Punchbag` (`NotAI=1 NotAnimation=1`, предметы `PunchingBag`/`SkeletonPunchingBag`), Fight2 — `Dojo_Disciple` (Tactic Standard, WeaponDamage=2…). Награды = 0.
- На экране Dojo есть кнопки-переходы (виджет L1980-1982): `d1(3)`=Dojo(тренировка), `d1(4)`=Shop, `d1(5)`=Map, `d1(7)`=Profile; `Nfb()` — переключить Disciple (`p.o.oub(!p.o.Y0())`) и `mp(3)`; `Tfb()` — сундук гемов; `Vfb()` — загрузка IAP 250-253; `xvb()` — диалог выхода (`Xc.Shb()`); плюс Konami-код CHEETAHMAN (`Kwa`) и `BUG` (`bwa`) — L1970: `this.Kwa=[67,72,69,69,84,65,72,77,65,78,159]` (CHEETAHMAN+Enter → `L.K.fi=!0` debug).
- Верхняя панель (деньги/энергия) — виджеты `wr`/`xr`/`yr` (L1968, power-бар `xr.kB()`: `p.o.dk`).

### Туториал (первые запуски)

- Квесты туториала — `quest_extensions/tutorial_quests.xml` (включается из `quests.xml`, `Z.Jna="quests.xml"` L2478, подключение по условию `_$StoryTutorialStep != END`):
  1. `StoryTutorialWelcome` (Priority -10): переменные `NotificationTextMove=tutorial_move`, `NotificationTextPunchBag=tutorial_punchbag`; `<Fight Name="Punchbag|Bosses|1"/>` — **первый бой с манекеном** (зона Punchbag).
  2. `StoryTutorialReturnToFight` — вернуться в бой.
  3. `StoryTutorialShop` (Priority -100) — диалог Sensei (`SenseiDialogText=tutorial_shop`), открытие магазина.
- `users_default.xml` несёт `Tutorial="MOVE"` — ранний флаг шага туториала.
- Квест-экшены выполняются `ha.F().eLa()` (L1015): очередь `Dh`, `b.lF(ta,!1)` запускает бой; по завершении `Mt` (L1016) убирает из очереди, `p.o.save()`.

### Флоу первого запуска целиком

```
index.html → window.GameInterface.init([fflate, imageloader, sf2.js, trace.js]) → SF2.main()
L65(L) boot: Ca.init(); (this.BJ=!Aa.Ue())&&Aa.init()  ← нет сейва → клонирует users_default.xml в SF2User
   → язык из сейва, пресеты для ja/ko/ru
→ mc.K.Taa(Rg) — Preloader (первый экран)
Rg.load() (L1967): ассеты [0, lang/{lang}.xml] + Yv() ассеты Dojo; прогресс 0→95% (Ca.mqb = sendPreloadProgress)
при 95%: Ja.Vxb() (bulk 394-551) + Ev-модули: ap, cp, $o, bp, dp (L1967)
cp (L1166): td.Qdb(); ra.load(); P.zdb(); p.F().Edb()  ← инициализация мира: Wab (типы боёв), items.parse,
   Xjb (сейв), Nyb/ijb (предложения), ukb (Warriors/Templates), Dkb (зоны), L3(Z.Jna)=quests.xml, hbb
dp (L1166): v.owb() (Q1=!0); v.uwb() → ha.F().Sf("QUEST_EVENT_SESSION")  ← первый квест триггерится
→ ad.Loader → ad.load() по TGa → Dojo (3) / далее квесты ведут в бой
```

---

## 2. Карта / зоны

### Данные зон

- **XML зон и воинов**: asset 273 (`Ja.ki(273)`) — `<Zones>`, `<Warriors>`, `<Templates>`, `<WarriorGroups>` (L188-189, L200). В extracted: `stages.xml`.
- **Зоны** (`p.F().uC`, класс `st` L1431): поля `{Dg[] локации, name, fileName (фон карты), yR=Start-флаг, status, index, bL (цифры награды), LK}`; `xQ(name)` локация по имени, `a0(type)` локации по типу боя, `Gx()` пересчёт статуса.
- Парсинг зон — `p.Dkb()` (L188) / `Ckb()` (L189): `<Zone Name= FileName= Start= RewardDigits= PrizeBaseDigits=>` → `st`; дети `<Battle>` → локации `Lc` (L1413)/`Gb` (периодика)/`cl` (реплеи) через `Qib` (L190): атрибуты `X, Y, Name, Alias, Title, Icon (default "training"), IconAtlas, Preview, Description, Location, Music, Type, RewardImage, ShowResistance, Hide, EclipseToggleName`.

### Состав (stages.xml, авторитетно)

| Зона | FileName (фон) | Нод (Battle) | Fights |
|---|---|---|---|
| Punkbag (Start=1!) | — | Training (DUMMY, dojo) + … | 3 |
| ZONE_1 | Map0.1 | BOSS_LYNX, Tournament, Survival, Duel, Stranger, Ambush1400, *_INTERMISSION x4, BOSS_HARDMODE, SENSEI_MEMORIES | 73 |
| ZONE_2 | Map1.2 | … (14 нод) | 84 |
| ZONE_3 | Map1.3 | … (14) | 84 |
| ZONE_4 | Map2.4 | … (14) | 82 |
| ZONE_5 | Map2.5 | … (14) | 82 |
| ZONE_6 | Map2.6 | … (17) | 90 |
| ZONE_7 | Map3.7 | … (21) | 128 |

- **Ноды** — `<Battle Name= Type= X= Y= Location= Music= Icon= Preview=>`; босс-нода `Type="BOSSES"` (например BOSS_LYNX, X=-180 Y=-45, Location="moon", Music="fight10_black_warrior"). Fight внутри ноды: `Name, Power, Rounds(2), RoundTime(99), Replays, Rewards[], Warriors[], Rules[]`.
- **7 картинок карты** `res/map/part0..6` = фоны зон (map0..map6; part0: 2046x854).
- Превью локаций: `map/preview_main.json` — 25 мини-локаций; `map/buttons.json` — арт кнопок нод (`BattleBtnBase/base_<name>`…), `preview_bosses.json`.

### Как открываются / текущая зона

- **`CurrentZone`** — `p.o.` (`xf.ro`, L248: `b=a.attributes.get("CurrentZone"); this.ro=…`); запись — **`xf.stb(a)`** (L274): `this.ro!=a&&(this.ro=a,this.Cr("CurrentZone",this.ro))` (сразу пишет в сейв).
- **Активность ноды** — battle-запись в `p.o.iF` (map `At`), проверка `p.o.WDa(zone|loc)` (L249/«WDa(a){return this.iF.get(a)!=null}»); записи создаются в `p.o.J1a(a)` (L255) при первом входе на ноду / получении.
- **Стартовая зона** — `st.yR` = флаг `Start` в XML (у Punchbag). `p.V$a()` (L220) ищет зону с `yR` = «стартовая/активная». На карте `Ya.HXa` (L2123): зоны с активными локациями показываются как полосы-виджеты (`qe`), **кроме** `yR`-зоны (она и есть текущая карта); `Ya.sY()` (L2122) — спецслучай: если виден только 1 виджет — стилизовать как `Cga` (current) на фоне `Map1.2` (=ZONE_2 фон) — арт-приём ранней карты.
- **Открытие следующей зоны**: после победы над боссом (`FightBosses`... и `FightFinalTitan`) — `p.o.stb(...)`/KZa-достижения (L423-424), `Ya.WEa` (L2130) проверяет активность боссов зоны.
- **MapFocus** — `p.o.ys` (`hb`, L255: `m5()`/`Ttb()` пишет `MapFocus` в сейв); `Ya.bKa()` (L2129) возвращает карту к фокусной зоне: `this.Uw(p.Uk(p.o.ys),0)`.

### Выбор боя

- Тап по ноде: `qe` → `Ya.Uw(loc, …)`/`fqb(zone)` (L2129: `a.lg` зона → `Uw`), скроллер `Vr` (L2120-2121) перелистывает зоны.
- Кнопка «Fight» на ноде → `v.Am(battle)` (L1216): проверка силы (`p.o.yN||v.qZa(-a.d4)`), `a.Wc.Cla(p.Dc)` (старт-таймер ноды), `rb.Wkb()` (snd_gong) → **`wa.F().mp(6,a)`**.
- `ai.init` берёт `this.info.fightList` (=`Td.data`); в `ai.aa` (L2005-2007): kd=0 setup `ca.hCa(...)` (L431: `f.lD/f.uP/f.Y1/f.xR/f.eE` — сколько раундов/противников), kd=1 `v.Yxa(...)` создаёт `ca`, kd=3 `ik` (VS-интро `Xp`, L2006: «ПРОТИВ» + имена) → kd=5 бой (`Za.F().update(); YL(Ig,a)`), мульти-бой — `jk` интро (`lca`, L2006).

---

## 3. Магазин / экипировка

### Экраны магазина

- **`Oa`** (dJ()=4, L2286-2290): вкладки `f5(a)` (L2286-2287): `0→xaa`, `1→u7`, `2→U8`, `3→s$` ("shopRangedLocked"), `4→G9` ("shopMagicLocked"), `5→gC` (зачарования, `m.oD(this.gC, c=>c.type==I.wk…)`), `7→QV` (IAP-офферы). Порядок соответствует слотам: Weapon(0)/Armor(1)/Helm(2)/Ranged(3)/Magic(4)/Enchant(5)/IAP(7). Точные подписи табов лежат в атласе `y.KSa..y.FSa` (иконки), «заблокировано» — строки `shopRangedLocked`/`shopMagicLocked`.
- **Таббар** `ss` (g="466", L2284-2285): `Tw=[0,1,2,3,4]` + 5 если `iap` + 7 если `L.K.Yja` (debug); счётчики бейджей — `p.items.T5a(Cj.zxb(a))`.
- Виджеты предмета: `fi` (карточка атрибута, `A$a()` = расчёт текущего значения: `p.o.bb()*B9+Fk`, L2279), `ms` (список атрибутов), `ps` (слот с вещами, `dfb(a)`→`Ex()`: `gH.hk(type,item)` применяет на пробной копии), `qs` (зачарования), `rs/gi` (список наград-предметов).
- Диалог покупки: `ph` (g="3F2", L1966-1968): `shopBuy`/`shopUpgrade`/`dlgOrder*`, иконки валют `img::`+`p.o.Vf` (монета) / `Z.Ur`="ruby" (гем).

### Каталог и цены (list.xml)

- `<Item Name= Image= Model= Type= SubType= WeaponDamage= Level= Price= UpgradeLevel= …>`; валюты: `Price` (монеты) / `BonusPrice` (гемы), `PaidItem="Paid"`, `ShopHide="1"`, `PackLabel="ZONE_N"`.
- Состав: **Weapon 150, Helm 141, Armor 135, Ranged 66, Magic 43**, + RealMoneyItem 91, Gold 33, Bonus 18, DailyOffer 30, Shuriken/Chakram/Kunai (ranged-подтипы).
- Категории (`vj.E0`, L1168): 1=Weapon, 2=Armor, 3=Helm, 4=Ranged, 5=Magic, 6=Ruby, 7=Free, 8=RaidConsumable, 9=Cheat, 10=Perks, 11=Moves, 12=Achievements, 13=QuestItems, 15=BattlePass, 16=StoryMapStage, 17=RaidMapStage.

### Покупка и применение

- **Проверка цены**: `p.YDa(item, currencyType)` (L210): тип 1/8 — монеты (`p.o.Tb`), 2/9/10/11/18 — гемы (`p.o.fd`), 3/14 — бесплатно; возвращает `el{type,value}`; дешевле проверить `a.xf>e.bb()` (уровень-лока).
- **Купить**: `p.gI(a,c,val)` (L211) → switch по валюте: `p.gab(a)` списать монеты, `p.Vfa(a)` гемы; `p.fab(a,e)` → `p.Ky(a,c,b,d)` (L218): добавить/апнуть предмет в инвентаре `p.o.xa`; `p.Ovb` — стек идемпотентности для одноразовых.
- **Экипировка**: `xc.hk(type,item)` (L817) — вешает предмет в слот (`Of` weapon, `hg` helm, `Lg` armor, `Hd` ranged, `Mg` magic, `Gc` tactic); натив-эквивалент описан в screens.hpp (EquipmentScreen). `p.bo(item,equip)` (L211-212) — `p.o.setItem(item, b)` (флаг `Equipped` в `<Items>`).
- Пересборка бойца при смене вещей — `ca.Tlb/Slb` (L417-418): сравнивает `Ld`-слепки параметров, при изменении `tja()/Qlb()` пересоздаёт бойца.
- **Урон/статы от экипировки**: у воина `xc` (L804-822) статы-атрибуты в `attributes` (ud); урон бойца `wd.bCa` (L513-514): `(a.Xb+f.Ly)*g*b*c*h*f.parameters.UZ` — Xb=Damage воина, `qea/kea` мультипликаторы атаки/защиты от атрибутов, `c2a` — броня (`Fists`→`M_` FistsDamageMod), `attributes` из `<AttributesAlign><Delta Factor Shift Priority>` (L1412-1416), `v.pAa/iea` (L1203-1204) — балансировка силы противника по Power.
- **Power** игрока `p.o.dk` (кап `wr=v.$Ca()`), таймер `PowerSyncTime`; бой недоступен если `qZa(-a.d4)<0` (сила ниже требования ноды).

---

## 4. Сейв

### Ключи хранилища

- **`SF2User`** (`Aa.WU`, L70-73): главный сейв — **сериализованный XML-документ**, zstd (to `kb.f3(a,qf.aG)`) + base64 (`ri.encode`), через `Ca.R1a(name)` = `Ck` (L36-37: `window.GameInterface.storage.getItem/setItem`) — реально localStorage `famobi:savegame` (microsite-game-interface.js).
- **`SF2Packs`** (`Aa.Y6`): пакеты/обновления (XML `<Packs>`).
- **`SF2Flags`** (`cg.P6`, L68-69): флаги JSON (`H1/VF/zK/qQ`).
- Импорт/экспорт `.sf2` — `Aa.Ddb()/Dpb()` (L72-73): файл = `"SF2"+base64[users+packs+flags]`.
- `Aa.init()` (L73): клонирует assets xml id 9 (**users_default.xml**) в SF2User + версия Haxe.

### Формат `<Root>` (users_default.xml, res/):

```xml
<Root>
  <CurrentUser ID="1"><Sounds><Sound Value="1.0" Mute="0"/>…</Sounds></CurrentUser>
  <Warriors>
    <Warrior ID="1" FirstName="NAME_SHADOW" Avatar="avatar_hero" Voice="Male" Money="0" Bonus="50"
      Strength="3" Stamina="3" Level="1" Experience="0" Power="5" PowerSyncTime="…" Difficulty="50"
      Skeleton="Skeleton" Armor="Body" Helm="Head" Weapon="Fists" Ranged="NoRanged" Magic="NoMagic"
      ShowUpgrades="0" Tutorial="MOVE" Tactic="Player" CurrentZone="ZONE_1" SaveSlot="0">
      <Items><Item Name="Body" Equipped="1" Count="1"/>Head/Fists/NoRanged/NoMagic</Items>
      <Battles><Battle Name="ZONE_1|BOSS_LYNX|"/></Battles>
      <Currencies/><Resistances Resistance_2="0"/>
    </Warrior>
  </Warriors>
  <Versions><Version Value="1.0.13"/><DataVersion Value="1.0.13.0"/></Versions>
</Root>
```

### Что парсит `xf` (user data, L244-260) из `<Warrior>`

- `Money`→`Tb` (монеты), `Bonus`→`fd` (гемы), `PaidMoney/PaidBonus`, `DenominationDigits`;
- `Level` (Ca.level), `Experience` (rs), `Power` (dk), `PowerSyncTime`;
- **`CurrentZone`→`ro`** (L248; запись `stb` L274), `MapMaskColor`;
- `Items`→`xa` (инвентарь `$g`, L244-245: `parse(h2, Ca.level)`, `Unlimited_Energy`);
- `Battles`→`iF` (прогресс нод + `lWa(new hl)`), `Fights`→`yc` (`il` записи боёв, `Sq(zone|loc)` поиск, `no` = число побед), `Shop` (локи `vq`), `Quests` (+`Variables`), `ActiveLotteries`, `Timers`→`yl`, `Currencies`, `Payments`, `Advertising/Resistances`, `Offers`;
- Сохранение любой записи: `xf.Cr(a,b)`/`nF/hL` (L157: `this.ga.set(a,b); this.save()`).

### Загрузка при запуске

- В `L` boot (L65): `(this.BJ=!Aa.Ue())&&Aa.init()` — нет сейва → создание из дефолта.
- `p.EdB()` → `Xjb()` (L187-188): `this.EB=Aa.load()`; `CurrentUser`, `Warriors`, `GameLaunchIndex++`, `LastSaveSlot/SaveSlot/ID/IsFake` (мультислот), `p.TJ=new sc(...)` (звуки), `p.o=this.Qjb(warrior)` → `new xf(warrior, ur(...))` (L204); `p.o.Ca.Fm()`, `p.o.save()`.
- Сохранения в рантайме: `p.o.save()` вызывается после `v.kD` (L1218), `p.dmb` (L234-context), `ha.Yba` (L1019), `Aa.flags.save()`, `p.o.Cr` (любая запись) и т.д. `p.Gx()` (L184) — пересчёт зон после изменений.

---

## 5. Боевой флоу

### Вход в бой (повторно)

`v.Am(battle, ...)` (L1216): `L.K.vFa=a.Nb.Wn()` (level для analytics), условие `p.o.yN||v.qZa(-a.d4)` (заблокировано?), `a.Wc.Cla(p.Dc)`, `rb.Wkb()` → **`wa.F().mp(6,a)`** → Loader → `ai` → `ik` VS-интро → `ca`.

### Конец боя — цепочка (важно!)

1. `ca.Onb()` (L411) — проверка конца раунда (таймаут/KO/ринг-аут); `E3a(...)` (L412-413) применяет результат раунда: `a.ng++` (победы), `zd/Iq/kh` флаги на параметрах, `ze` (Zt) трекер.
2. `ca.bea(a)` (L413-414): `this.Da.PU=Rk` (текущий бой); `a.Fj?Ca.bea(gameOver):Ca.K4a(gameComplete)` (L34-35) — **GameInterface gameOver/gameComplete**.
3. **`ca.TYa()`** (L424) — финальный обработчик битвы: `fe.complete(eL,!0)`, снапшоты HP, `v.JFa(...)` + **`v.kD(a, this.Da, …)`**.
4. **`v.kD(a,b,c,d,e,f,g,h)`** (L1213-1215):
   - `f=new hp` — результат: `f.Da=b` (битва), `f.Mf=zone|loc`, `f.nB=c` (параметры игрока), `f.pwa(b.D0(c),a,b)` — разбор результата; `f.zd()` = победа;
   - `b.lg.hMa(b, f.zd())` — запись победы/поражения в progress-запись ноды; `b.type=="FightPeriodic"&&…v.n1a(b,f.zd())` — таймеры периодики;
   - **`c=p.F().dmb(f)`** (L185-186): применяет награды из `f.hj` (`sw`): `p.Iab(exp)` (опыт → уровень `xf.Jab/OLa` L252-253), `p.Fwa(b)` (`Tb+=монеты` L220), `p.Ewa(c,3)` (`fd+=гемы`), `p.o.Hua(currency)` (`Currencies`), `p.o.Pw` (Resistances), предметы → `p.Ky(item,1)` в инвентарь (L218); `p.o.save()`; возвращает `c`=есть ли вещи;
   - журнал квестов: `a.Nb=b.Nb; a.Qv=f.zd()?"Win":l?"Surrender":"Loss"; a.t2=c?1:0`;
   - `g=b.wi…` — проверка дополнительных наград подбоев (`bm(n.bb()).Yj!=null`) → `ha.F().Flb=a`;
   - квестовые события: `ha.F().RA("QUEST_EVENT_FIGHT_END")` (или RAID) → сюжетные квесты реагируют; `p.o.save()`;
   - **навигация**: `k?p.$wa():b.type!="FightRaid"&&(l?v.qxa(f):(b=ca.Ka(),b!=null&&b.zia(f)))`
     - победа без продолжения → `p.$wa()` (L221: авто-обработка доступных реплейных битв `rV:$fb` — реплеи/`Replayable` бои);
     - **поражение → `v.qxa(f)`** (L1213): `a.type!="FightPVP"&&wa.F().mp(5)` — **возврат на Карту**;
     - иначе `ca.zia(f)` — следующий противник/раунд/подбой.
5. Межуровневые босс-сцены: `v.kD(new Fh, a, p.o.Ca, null, 1)` (L1216) — авто-завершение (используется квестом для реплей-сцен без боя); лут-нода Stranger/Ambush1400 (HIDDEN) — `p.dmb` выдача предмета.
6. `v.S6a` (L1214) — подсказка «ещё немного до уровня»; `p.o.OLa` (L252) — анимация левел-апа: `xa.vu()`, `xa.Ryb(bb())` — новые предметы на уровень, `Oa.get().Lma()`, `p.F().Yyb(bb())`.

### Поражение / реванш

- Поражение: `flb` (пре-энд) `Qv="Loss"`, `kD` → `qxa` → **Map**; нода остаётся (progress-запись хранит поражения, реплеи `Replays`/`ReplayInterval`/`DisableRetry` из `IIa`, L205-206); босс не даёт награду повторно (`b.lg.hMa`).
- Реванш возможен если `Replays` не исчерпан; для `FightReplayable*/FightBossesReplayable` — `cl` (L1413-1415): `ReplayCount`, интервалы, `$fb()` реплей-цикл.
- Награды за подбой (`b.wi`) — `hp.pwa(D0(c))` → `sw`; при победе с невыданными наградами — `ha.F().Flb=a` (лут-экран).

---

## 6. Начало игры

### Первый запуск

- Нет сейва (`!Aa.Ue()`) → **`Aa.init()`** клонирует `users_default.xml` (xml asset 9) → в `SF2User`; `L.BJ`=true (флаг нового игрока; в Dojo грузятся доп. ассеты 314/1354/196/1317, L1969).
- Дефолт: Level 1, Power 5, 0 монет, **50 гемов**, экипировка Body/Head/Fists, `Tutorial="MOVE"`, **`CurrentZone="ZONE_1"`**, battle-запись `ZONE_1|BOSS_LYNX` (открыт босс-нод Линкса).
- Квесты-туториал (см. §1) заводят в тренировку Punchbag, диалоги Sensei, магазин.

### Загрузка ресурсов (Preloader → Loader)

- `Rg` (Preloader): 0-95% — ассет 0 + `lang/{lang}.xml` + ассеты Dojo (`Tf.Yv()` L1969: 753/754/755 или 315/1355/10-11/1317/1316/248-249/338-339/829-830/266-267/262-263/268-269/1306-1307/260-261/244-245/264-265/254-255/12-13); `Ca.mqb(a)` — `sendPreloadProgress` во внешний интерфейс.
- 95-100%: `Ja.Vxb()` (bulk-загрузка 394-551) + `Ev`-модули `ap,cp,$o,bp,dp` (`cp`: `td.Qdb();ra.load();P.zdb();p.F().Edb();id.ht().bya();Cc.F()`; `dp`: `v.owb();v.uwb()` → `QUEST_EVENT_SESSION`).
- `ad` (Loader): текст «Loading N%», по готовности `ad.load()` → целевой экран (TGa).

### Отображение (canvas, aspect)

- `index.html`: WebGL2 обязателен; инициализация `GameInterface.init([fflate, imageloader, sf2.js, trace.js]).then(SF2.main)`; canvas WebGL2 + `stencil`, </br> запрет contextmenu/dragstart.
- Окно/резол: `Kk` (window update в `Pg.xeb` L56), `N.lc` (aspect), `N.rect`, `ma.Kq` — проекционная математика (L1836: `ma.Kq.N=N.width; …`); `Kk.update()` пересобирает canvas при resize; игра рендерит в фиксированный 60Hz шаг (`Us`, L135: `Bm=.0166667`), рендер по rAF с alpha-блендом (`Pg.Ea`).
- Управление: pointer/touch (`Jk`, L2429-2437) — тап по земле = движение (`ma.Bd`, L1836); кнопки HUD атака/блок/магия (`ca.cka/U4/Gqb`).

---

## ЧТО У НАС НЕ ТАК (расхождения натива с оригиналом)

### 1. Экраны и флоу
- Натив грузится в **GeneralMenu (8)** как главное меню (`app.cpp:381 push kScreenGeneralMenu`) — в оригинале **хабом является Dojo (3)**, а GeneralMenu-экрана в этом билде вообще нет (только enum L1167).
- Нет Preloader/Loader (`kScreenPreloader` есть, загрузчика нет): натив не эмулирует `Rg→Ev→ad.load()` и `sendPreloadProgress`.
- Нет PvP-экрана (9) — совпадает (в JS его тоже нет), но «Duel»/периодические бои идут через `Td.data` → `wa.mp(6, data)`; натив так не умеет (нет `wa`-эквивалента: `mp/Td/ghb/QUEST_EVENT_SCENE_LOADED`).
- Квестовое управление навигацией отсутствует: в JS экран переключают квест-экшены `Gn`/`Sn`/`go` (L1032/1070/1090); натив — прямое `Screen::push`.
- Dojo в нативе нет (нет `Tf`, тренировки «Punchbag», «Disciple», Konami-кода, `gameReady()`).

### 2. Карта/зоны
- Натив рисует ноды из `stages.xml` как плоские кнопки (`MapScreen.Node{name,type,x,y,active:true}`) — нет: скроллера зон `Vr`, виджетов зон `qe`, `yR`/Start-зоны (Punchbag), `CurrentZone`(stb/ro), `MapFocus`, `WDa`-разблокировки нод, босс-статусов `Ya.WEa`, спецслучая `Map1.2`.
- Все ноды `active=true` по умолчанию — оригинал открывает только по battle-записям в `p.o.iF`.
- Нет `Type`-семантики нод (Boss/Periodic/Hidden/Intermission/Replayable) и их таймеров (`Gb/ cl`).

### 3. Магазин/экипировка
- Натив: flat-карточки `list.xml` Weapon/Armor/Helm только за монеты (`screens.hpp` ShopScreen: `p.o.Tb >= a.jp()`, `gI`). Оригинал шире: 5-6 вкладок (Ranged/Magic/Enchant/IAP), гемы (`BonusPrice`), OneTime/`PaidItem`, локи вкладок, бусты, зачарования `qs`, «диалог покупки» `ph` с таймерами доставки/апгрейда.
- Нет пересборки бойца при смене вещей по слепкам `Ld` (`ca.Tlb/Slb` → `tja/Qlb`) — натив просто меняет слот и пересобирает модель.
- Статовая модель упрощена: нет XP-кривой уровней `OLa/$B`, Power/капа `wr`, атрибутов `<AttributesAlign>`, формула урона `bCa` свёрнута (натив считает от WeaponDamage напрямую — «не проверено» на соответствие `(Xb+Ly)*...*UZ`).

### 4. Сейв
- Натив хранит trimmed-проекцию `WarriorSave` (деньги/опыт/уровень/предметы) отдельным XML — JS хранит **полный документ**: `Battles` (прогресс нод), `Fights` (победы), `Quests` переменные, `Timers`, `Curencies`, `Resistances`, `Offers`, `Paymerts`, `Lotteries`, `CurrentZone`, `FighIDS`, `MapFocus`, `GameLaunchIndex`, мультислоты.
- Нет переключения слотов (`LastSaveSlot/SaveSlot/IsFake`), нет импорта/экспорта `.sf2`, нет `SF2Packs`/`SF2Flags`; нет zstd+base64 (натив — plain file).

### 5. Боевой флоу
- Натив ResultsScreen: по клику возврат на карту. Оригинал: победа → `p.$wa()` (авто-реплеи + следующий этап), поражение → `v.qxa`→`wa.mp(5)`; босс-сценарии через `v.kD(new Fh,...)`; лут-подбои `b.wi`/`Flb`.
- Нет награждения XP-кривой (level-up `OLa` + выдача предметов уровня), нет periodic/replay-таймеров наград, нет `gameStart/gameComplete/gameOver` вызовов (`Ca.Q4a/K4a/bea`).
- Нет round-достижений `KZa/JZa` и `QUEST_EVENT_FIGHT_PRE_END/END` триггеров — сюжетные квесты натива не дышат.

### 6. Начало игры
- Натив копирует users_default.xml лучшим образом (save_system.hpp), НО: нет `Aa.init()`-семантики версий `Versions/HaxeVersion/DataVersion`, нет флага нового игрока `L.BJ` (влияет на подгрузку ассетов Dojo), нет `gameReady()` на входе в Dojo.
- Нет туториала: квесты `tutorial_quests.xml` (`StoryTutorialWelcome` → бой `Punchbag|Bosses|1`, `StoryTutorialShop`) не загружаются (`p.L3(Z.Jna)` + `ha.WO`); нет `Tutorial="MOVE"`-шага.
- Preloader/WebGL2-цепочка иная (натив сам создаёт окно/канвас) — «не проверено» полное соответствие aspect/ретины (JS: `Kk`/`ma.Kq`).

---

### Сводка ключевых строк JS (быстрый доступ)
L65 boot/`Aa.init` · L70-73 `Aa` save · L1166 loader-cp/dp · L1167 `xn` enum · L1168 `vj` категории · L119-127 `$d`/`mc` · L176-220 `p` · L185 `dmb/emb` · L187-189 `Xjb/Dkb/Ckb` · L205-206 `IIa` · L218 `Ky` · L220 `V$a/Fwa/Ewa` · L244-260 `xf` · L274 `stb` · L933-934 `wa.mp/fLa/ghb` · L1014-1020 `ha` · L1032-1091 квест-экшены · L1213-1216 `v.kD/Am/qxa` · L1239 `ov` · L1413-1415 `Lc/Gb/cl` · L1431 `st` · L1837 `Zd.load`, `ma` · L1967 `Rg` · L1968-1969 `ad`/`Tf` · L2005-2010 `ai` · L2121-2130 `Vr`/`Ya` · L2189-2198 `vb`/`cs` · L2284-2290 `ss`/`Oa` · L2478 `Z.Jna/Ur/Hna` · L2479 `v.su`.