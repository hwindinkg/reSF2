# JS_RENDER — Спецификация рендер-пайплайна Shadow Fight 2 (web)

**Источник (единственный):** `reference/www/sf2.502f0946.js` (2531 длинных строк, Haxe 4.3.7 → JS, классы с манглед-именами). Номера строк — 1-based против этого файла.
**Натив для сверки:** `core/render/*` (gl, sprite_batch, texture_gpu), `core/scene/location_scene.cpp` (слои + параллакс), `core/scene/model.cpp` (кости/треугольники/капсулы), `core/app/screens.cpp` (fight-screen render).
**Важно:** JS_POSE_PIPELINE.md / GENEAL_GA_MAP.md НЕ СУЩЕСТВУЮТ — всё выведено из JS ниже.

---

## 0. Два бэкенда рендеринга

В `Pg.init` (L49-50) выбирается бэкенд по `options.ZH` (Ws ZH=2 по умолчанию, L134):
- `ZH==2` → WebGL: `window.ubb(...)`, `Ha = new Id` (L1808, класс WebGL-рендерера).
- `ZH==1` (или null) → Canvas2D: `window.Sab(...)`, `Ha = new Lk` (L1522, 2D-canvas).

Canvas2D-рендереры: `vq/wq/xq/yq/zq/Aq/Td/Bq/Cq/Dq/Td($e)/Ph/Eq` (L1542-1584) — регистрируются в `Lk.ama()` (L1523-1524) по типу эффекта (поле `Xd`).
WebGL-рендереры (аналоги): `Qq(Sprite)/Sq(шрифт)/Uq/Wq(rect)/Zq(Mh)/Xq(текст)/ar(частицы)/Xh(path)/Qk(circle)/b k(tile)/hr(tilemap)/fr(le)/Pk(Yi-меш)/Ok(Xi-сегмент)/jtTj-3D-формы` (L1725-1792).
Маппинг эффект-клass → рендерер (оба бэкенда):

| Эффект (клass) | Canvas2D (`Lk`) | WebGL (`Id`) | Что рисует |
|---|---|---|---|
| `Qh` — изображение из атласа | `vq` L1542 | `Qq` L1727 | квады с поворотом/цветом, стencil-клип |
| `Qj` — сетка шрифта | `wq` L1545 | `Sq` L1737 | битмап-шрифт (fnt/tps) |
| `xg` — плоский цветной rect | `xq` L1548 | `Wq` L1745 / `Uq` L1741 ($e) | заливка recta |
| `Rj` — текст | `yq` L1549 | `Xq` L1747 | текст через path-рендерер |
| `Mh` — rect-изображениt (letterbox/fade) | `zq` L1549 | `Zq` L1749 | растянутый белый квад с альфой/цветом |
| `Ah` — частицы | `Aq` L1553 | `ar` L1753 | instanced квады: rotation/scale/цвет-градиент |
| `ke` — векторный path | `Td` L1555 | `Xh` L1757 | Path fill/stroke (опкоды 1-19) |
| `Gq` — path-расширение | `Bq` L1558 | — | rect/roundRect/arc/bezier/lineCap/dash |
| `Yi` — **триугольный мessh бойца** | (через `Ph`? нет — см. ниже) | `Pk` L1746 | плоские треугольники, цвет uniform |
| `Xi` — **капсула/толстый сегмент** | (через `Ph`?) | `Ok` L1734 | толстые сегменты с круглыми концами |
| `Yj` — круг из атласа | — | `Qk` L1761 | текстурированный диск (ударные точки) |
| `le` — tile/9-slice | (`Dq` L1560) | `b k` L1765 | тайлинг/стetch ±анимация |
| `Tj` — 3D-формы ($e узek) | `Ph` L1570 | `jtTj` L1788 | точки/триугольники/нормали/капсулы-линии |
| `Sj` — tilemap | — | `hr` L1783 | сетка из data-грида |

**Ключ: `Yi` (меш Fk) и `Xi` (сегменты Dk) на Canvas2D не имеют прямого обработчика** в списке `ama()` — на 2D-бэкенде боец рисуется через `$e`/`Tj` (`Ph`): тот же буфер вершин, но через Path2D fill. На WebGL (дефолт) — `Pk`/`Ok` (GL_TRIANGLES, `u_zndc`).

---

## 1. Сцена-граф: `Db`-узлы и проходы `oja`/`nja`

### 1.1 `Db` (g="1", L27-30)
```
class Db { active, J6, Yg, parent, af (firstChild), Ma (nextSibling), sfb, rfb, time, name }
oja(a){ if(!Yg && active!=0){ J6=true; aa(a); for(b=af; c=b.Ma; ...) b.sfb||b.Yg||b.oja(a); time+=a } }   // L29 — UPDATE
nja(a){ if(!Yg && J6!=0 && active!=0){ Ea(a); for(b=af; ...) b.rfb||b.Yg==null||b.nja(a) } }             // L29 — RENDER
```
- `active` (X(a)) — глобальный вкл/выкл ветки; `sfb/rfb` — скип-апдейт/скип-рендер одного узла; `J6` — «обновлён в этом кадре» (гард для nja).
- Порядок: **pre-order, дети по списку `af→Ma`** (первый ребёнок = рисуется первым = снизу).
- `O` (g="2", L30-31) — display-object: `Db` + `node=new Ea` (2D-узел). `C(x)/D(y)/la(scale)` проксируют в node. При `appendChild` 2D-дерево зеркалит 3D-дерево: `a instanceof O && a.node.nJ()==null && this.node.appendChild(a.node)` (L31).
- `dd` (g="9", L37-40) — **3D-контейнер** (НЕ Db): `children[]`, `Zm[]` (компоненты-контроллеры), `node=new Hd`. `xma()` (L38) = update: `for Zm: update(); for children: xma()` — **именно им обновляются Fk/Ek/Dk/ni**. `setActive(a){ node.$m = a?2:1 }` (L39).

### 1.2 Пери-фреймовый драйвер (L56-57)
```
xeb(a)  (L56): window.update(); plb.aa(dt); inputs Y3(); фикс 60Гц: Us.Gy+=dt*NL; while(Gy>=Bm){ aa(Bm); Gy-=Bm }
aa(a)   (L57): inputs state.update; root.oja(a) (сцена); Oh.notify()
Ea(a)   (L57): if(Ha.vp()){ mha&&AFa(); Ha.clear(); root.nja(a); mha&&rJa() } Ha.aQ()
```
- `Us` (L135): `Bm = 0.0166667` — фикстированный шаг 1/60; рендер — 1 раз на rAF с альфой `Gy/Bm`.
- `AFa()` (L57-58): **леттербокс-ограничение аспекта**: `a=window.w9a() (aspect), b=window.L6 (cssW), c=window.K6 (cssH)`; `a>2.5` → вертикальные полосы `KJa(b,0,b+a,1)`; `a<.4` → горизонтальные. Порог 2.5 = `N.sTa` (L2462).

### 1.3 Рендер-дерево боя (fight scene graph)
Строится в `ca` (g="AC", L379-382): `ca.go = new dd("Fight:...")`; `Ta = new ql(this.go); Ta.init(this.location)` (L382).
```
ca.go (dd "Fight") ─ node Hd
 └─ ql.go (dd "Camera", L368 V1a)
     └─ Ut.go (dd "Render", L823)
         ├─ Qi-слои локации (Bf.Ct): sky..glow (z = 0, -3, -6, …)   [L832 UWa]
         │   └─ (только для hn-слоя Type=2): tl.go RenderContainer
         │        ├─ ev.qh.go — бойцы: Rw(z=-.001), pF(z=0)          [L845 Gf]
         │        │   ├─ fighter.model.go (dd "Model") = Mesh(Fk) + ModelCapsules(Ek) + оружие/эффекты (bv)
         │        │   └─ ...
         │        ├─ Xm Gq (ground effects, z=+.01)                  [L843 Zab]
         │        ├─ Xm Hq (air effects,   z=+.01)
         │        └─ Cu (Qi камера-UI, z = last.ldb-3) — vB-видоискатель [L832]
         └─ (layers appended после)
```
- `tl.init` (L842-843): `go.node.Jb.translate = (-width/2, height/2 - ct)` — **мировая система координат: (0,0) = центр ширины арены, y=0 у верхней кромки, y растёт ВНИЗ**; `a.hn.go.nd(this.go)` — RenderContainer цепляется в hn-слой (Type=2), поэтому **масштаб Bj применяется к слою hn → бойцы наследуют zoom через родителя**.
- `UWa` (L832): `for Ct: this.go.nd(layer.go)` (все слои в Ut.go), `Cu = new Qi(Ct[last].ldb - 3)` (UI-слой позади всех), `FOa()` — Cu копирует scale/translate от hn.
- Обновление/рендер боя — в `ma.Sya` (L1833): `a.go.xma()` (компоненты Fk/Ek/Dk/ni), `f.tg()` (матрицы), `f.Fx()` (сбор render-стейтов), `L.K.Ha.Ea(f,b)` (рендер с камерой b=N.Ta).

### 1.4 Натив
`core/scene/location_scene.cpp` строит те же слои (`Layer{factor,type,sprites}`, `arena_width/height/floor`, `root_color`), **s ModelsViewer (Type=2) — пропущен** (коммент в location_scene.hpp L9-10); бойцы рендерятся отдельным проходом в `core/app/screens.cpp` (L873-1030). Есть `render_node` (scene/renderer.cpp L136-141).

**ЧТО У НАС НЕ ТАК:**
1. Натив рисует фон (ВСЕ слои incl. arena-ground/dust/glow/black-bars) ПЕРВЫМ, затем бойцов поверх (screens.cpp L874-918 → бойцы). Оригинал вставляет бойцов ВНУТРЬ слоя Type=2 (между fog Factor=0.9 и слоями Factor=1: arena-стороны/пол/пыль/чёрные бары, затем glow) — **в оригинале пол, пыль, чёрные бары и glow рисуются ПОВЕРХ бойцов** (XML-порядок слоёв), в нативе — под ними.
2. Нет z-стеков: оригинал имеет `ED(a) = -.001*translate.z` (L1488) и z-сдвиги слоёв (-3/слой, -0.01 на спрайт внутри слоя, эффекты +.01, бойцы -.001/0) — натив всё плоско рисует через `Camera.zoom`.
3. Нет аналога `Fx()` (сбор стейтов) и сортировки по типам — натив рисует в порядке вызовов.

---

## 2. Камера — ТОЧНО

### 2.1 Рендер-камера `Vg` (g="18", L76-83) — это `N.Ta`
- `ba(w,h)` (L77): проекция `j4`: `m11=2/w, m22=-2/h, m14=-1, m24=1` (NDC, y-flipped).
- `uA()` (L78-83): view = translate(-pos)×scale(zoom)×rotate×...; `HA = j4×WF` (fzb L74); инверсия `QDa` для screen→world (`dJa` L78: `a=-1+2*(x-c.x)/c.w; b=-1+2*(c.y-y)/c.v`).
- `C(x)/D(y)` — позиция; `tMa(z)` — zoom; `Dwa()` — origin=центр вьюпорта; `K4()` — сброс pos/rot/zoom=1; `kT()` — сброс origin.
- `N.aa` (L84-85): `N.rect = clip-rect; N.width/height/lc; N.Ta.K4();kT();ba(N.width,N.height)`. `N.kn(0)` = screen→world.
- Canvas2D: `Lk.kPa()` (L1536) строит базовую матрицу `Bl`: `m11 = w*(c-a)/2, m22 = -h*(d-b)/2, m14=w/2, m24=h/2` — **y-flip и центрирование**; далее `ji(a) = Bl × Yl.HA × a.dm` (L1532) — Yl.HA = камера (`Ha.Yl` = активная камера).

### 2.2 Контроллер камеры боя `ql` (g="A7", L362-371)
- `tyb()` (L363): **target = середина бойцов**: `Du.ma = wd.mea(Rw, pF)` (Rw/pF = два fighter-обёртки из ev.Gf).
- `dZa()` (L363-365): **сглаживание**: `$X = By-DO` (делta target), `bY = iq+$X`, `aY = bY-Jl`, `IO = By-Jl`; `LO = aY+IO`, `IO*=.15`; `|LO|>200` → нормализовать ×200 (**клamp скорости 200 у.е./с**); `Jl += LO`; `VG = Jl-iq; |VG|>50` → клamp 50 (**клamp делты/кадр 50**); `Go.XA(Jl)`.
- `c3a()` (L366): `ia.Al(Go.ma, Du.ma, h$.ma, i$.ma, IJ?Bf.currentScale:0)` — **h$/i$ = позиции узлов NPivot бойцов** (`a.oa.Ic(v.LC.sba)`, `sba = "NPivot"` из CameraSettings, L1180-1181: `Ov{sba, maxWidth, oGa=MaxWidthDelta, Vva}`).
- `d3a()` (L366-367): тряска экрана через `hw` (траектория) → `ia.Byb(x,y)` — смещение RenderContainer.
- `f3a()` (L367): линза-зум `Bf` (currentScale двигается от `DS=ia.xCa()` к `nM≥1`).

### 2.3 `Ut.Al` (L826-827) — параллакс/зум слоёв
```
Al(a,b,c,d,e){                      // a = focus (Go.ma)
  mwa();                            // L823-824: Ira=N.height/Lb.height; BK=(N.width-sTa*N.height)/2 (полосы!); nC=b/Ira; NW=nC/Lb.width
  Io = Lb.width/2 - a.x;            // ← панорама = центр арены − фокус
  Bj = e>0 ? e : xCa();             // xCa() = min(nC/(ECa+300), 1)   ← зум-аут чтобы влезли бойцы+300px
  // клamp min: a = LC.maxWidth>0 ? maxWidth/w : 1; a = NW/a; Bj<a -> zoom-out + доводка фокуса через kJa() с Xia/X3
  Bj = max(Bj, NW);                 // не меньше "вся ширина" (Kga → точно NW)
  d = (Lb.width-LC.oGa)*Bj*.5 - nC*.5;   Io = clamp(Io, -d, d);       // ← клamp панорамы (не видно краёв арены)
  if($z(Xia,0)||$z(X3,0)) Io*=Bj;
  for layer in Lb.Ct:               // ← ВСЕ слои локации
    lEa()||ij ? layer.setScale(Bj)      // Type=2 ИЛИ Scaling=1 → масштабируются zoom'ом
              : layer.Xrb(F9*(1-Bj));   // иначе вертикальный сдвиг F9*(1-zoom)
    layer.Wrb(Io * layer.bp);           // ← ПАРАЛЛАКС: translate.x = Io × Factor
  kyb(vB, Io-(Lb.width/2-c.x)*Bj, Lb.hn.translate.y + 2*F9*Bj + 10);  // видоискатель
}
kJa(a,b,c){ d=LC.Vva; return |b|+d>a ? ∓(|b|-a+d)*c : 0 }   // плавная доводка
F9 = (Lb.height/2 - Lb.ct)/2                               // L823 init
```
- `Qi.setScale/Wrb/Xrb` (L488-489): ставят `go.node.Jb.scale.x=y=а` / `translate.x` / `translate.y`.
- **Пороговые числа**: скорость 200, дельта 50, маржа 300, маржа 100 (в Sya), Vva из XML, F9=(h/2−floor)/2.

### 2.4 `ma.Sya` (L1833-1835) — финальная матрица боя
```
b = N.Ta; b.Dwa();                                   // сброс origin
d = a.Ta.ia.Rf.qh.ECa()  = |Rw.Eu.x - pF.Eu.x|        // L845-846 — разброс бойцов по X
e = a.Ta.ia.m$a()        = Lb.height * Bj             // L823 — видимая высота мира
f = N.height / e;                                     // ← базовый zoom: высота арены = вся высота экрана
f *= (c<.45?.45:c>1?1:c);                             // аспект-клamp (портрет → меньше)
f *= .8 + ((clamp(c,.5,.8)-.5)/.3)*.2  (если c<.8);   // доп. клamp узкого экрана
f *= min(N.width/(d*f+100), 1);                       // ← фит по ширине: бойцы + 100px
d = .6 + ((clamp(c,.5,1)-.5)/.5)*.7;  f<d → f=d (иначе C(0));  // миним. zoom .6..1.3
b.tMa(f);                                             // zoom камеры
c<1 && b.D(round((N.height-e*f)/2)/f*.5);             // портрет: вертикальный сдвиг вниз
// Kq: видимый мир (для HUD-раскладки): ma.Kq = {N: N.width, P: y(world top→screen), W: e}
L.K.Ha.Ea(a.go.node, b);                              // рендер боя
// затем — леттербокс-полосы через ma.YY[0/1] (черные rect-эффекты) если N.BK>0 / c>0 / e>0
```
- `ma.Kq` (L1833-1834): `N=N.width; P=c (screen-y мира top); W=e (=Lb.height*Bj)`. Потребляется HUD: `Cr.layout` (L2027 `a.F5a()`), `lk.ia` и т.п.

### 2.5 Фон/слои локации — порядок из XML (реал: arena_params)
Референс `reference/www/res/locations/arena/arena_params.16ca56d9.xml`:
```
Root Width=1936 Wall=210 Pages=2 Height=512 Floor=80 Color=0x190702
Layer Type=1 Scaling=1 Factor=0     ← небо _0031_background (1936x512)
Layer Type=1 Scaling=1 Factor=0.05  ← _0030_mountains
Layer Type=1 Scaling=1 Factor=0.1   ← _0029_houses_back
Layer Type=1 Scaling=1 Factor=0.4   ← _0026_back_banners (SimpleEffect OscillationY, Period .5) + _0027_people_back
Layer Type=1 Scaling=1 Factor=0.5   ← _0024_middle_banners, _0023_middle_people_additional, …
Layer Type=1 Scaling=1 Factor=0.6   ← _0019_People_front_1.._0014_People_front_6
Layer Type=1 Scaling=1 Factor=0.75  ← _0012_pavilion
Layer Type=1 Scaling=1 Factor=0.7   ← _0013_rock, _0011_fence
Layer Type=1 Scaling=1 Factor=0.8   ← _0010_bamboo_left, _0009_bamboo_right
Layer Type=1 Scaling=1 Factor=0.9   ← _0008_fog (1936x386)
Layer Type=2  Factor=1              ← ModelsViewer PlayerPositionX=868 Y=-94 / EnemyX=1068 Y=-94  (боевой слой, hn)
Layer Type=1 Scaling=1 Factor=1     ← _0006_Arena_right(+Flip), _0004_Arena_left, dust×6 (SimpleEffect ReappearX±1225 Speed X=1.8), _0007_arena (пол, 1936x145 Y=179), pixel_1 чёрные бары (2000x200 Y=351 и Y=-356)
Layer Type=1 Scaling=1 Factor=0.1   ← _0000_glow (поверх всего)
```
- Парсинг: `Bf.init` (L474-475) читает `{loc}_params.xml`, `Pages`-атласы `locations/{name}/{name}[-N].png`, `NU=Wall`, `ct=Floor`, `Tza=PositionY`, `N2=Color`, `width/height`; `zjb(child, c)` с `c+=-3` (z слоя).
- `zjb` (L475-482): `Qi{ldb=z, ij=Scaling>0, bp=Factor, type=Type, re=Atlas}`; дети: `ModelsViewer` → spawn-точки (Yia/B_); `ParticleEffect/NewParticleEffect` → `QIa` (jh-частицы); `Image` → `ujb` (атлас-спрайт, `pixel_1` → `Fc.Ed(цвет,w,h)` rect, `Flip` → Hr(!0)); `SimpleEffect` → `UIa` (Picture/Sequention анимации + OscillationX/Y, ReappearX/Y, Rotation, Speed, Transparency с Period/Value/Ease ключами); `Bf.R3a` (L486): X/Y/Rotation/Width/Height-скейл.
- Внутри слоя: `NWa`/`pWa`/`fXa` (L487-488) — аппенд с `Dla(QH)` (z −0.01 на элемент → **порядок внутри слоя = порядок в XML**).

### 2.6 Натив
`Camera` (core/scene/renderer.hpp L40-67): `world_to_screen_x(wx, factor) = (wx + Io*factor − cx)*zoom + w/2`, `world_to_screen_y = (wy + F9*(1−zoom) − cy)*zoom + h/2` — **совпадает** с JS `Wrb(Io*bp)`/`Xrb(F9*(1-Bj))` (F9=(arena_h/2−floor)/2). Y-flip: `y_ndc = 1 − 2*y/view_h` (sprite_batch.cpp L195) = `Bl.m22=-d`. `default_camera` (location_scene.cpp L215-233): zoom = view_w/arena_w (аналог JS фита), center_y через floor_screen_y.

**ЧТО У НАС НЕ ТАК (камера):**
1. **Нет аспектных клamпов** из `Sya`: `f *= clamp(c,.45,1)` и `f *= min(N.width/(span*f+100),1)` — натив `FightCamera::framing` (fight.hpp L190): `zoom = min(1, view_w/span)` — **маржа 100px есть, аспект-клampов нет; портретная вертикаль (−500-899) нет**.
2. **Нет панорамы Io в бою**: натив `camera.center_x` — просто середина бойцов; оригинал клampит `Io` к `±((w−oGa)*Bj*.5 − nC*.5)` — нативные бойцы могут уезжать за край.
3. **Нет сглаживания ql.dZa** (velocity-clamp 200, delta-clamp 50, IO*=.15) — натив позиционирует камеру мгновенно.
4. **Нет зум-масштабирования слоёв** (`setScale(Bj)` для Type=2/Scaling): натив масштабирует только через Camera.zoom, т.е. в «зум-ауте» фон ведёт себя неправильно (в оригинале горизонт сжимается сильнее, чем бойцы).
5. **Нет леттербокс-полос** (N.BK, ma.YY) и **нет вертикального сдвига D(...)** при портрете.
6. `ma.Kq` (видимый rect для HUD) не реализован — HUD-элементы боя позиционируются по жёстким пикселям (впрочем, на 16:9 это почти совпадает).

---

## 3. Боец — `dv.ia` (L840-841), `Fk.update` (L841-842), капсулы

### 3.1 Данные и сборка
- `wd` (L490-492): `Fc = new Ae` (данные скелета/анимаций, НЕ тень), `go = dd("Model")`; дети: `dd("Mesh")` с компонентом `MW = Fk` (uWa), `dd("ModelCapsules")` + компонент `Jba = Ek` (vWa на go), далее weapon/armor/эффекты через `bv` (bone-visual, L834-835).
- Треугольники: `dv` (g="16E", L840-841): `DXa/VeA` интернируют имена костей → `qu = jma(zU)` (Int32Array индексов); `ia()` (L841): **для каждой кости копирует `kM[b].ma.x/y` в `Xg[2b], Xg[2b+1]`** — вот этот самый `dv.ia` из задания: вершины меша = текущие позиции костей (2D-проекция).
- `Fk.update` (L841-842): `Bc.init()` (собрать qu), `bK` (под-контроллер, напр. оружие/фигура-анимация `ni`) update, `Bc.ia()` → вершины, затем `e.indices = Bc.qu; e.we=…; e.Xg=Bc.Xg; e.Rd=…` — **Yi-эффект получает массивы вершин и индексов**.
- Цвет: `Fk.La(color)` / `Qs(color)` → `e.color = Na.cd(color)`; на боец-контейнере: `ev.Gf` вызывает `a.model.Qs(b)` где `b = Lb.N2` = **Root Color локации** (`aXa(a.oa, this.Lb.N2, c)`, L825). Canvas2D `Ph`: `r = (q.z*255|0) | (q.y*255|0)<<8 | (q.x*255|0)<<16` — цвет из `c.color`.
- Капсулы: `Ek` (g="170", L842) — контейнер; `oWa(a)` — добавить; `update()` — обновить все. Каждая капсула = `Dk` (g="16B", L835-836): `e.add(b,e,c,a,stroke/2)` — **сегмент через Xi: ширина = stroke/2 с каждой стороны → полная толщина = stroke**; `Xi.uXa` (L1637) строит квад + круглые концы (n = π/(2·acos(1−precision/e)), precision=0.1). `stroke` приходит из данных Figure `Radius1*2` (см. натив screens.cpp L984 `stroke = rad*2*zoom`).
- Прозрачность: глобально `$m` (2=видно), `Kh.IZ/Kw`, альфа через `node.wa()`/эффект `alpha`; конкретно боец: `Jma/wa` и т.п.

### 3.2 Рендер меша/капсул (дефолт — WebGL)
- `Pk` (L1746, `Xd=Yi`): `gl.drawElements(we, 4)` (GL_TRIANGLES), цвет uniform `bi` (RGBA), z = `ED(hb) = -.001*translate.z`.
- `Ok` (L1734, `Xd=Xi`): то же самое для сегментов (квады с круглыми концами уже в буфере `Xg/indices`).
- Canvas2D: `Ph` (L1570-1582, `Xd=Tj`, узел `$e`): `path.gk(color, w, true)`, флаги `t`: `(t&1)` триугольники (XR/mE per triangle), `(t&2)` капсульные outlines (пары вершин из `e.qu` + `gK` нормаль), `(t&4)` капсульные loop-индексы (`e.xza`), `(t&8)` точки (`mJa/2` полуразмер), `(t&16/32/64)` дополнительные буферы сдвинутых вершин. Применяется Path2D fill+stroke через `ke` (Td, L1555-1558, опкоды).
- **Z-порядок фигур:** внутри `dd("Model")` дети в порядке добавления: Mesh → ModelCapsules → оружие/эффекты (bv). Бойцы в `ev.qh.go`: первый созданный (Rw = противник, в `ca.o1a()` сначала `yb=Gf(kc)`) на z=-.001, второй (pF = игрок) на z=0 → **противник позади игрока** (L845).
- Тень: **в JS НЕТ пер-бойцовской тени**. `ma.KI` (L1831) — глобальный чёрный fade-rect (`Fc.Ed(-16777216)`, wa(0)), `Dw/HE` — затемнение экрана в переходах. `wd.Fc` (Ae) — скелетные данные. Никакого `shadow` под бойцом в оригинале нет — «тень» у игрока в SF2 web отсутствует (это фича натива, см. ниже).
- Оружие/броня: прикрепление через `bv` (L834-835): `update()` — `c.scale.x = 1*hd()*scale.x; c.scale.y = scale.y` (зеркалит по facing `hd()`); позиция `effect.position.nt(Fc)` (трансформ кости); `rotate()` — поворот `Vla*hd()`. Эффекты-модели (магия) через `cv.lwb` (L838-839): `ni` из `magic/{name}.json` + `bv` обёртка.

### 3.3 Натив
`core/scene/model.cpp` — кости (Y уже негатив: `b.y = -attr Y`), `TriResolved` индексы; `fighter.cpp` `build_vertices`/`sample` — проекция костей (см. `dv.ia`); `screens.cpp` L929-1030: `project()` + `draw_triangles` (меш), `draw_capsules` — **толстые квады + круги-концы, stroke = Radius*2** (совпадает с `Dk`).
**ЧТО У НАС НЕ ТАК:**
1. **Тень — ВЫДУМАННАЯ**: натив рисует процедурный чёрный эллипс под бойцом (screens.cpp L884-918, alpha 0.35, rx=spread/2, ry=rx*0.22). В оригинале тени нет — силуэт стоит прямо на линии пола. (Если ориентир — «оракул» — ок, но это не оригинал.)
2. **Капсулы-фигуры (Figure Type=Capsule) рендерятся только как коллизионные рёбра** (edge_max, dedup по edge) — в оригинале капсулы-фигуры — отдельные `Dk` с собственным stroke (Radius1*2 каждого, без dedup), и они дают мягкие круглые суставы; натив аппроксимирует круг 12-сегментным диском (оригинал — непрерывная дуга через Xi, precision арки зависит от толщины).
3. **Порядок бойцов**: натив рисует player → enemy (screens.cpp L929-932 порядок вызовов) — в оригинале enemy (Rw) за z=-.001 → рисуется ПЕРВЫМ (позади), player сверху. При перекрытии силуэтов порядок разный.
4. **Нет блендинга фигур**: оригинал рисует фигуры с alpha через `Ph` (Path2D fill со stroke поверх) — натив draw_triangles одним цветом без обводки (нет double-draw fill+stroke).
5. Нет ни `Fk.bK` (оружие как анимированная фигура), ни `bv`-прикрепления оружия к кости (натив, видимо, рисует оружие как отдельную модель или вообще не прикрепляет позиционно к кости).

---

## 4. Эффекты / частицы

### 4.1 Контейнеры
- `tl` (g="171", L842-844): `qh` = бойцы, `Gq` = `Xm` (ground effects, z=+.01), `Hq` = `Xm` (air effects, z=+.01) — эффекты слоями `+.01` поверх всего.
- `Xm` (L836-837) / `cv` (L837-839 "EffectsRunning"): регистрация по имени + модели: `Nt(a)` (добавить `lwb`), `Ot` (снять `Dwb`), `Pt` (стоп `Hwb`). Слушатели вешаются в `tl.ZP` (L844) на fighter-события (`Nt/Ot/Pt` V-bus'ы wd).
- `cv.lwb` (L838-839): `b.Fc.Wl = b.da.hd()` (facing); позиция `effect.position.nt(c)`; scale `Wl*scale.x/scale.y`; **модель-эффект**: `f = d.wWa()` = `ni` — фреймовая анимация из `magic/{name}.json` + `magic/{name}.png` (`E.get(G.qf(...))`), `iterations` (wcb → -1 = loop), `RLa/wrb` (вперёд/назад), speed `mP = NL/60`; обёртка `bv` (поворот/флип). `a.DWa(a)` — привязка к бойцу.
- `cv.WL()` (L839): `1/v.on()` (time-scale), `animate.ia(dt*a)`, удаление по окончании (`LJ` false), `P1 && !Yla` → update.

### 4.2 Спарки/попадания
- `av` (g="169", L833-834): частица-«искра» из текстуры `E.get(260)` (белая точка): `fg = (x/200+rand(-40..40)/10, y/200+rand(-60..20)/10)`, гравитация `fg.y += .2`/кадр, поворот `atan2(fg.y,fg.x)`. Спавн: `Ut.ryb(point, color, count)` (L824) — пачка частиц; обновление `Cnb()`, очистка `dKa()` при `uba>90` (L824). Цвет из `Na.cd(N2)` → цвет локации.
- Trigger: `ql.Rub(pos, color, count)` (L369) — выставляет `yv` (Tt), срабатывает при `Bob()` (L368) — **ударные искры при попадании** (вызывается из ca-хендлеров попаданий).
- `sXa` (L827-828): маркеры за экраном (текстура 1300, фреймы "0".."19" — стрелки-указатели куда ушёл противник).
- Магия: энергошары (`wd.bh` "magic bullets") + `cv`-эффекты из `magic/*`.

### 4.3 Частицы локации (ParticleEffect)
- `jh` (L1147-1148): из XML: Life, Gravity, ForceX/Y, Rate, Emitter, MaxParticles(500), AngVel, StartSize, StartRotation, StartSpeed, VelocityX/Y, Color, Frame (текстура 1304); узел Hd + Xb с `Ah`-эффектом (старт/конец цвет — градиент).
- `QIa` (L481-482): из `<ParticleEffect X Y>`.

### 4.4 Пи(r)о-эффекты (ударная волна/потрясение, гитстun-flash)
- `ql.DL(hw)` (L370): тряска камеры (траектория, `N5/cU` каунты, `d3a` применяет); `ql.Dvb` (L370-371): линза-зум; `ca.xX/mV` (hitstun/flash таймеры) → `Kla` (L369 camera shake через `ia.Hyb` — «флеш» из `lo` (E.get(1306)) с синус-альфой `kyb` L825).
- Пыль/рregnний: SimpleEffect с `Speed`/`ReappearX` (dust в арене, XML выше).

### 4.5 Натив
**ЧТО У НАС НЕ ТАК:** нативный рендер эффектов отсутствует как класс — нет `Ah`-частиц (ar/инstantced quads), нет `ni`-фреймовых магических анимаций, нет спарков `av` (ryb/Rub), нет стрелок sXa, нет камера-тряски (`ql.DL/d3a/Byb`). Искры/пыль/магия не рендерятся вовсе. (Есть аудио и damage-числа, но визуальные триггеры эффектов не подключены.)

---

## 5. HUD / UI-рендер

### 5.1 Экран-контейнеры
- `$d` (L119-122): `elements = Db`, `content = Ea`, `node = Hd ($m=1)`; `Dw(a)/HE(a)` — fade (vf-стейт type 5). `mc` (L122-127): `Ta = Vg` (UI-камера), `back/cf` Hd-узлы; `Ea` (L125-126): **back → все экраны стека → cf** (рендер всего стека каждый кадр).
- `Ar` (g="403", L2016-2021) — fight-HUD: `mb = Sf` (слой боя), `Se = Cr` (раунд-баннер), `QE = Dr`; `Gzb(...)` → `mb.strike(...)` — HP-обновления; `ia()` → `mb.ia()`.
- `Sf` (g="409", L2033-2041): `Jn` (пауза-кнопка, `db.xz(E.get(1294), y.IQa)`), `Id/je = lk(0/1)` (панели бойцов), `OA` (text), таймер-текст, `X0() = round.gma*60` (секунды из round.gma), `layout()` использует `ma.Kq`.
- `lk` (g="407", L2027-2033) — панель бойца: `Hf = oe` (иконка предмета), `al = Br` (имя/HP — `al.v5(gd)/g5(Zn)`), `Sh = Fr` (портрет+HP-полоса, `Vma(a)` — установить HP), `S4 = Er` (раунд-точки, текстура 1294 фреймы `y.UU/y.LQa`), `Jh = Gr` (всплывающие числа/статистика), текст `ea`. Позиции: игрок node.C(130), противник node.C(-460) → **панели симметрично, X=±130/±330, Y=-50/-100**.
- `db` (L1837-1840) — кнопка: `$w` (текстура+фрейм), `Xc = R.Ed(-65536,…)` (hit-rect, красный), `aa` — тап через `ma.Bd` (клик тест), `g1()` → `pa.Z(ee)` (событие). Кнопки: `Yh` (label+кнопка), `ck` (toggle), `Bb` (большая, scale-анимация `zTa`, `fza` = "btn"+class), `Qf` (иконка). Атласы: `E.get(260)` (белая кнопка/точка), `E.get(244)` (btn bg).
- `Ex` (L2015-2016) — таймер: два `ea`-текста + `Uca.V(":")` (type 0) или `Uca.V("/")` (type 1 — HP "текущее/макс").
- `Gr` (g="40C", L2049-2052) — всплывающие боевые числа (урон/комбо) через `Hx` (E.get(1310) иконки + `ea` L1308).

### 5.2 Как рисуется
- Текст: `ea` (L1610-1621, `R`-обёртка) → `Rj` (ке):
  - Canvas2D: `yq`→`Td` path; `wq` (L1545-1548) — битмап-грид: per-glyph drawImage из шрифт-атласа, `zxa`-режимы, `fillColor`/`cma`/`NY` (тень/цвет) через composite ops (source-over/destination-over/source-atop) **прямо в `Lk.aL/NK` буфер (не offscreen)**.
  - WebGL: `Sq` (L1737-1739) — `Rq`-шейдер, текстура шрифта, `u_mode` (1=text, 2=colored), индексный буфер.
  - Шрифты: `E.Na()` = атлас 264 (L93); таймер/раунд — `E.get(1298)`, иконки — 1308/1310.
- Полосы HP: в оригинале это **`xg`-rect эффекты** (xq/Wq/Uq) поверх фреймов и, вероятно, `Zb`-ноды; точные фреймы HP-бара лежат в `E.get(1310)`/1294 (см. `Bb.wl(vc.qM(...))` — градиент-текстуры `vc.qM` из `Th.Fna/SU` — **HP-бары закрашиваются 4px-градиентными текстурами** `E.get(264,16)`? нет — `Th.Fna(1)` — см. `Th` — клass градиентов).
- Кнопки: `Zb` (`db`-эффект) → `xq/Wq`; иконки — vq/Qq.

### 5.3 Натив
`core/app/screens.cpp` L1037+: HealthBar фреймы + битмап-шрифт таймер/раунды (комментарии "bitmap font + HealthBar frames" — коммиты e68f2175, 3701fbbb). Есть `font.cpp` (fnt parse), `sprite_batch`.
**ЧТО У НАС НЕ ТАК:**
1. **Нет `ma.Kq`-раскладки**: HUD жёстко по пикселям (bar_w=440, bar_y=150), оригинал позиционирует панели через `Kq.F5a()` и `content.la(min(800, min(N-J, W-P))/fa.x*.6)` (Cr.layout L2027) — при другом аспекте/letterbox расхождение.
2. **Нет fade-слоёв экранов** (`vf`-стейт, `Dw/HE`): переходы натива (если есть) не используют альфа-фейд узла.
3. **Нет `Sf`-раскладки раунд-точек** (`Er`: 40px шаг, 25° наклон) и нет духа боевых чисел (`Gr`+`Hx`).
4. Таймер: оригинал `X0() = round.gma*60` (round.gma — длина раунда в пакетах по 60 кадров) с `padStart(2,"0")` (L2154) — натив `gma - ceil(time)` — надо сверить формат (M:SS).

---

## 6. Транзишены экранов — `ae` (g="42", L127-132)

- `mc.Taa` (L126): если у экрана есть незагруженные ассеты (`Pea()`), оборачивает в `Xg` (loader), иначе: стек-пуш с `ae.spb` (kind 0) / `ae.TKa` (kind 3, старый→новый) и т.д.
- `ae` — стейт-машина (kind 0..4), фазы:
  - kind 0 (пуш нового экрана поверх): `b.Te(1); Kt(); pr(); wait QQ (пауза-задержка); Te(2); Dw(0)→Dw(a) где a=ed(ot) (фейд-ин за ot сек); a==1 → Te(3); In(); Kg=true`.
  - kind 1 (пуш с лоадером): то же + `a.Te(5)` (старый экран уходит в фейд), `kEa()` — снять экран.
  - kind 2 (ipb — текущий экран уходит к родителю): `a.Te(5); HE(0)` → HE(a) за `GQ()` (фейд-аут), потом `Te(6); sm; B`.
  - kind 3 (TKa — замена): фейд-аут старого (HE за GQ/2), фейд-ин нового.
- `$d.Te` (L121): `2: node.$m=0; ym(0)` (невидим), `4: node.$m=0`, `6: node.$m=1`. `Dw(a)→ym(a)→O7a().tla(a)` — `vf` (type 5 стейт на Hd) — альфа узла; **фейды = альфа Hd-узла экрана + $m=1/0**.
- `wf`/`vf` (L1716) — стейт-alpha: `wg extends sd` (L1716: `class wg extends sd{constructor(){super(4)...` — type 4? Нет — `vf=sd`-подтип; в `$d.O7a` тип 5).
- `pr()` (L121): позиционирование content под аспект через `Mr.Cfa().fn(AZ.x/AZ.y)`.
- Задержки: `QQ()` (экран: обычно 0.25/0.5, у `ma` нет переопределения → 0?); `ot()` (fade-in ~.5), `GQ()` (fade-out ~.25).

**ЧТО У НАС НЕ ТАК:** в нативе нет screen-stack `mc` и fade-транзишенов `ae`; нет `Xg`-обёртки лоадера с прелоадом ассетов экрана; переходы между боем/картой/меню, скорее всего, мгновенные (проверить `core/app/screens.cpp` — нет упоминаний fade).

---

## 7. Локации — построение слоёв из XML (подробности)

См. §2.5. Дополнительно:
- **`ModelsViewer` (пол/земля)**: это НЕ картинка, а **маркер боевого слоя** (Type=2, `hn`): читает `PlayerPositionX/Y` и `EnemyPositionX/Y` в `Bf.Yia/B_` (L476), бойцы спавнятся туда (`ca` L381: `a.kc.position = location.Yia`). Сам «пол» как графика — отдельные Image-слои (`_0007_arena` и т.п.) ПОВЕРХ боевого слоя. z боевого слоя: `ldb` (в арене -33: после 11 слоёв × -3... счёт: layer#11 (ModelsViewer) имеет c=-3*10=-30? Нет: первый слой c=0, второй -3, ... 11-й = -30? Формула: zjb(child, c), c+=-3 после каждого → k-й слой z=-3(k-1). Арена: 13 слоёв → ModelsViewer (11-й по счёту в XML) z=-30, пол-слой z=-33, glow z=-36.
- **`Ui`-эффекты SimpleEffect**: OscillationX/Y (Offset + ключи Period/Value/Ease), ReappearX/Y (`of` Min/Max — периодический респавн по X), Rotation (StartAngle/Offset + ключи), Speed (X/Y), Transparency (Offset + ключи KWа). Анимация через `xl` (L1141: `frames`, `MT(frame)` → `Y.Cb(frames[a])` — тот же механизм, что `ni`).
- **Картинки** (`Image`): из атласа локации `locations/{name}/{name}.png...` (Pages), `ClassName` → фрейм; `X/Y/Rotation/Width/Height` из атрибутов (`R3a` L486-487); `Flip` → `Hr(!0)` (функция зеркалит узел); `pixel_1` → **заливка `Fc.Ed(color,w,h)`** — чёрные полосы виньетки (арена: Y=351/H=200 и Y=-356/H=200).
- **Sequention** (анимация-плитка): `xl(1,a)` с `Speed`, `FrameStart/FrameEnd`, `Offset` — кадры из `className` по индексам; `vqb` возвращает null если нет кадров.
- Параллакс: `bp = Factor` (атрибут `<Layer Factor>`); **Factor=0 → слой не двигается горизонтально** (небо); Factor=1 → двигается с миром (пол/стены); Scaling=1 → слой масштабируется зумом камеры (`setScale(Bj)`); Type=2 → боевой слой (`lEa()` true, `hn`).

### 7.1 Натив
`location_scene.cpp` — `load(params_xml, atlas_jsons, res_root)` (мульти-атлас, mini части: arena = arena.ca2949ef.json + arena-2.586e4f15.json), `Layer{factor,type,sprites}`, layer factor применяется в `sprite_to_quad` (`camera.world_to_screen_x(t.x+lx, factor)`), ModelsViewer пропущен, `pixel_1` → solid rect (`R.Ed` аналог), `default_camera` — фит арены.
**ЧТО У НАС НЕ ТАК:**
1. **ModelsViewer-слой не рендерится нативом** — бойцы рисуются ОТДЕЛЬНО от слоёв (не внутри z-позиции Type=2), из-за чего пол/арена-стороны/пыль/glow оказываются ПОД бойцами (а в оригинале — поверх; см. §1.4).
2. **Нет SimpleEffect-анимаций** (Oscillation с ключами Ease+/-, Speed/Reappear пыли, Sequention) — статичные спрайты.
3. **Нет `Scaling`-зума слоёв** (`setScale(Bj)`) и **нет `pixel_1`-виньетки** — натив, вероятно, не рисует чёрные полосы арены (проверить).
4. **F9/центр**: натив `layer_vshift` использует `arena_h` из Root — у dojo ок, но если `Floor`/`PositionY` отличаются, вертикальная фокусировка (положение линии пола на экране) может не совпадать с оригиналом (у оригинала ещё портретный сдвиг `D(round((H-e*f)/2)/f*.5)`).
5. Мульти-атлас и `Pages`: нативные тайлы/страницы атласа не используются (оригинал склеивает `Pages` страниц в одну текстуру).

---

## Итог: топ-5 мест, где натив расходится с оригиналом по рендеру

1. **Z/порядок отрисовки**: оригинал рисует пол/арена-стороны/пыль/glow/чёрные бары ПОВЕРХ бойцов (бойцы внутри Type=2 слоя между fog 0.9 и полом 1.0) и enemy позади player (z=-.001/0). Натив — весь фон, затем бойцы (и player→enemy). → **пол выглядит «под» бойцами, вертикаль ломается, glow не поверх.**
2. **Камера**: натив — упрощённый `zoom=min(1, view_w/span)` без аспект-клampов Sya (0.45..1, `min(N.width/(span*f+100),1)`), без портретной вертикали, без сглаживания ql.dZa (200/50), без панорамы Io-клampа, без леттербокс-полос ma.YY и без параллакс-зума слоёв (`setScale(Bj)`). → **на «сломанной вертикали» и при зуме бойцы/фон ведут себя не как в оригинале.**
3. **Тень бойца**: натив добавляет процедурный чёрный эллипс (alpha .35, 16-гранник) — **в оригинале тени под бойцом НЕТ** (только глобальный `ma.KI`-фейд экрана). Если стремиться к оригиналу — убрать; если нужен «жизненный» вид — это осознанное отклонение.
4. **Капсулы**: натив рисует только коллизионные рёбра (dedup по edge, круг = 12-гранник, stroke=Radius*2). Оригинал: все капсулы-фигуры моделей через `Dk`/`Xi` с непрерывной дугой (precision 0.1) и фигуры-капсулы отдельно; плюс `Ph` рисует fill+stroke двойным проходом (у натива — только fill треугольников).
5. **Эффекты отсутствуют целиком**: нет частиц `Ah`/`ar` (ударные искры `av`/ryb, dust с ReappearX+Speed, магические `ni`-анимации из `magic/*.json`, маркеры sXa, камера-тряска ql.DL/d3a) и нет SimpleEffect-анимаций локаций (Oscillation/Sequention). Натив — статичная сцена без «жизни».

---

## Файлы для правок при приведении к оригиналу
- `core/app/screens.cpp` — порядок отрисовки боя (вставить бойцов между слоями арены, убрать/переделать тень, капсулы через непрерывные дуги).
- `core/scene/fight.cpp` / `fight.hpp` (`FightCamera::framing`) — полная формула `ma.Sya` (аспект-клampы, маржа 100, миним. zoom 0.6..1.3, портретная вертикаль, сглаживание 200/50).
- `core/scene/renderer.hpp/cpp` — `setScale(Bj)` для слоёв, леттербокс (`N.BK`), `ma.Kq`, screen-stack `mc` + fade `ae`.
- `core/scene/location_scene.cpp` — SimpleEffect-анимации (Oscillation/Reappear/Speed/Transparency), `pixel_1`-виньетки, сеquention, мульти-атлас Pages.
- Новый: particle-система (`Ah`/`ar` instanced quads), `ni`-фреймовые эффекты (`magic/*`), спарки `av` (текстура 260), маркеры sXa (1300).