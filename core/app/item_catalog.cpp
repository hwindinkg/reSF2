// Item catalog implementation — parses res/list.xml <Items> (JS `it` g="5A").

#include "app/item_catalog.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include "xml_doc.hpp"
#include "scene/damage.hpp"

namespace sf2::app {

namespace {

// The JS `I` item-type enum values (L1271963).
constexpr const char* kTypeWeapon = "Weapon";
constexpr const char* kTypeArmor = "Armor";
constexpr const char* kTypeHelm = "Helm";

bool attr_bool_str(const char* v) { return v != nullptr && std::string(v) == "1"; }

// --- internal_settings.xml table parse (JS `ow.parse`/`Mv.parse`/`Nv.kBa`) ---
// All helpers mirror the JS coercion primitives exactly.

// `u.ka` (L1262938): null -> def, "1"/"true" -> true, else false.
bool js_bool(const pugi::xml_attribute a, bool def = false) {
    if (!a) return def;
    const std::string v = a.value();
    return v == "1" || v == "true";
}
// `u.I` (L1263010): `K.parseInt` -> int, null -> def.
int js_int(const pugi::xml_attribute a, int def) {
    if (!a) return def;
    char* end = nullptr;
    const long v = std::strtol(a.value(), &end, 10);
    if (end == a.value()) return def;
    return static_cast<int>(v);
}
// `u.H` (L1263074): `parseFloat` -> float, NaN/null -> def.
float js_float(const pugi::xml_attribute a, float def) {
    if (!a) return def;
    char* end = nullptr;
    const double v = std::strtod(a.value(), &end);
    if (end == a.value()) return def;
    return static_cast<float>(v);
}
std::string js_str(const pugi::xml_attribute a) {
    return a ? std::string(a.value()) : std::string();
}

// `Nv.kBa` L604695: one `<AttributeLimits>`/`<ItemLimits>` block. `LeftLimit`/
// `RightLimit`/`LevelMultiplier`/`Shift` default to -1 (`Ew` `rFa`/`MKa`/`yFa`/
// `shift`); `Level` is a `a|b|c` list (`ir`, empty when absent).
std::vector<ShopBarScaleLimit> parse_limit_block(const pugi::xml_node parent) {
    std::vector<ShopBarScaleLimit> out;
    if (!parent) return out;
    for (const pugi::xml_node e : parent.children("Limit")) {
        ShopBarScaleLimit f;
        f.left_limit = js_int(e.attribute("LeftLimit"), -1);    // `rFa`
        f.right_limit = js_int(e.attribute("RightLimit"), -1);  // `MKa`
        if (e.attribute("Level")) {
            const std::string lv = e.attribute("Level").value();
            if (!lv.empty()) {
                std::size_t p = 0;
                for (;;) {
                    const std::size_t q = lv.find('|', p);
                    const std::string tok =
                        lv.substr(p, q == std::string::npos ? q : q - p);
                    char* end = nullptr;
                    const long v = std::strtol(tok.c_str(), &end, 10);
                    if (end != tok.c_str()) f.levels.push_back(static_cast<int>(v));
                    if (q == std::string::npos) break;
                    p = q + 1;
                }
            }
        }
        f.level_multiplier = js_float(e.attribute("LevelMultiplier"), -1.0f);  // `yFa`
        f.shift = js_int(e.attribute("Shift"), -1);                            // `shift`
        out.push_back(std::move(f));
    }
    return out;
}

// `cw` (L633598) + `bw.parse` (JS `v.xIa`, `<OutdateLevels>`): one row. `Type`
// splits on "|" (absent -> `[""]`, `cw.parse` `a!=null?a:""`).
struct OutdateLevel {
    int value = 0;                   // `Value` (`u.H`)
    std::vector<std::string> types;  // `Type`
};

// `ow.parse` L615263 (`v.eo.attributes`) + `Mv.parse` L604556 (`v.Ova`) +
// `bw.parse` (JS `v.xIa`, `v.xIa.parse(a.A("OutdateLevels"))` @593527).
void parse_shop_tables_xml(const std::string& xml_text,
                           std::vector<ShopAttributeDef>* defs,
                           std::vector<ShopBarScale>* scales,
                           std::vector<OutdateLevel>* outdates = nullptr) {
    sf2::data::xml_doc doc;
    doc.parse(xml_text);
    const pugi::xml_node root = doc.root().first_child();
    if (!root) throw std::runtime_error("shop tables: settings root missing");
    defs->clear();
    scales->clear();
    if (const pugi::xml_node attrs = root.child("Attributes")) {
        for (const pugi::xml_node a : attrs.children("Attribute")) {
            ShopAttributeDef d;
            d.name = js_str(a.attribute("Name"));
            d.icon = js_str(a.attribute("Icon"));
            d.bar_scale = js_str(a.attribute("BarScale"));
            d.hidden = js_bool(a.attribute("Hidden"));
            d.shop_hidden = js_bool(a.attribute("ShopHidden"));
            d.profile_hidden = js_bool(a.attribute("ProfileHidden"));
            defs->push_back(std::move(d));
        }
    }
    if (const pugi::xml_node bs = root.child("BarScales")) {
        for (const pugi::xml_node s : bs.children("BarScale")) {
            ShopBarScale sc;
            sc.name = js_str(s.attribute("Name"));
            sc.type = s.attribute("Type") ? std::string(s.attribute("Type").value())
                                          : std::string("Linear");
            sc.power = js_float(s.attribute("Power"), 0.0f);
            sc.min = js_float(s.attribute("Min"), 0.0f);
            sc.attribute_limits = parse_limit_block(s.child("AttributeLimits"));
            sc.item_limits = parse_limit_block(s.child("ItemLimits"));
            scales->push_back(std::move(sc));
        }
    }
    if (outdates != nullptr) {
        outdates->clear();
        if (const pugi::xml_node ol = root.child("OutdateLevels")) {
            for (const pugi::xml_node o : ol.children("OutdateLevel")) {
                OutdateLevel d;
                d.value = static_cast<int>(js_float(o.attribute("Value"), 0.0f));
                const std::string t =
                    o.attribute("Type") ? o.attribute("Type").value() : std::string();
                std::size_t p = 0;
                for (;;) {
                    const std::size_t q = t.find('|', p);
                    d.types.push_back(t.substr(p, q == std::string::npos ? q : q - p));
                    if (q == std::string::npos) break;
                    p = q + 1;
                }
                outdates->push_back(std::move(d));
            }
        }
    }
}

// Runtime storage for the parsed tables (JS `v.eo.attributes` / `v.Ova.z7`).
std::vector<ShopAttributeDef> g_attr_defs;
std::vector<ShopBarScale> g_bar_scales;
std::vector<OutdateLevel> g_outdate_levels;
bool g_tables_loaded = false;

// Lazily parse the canonical extracted settings file when the explicit
// `load_shop_tables_from_settings` (config load) did not run.
void ensure_shop_tables_loaded() {
    if (g_tables_loaded) return;
    std::ifstream in("reference/extracted/xml/res/internal_settings.xml",
                     std::ios::binary);
    if (!in) throw std::runtime_error("shop tables: internal_settings.xml missing");
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    load_shop_tables_from_settings(text);
}

} // namespace

// The runtime `<Attributes>` table (JS `v.eo.attributes`; `ow.parse` L615263
// @591524 `v.eo.parse(a.A("Attributes"))`). Populated by
// `load_shop_tables_from_settings`; lazily parsed from the shipped
// `internal_settings.xml` otherwise. `ms.setParameters` (L1170616) walks it
// in file order and skips only `hidden`.
const std::vector<ShopAttributeDef>& shop_attribute_defs() {
    if (!g_tables_loaded) ensure_shop_tables_loaded();
    return g_attr_defs;
}

namespace {

// `internal_settings.xml` `<BarScales>` — the `v.Ova` `Mv` table (JS L604556;
// `Mv.parse` reads Name/Type/Power/Min + the `<AttributeLimits>`/`<ItemLimits>`
// rows via `Nv.kBa` L659xxx). Each row is `{LevelMultiplier, Shift, LeftLimit,
// RightLimit, Level[]}` (`Ew`, defaults -1/-1/-1/-1/empty). `v.BP` (the `Exp`
// formula divisor) is `v.BP=u.H(a.A("DamageDoublingRange").attributes.get(
// "Value"))` (L593016), already parsed into
// `FightParams::damage_doubling_range` by `load_fight_params_from_settings`
// (damage.cpp); `shop_attribute_bar_fill` reads it from there instead of the
// previous local `10.0f`.

const std::vector<ShopBarScale>& shop_bar_scales() {
    if (!g_tables_loaded) ensure_shop_tables_loaded();
    return g_bar_scales;
}

} // namespace

// `fi.Z7a` (L2273-2274): the value -> bar-fill ratio. Mirrors `fi.tbb`
// (L2272-2273) field resolution exactly:
//   a==null||a==""  -> xW=iO=-1, B9=1, Fk=0
//   else            -> xW=iO=0 then (limit!=null) xW=rFa, iO=MKa, B9=yFa, Fk=shift
//                      rH=dk<0?0:dk, bC=min<0?0:min, XTa=type
//   b = xW>=0 && iO>=0 ? iO : level*B9 + Fk          (`A$a` L2270)
//   Exp:    c = 2^((value-b)*rH/v.BP)                 (`v.BP`=kDamageDoublingRange)
//   Linear: c = (value/b)^rH
//   c<0?c=0 : c>1?c=1 ; return max(c, bC)
float shop_attribute_bar_fill(const std::string& bar_scale, int value, int player_level) {
    const ShopBarScale* bs = nullptr;
    if (!bar_scale.empty()) {
        for (const ShopBarScale& s : shop_bar_scales()) {
            if (s.name == bar_scale) {
                bs = &s;
                break;
            }
        }
    }
    int xw = 0;              // `xW`
    int io = 0;              // `iO`
    float level_mult = 0.0f; // `B9`
    float shift = 0.0f;      // `Fk`
    float power = 0.0f;      // `rH`
    float min_fill = 0.0f;   // `bC`
    std::string type;        // `XTa`
    if (bs == nullptr) {
        xw = -1;
        io = -1;
        level_mult = 1.0f;
    } else {
        // `f7a(player_level)` then the `g7a()` no-Level default (`tbb` b=true).
        const ShopBarScaleLimit* lim = nullptr;
        for (const ShopBarScaleLimit& l : bs->item_limits) {
            if (std::find(l.levels.begin(), l.levels.end(), player_level) !=
                l.levels.end()) {
                lim = &l;
                break;
            }
        }
        if (lim == nullptr) {
            for (const ShopBarScaleLimit& l : bs->item_limits) {
                if (l.levels.empty()) {
                    lim = &l;
                    break;
                }
            }
        }
        if (lim != nullptr) {
            xw = lim->left_limit;
            io = lim->right_limit;
            level_mult = lim->level_multiplier;
            shift = static_cast<float>(lim->shift);
        }
        power = bs->power < 0.0f ? 0.0f : bs->power;
        min_fill = bs->min < 0.0f ? 0.0f : bs->min;
        type = bs->type;
    }
    const float baseline = (xw >= 0 && io >= 0)
                               ? static_cast<float>(io)
                               : static_cast<float>(player_level) * level_mult + shift;
    float c;
    if (type == "Exp") {
        c = std::pow(2.0f,
                     (static_cast<float>(value) - baseline) * power /
                         sf2::scene::fight_params().damage_doubling_range);
    } else if (type == "Linear") {
        c = std::pow(static_cast<float>(value) / baseline, power);
    } else {
        return 0.0f;  // `XTa` undefined (unreachable: shipped types are Exp/Linear)
    }
    if (c < 0.0f) {
        c = 0.0f;
    } else if (c > 1.0f) {
        c = 1.0f;
    }
    return std::max(c, min_fill);
}

std::vector<CatalogItem> parse_item_catalog(const std::string& xml_text) {
    std::vector<CatalogItem> out;
    sf2::data::xml_doc doc;
    doc.parse(xml_text);

    const pugi::xml_node root = doc.root().first_child();
    if (root == nullptr || std::string(root.name()) != "List") {
        throw std::runtime_error("item catalog: root <List> missing");
    }
    const pugi::xml_node items = root.child("Items");
    if (!items) {
        return out;
    }
    for (const pugi::xml_node item : items.children("Item")) {
        CatalogItem ci;
        if (item.attribute("Name")) ci.name = item.attribute("Name").value();
        if (item.attribute("Type")) ci.type = item.attribute("Type").value();
        if (item.attribute("SubType")) ci.subtype = item.attribute("SubType").value();
        // JS `pL` L322: `lock` = `PackLabel` (the GroupID fallback never occurs
        // in the shipped list.xml — 378 `PackLabel`, 0 `GroupID`).
        if (item.attribute("PackLabel")) ci.pack_label = item.attribute("PackLabel").value();
        if (item.attribute("Labels")) ci.labels = item.attribute("Labels").value();
        if (item.attribute("Model")) ci.model = item.attribute("Model").value();
        if (item.attribute("Image")) ci.image = item.attribute("Image").value();
        ci.price = item.attribute("Price") ? item.attribute("Price").as_llong() : 0;
        // JS `od` (item ctor): the `BonusPrice` (Ruby/crystal) cost.
        ci.bonus_price = sf2::data::xml_attr_int(item, "BonusPrice", 0);
        ci.level = sf2::data::xml_attr_int(item, "Level", 1);
        ci.has_level = static_cast<bool>(item.attribute("Level"));
        ci.weapon_damage = sf2::data::xml_attr_int(item, "WeaponDamage", 0);
        ci.body_defense = sf2::data::xml_attr_int(item, "BodyDefense", 0);
        ci.head_defense = sf2::data::xml_attr_int(item, "HeadDefense", 0);
        ci.unarmed_damage = sf2::data::xml_attr_int(item, "UnarmedDamage", 0);
        ci.magic_damage = sf2::data::xml_attr_int(item, "MagicDamage", 0);
        ci.delivery_sec = sf2::data::xml_attr_int(item, "DeliveryTime", 0);
        ci.delivery_coin = sf2::data::xml_attr_int(item, "MoneyDeliveryPrice", 0);
        ci.delivery_gems = sf2::data::xml_attr_int(item, "BonusDeliveryPrice", 0);
        // JS `xb(a.attributes.get("RecieveGold"/"RecieveBonus"))` (L164808/164852).
        ci.recieve_gold = sf2::data::xml_attr_int(item, "RecieveGold", 0);
        ci.recieve_bonus = sf2::data::xml_attr_int(item, "RecieveBonus", 0);
        ci.shop_hide = attr_bool_str(item.attribute("ShopHide").value());
        ci.hidden = attr_bool_str(item.attribute("Hidden").value());
        ci.paid_item =
            item.attribute("PaidItem") ? item.attribute("PaidItem").value() : "None";
        if (item.attribute("PaidItem")) ci.paid = true;
        // JS item ctor L321-327: `badge` (Badge), `bU` (ShopLabel), `Ms`
        // (AddPercent) and `Zz` (ConsumableProduct). `ns.j5` (L2308-2309)
        // reads all four for the shop-cell sale badge.
        if (item.attribute("Badge")) ci.badge = item.attribute("Badge").value();
        if (item.attribute("ShopLabel")) ci.shop_label = item.attribute("ShopLabel").value();
        ci.add_percent = sf2::data::xml_attr_int(item, "AddPercent", 0);
        ci.consumable_product =
            attr_bool_str(item.attribute("ConsumableProduct").value());
        // JS `this.S5 = u.ka(a.attributes.get("SingleTimeBuy"))` (L164197).
        ci.single_time_buy = attr_bool_str(item.attribute("SingleTimeBuy").value());
        // JS item ctor L164: `Hp` = `RealPrice` with its leading currency char
        // stripped (`J.substr(this.xr,1,null)`, first space token), else
        // `RealPriceConst`. `ICa()` (L169073) = `kc(this.Hp) > 1E-10` gates the
        // Ruby/IAP tab (`f5` case 5, L1177372) and the shop detail buy button
        // (L1156807). Only rows with a positive real price are listed/sold.
        {
            std::string rp;
            if (item.attribute("RealPrice")) {
                const std::string raw = item.attribute("RealPrice").value();
                rp = raw.empty() ? std::string() : raw.substr(1);
                const std::size_t sp = rp.find(' ');
                if (sp != std::string::npos) rp = rp.substr(0, sp);
            } else if (item.attribute("RealPriceConst")) {
                rp = item.attribute("RealPriceConst").value();
            }
            if (!rp.empty()) {
                try {
                    std::size_t pos = 0;
                    ci.has_real_price = std::stod(rp, &pos) > 1e-10;
                } catch (...) {
                }
            }
        }
        // JS item ctor L326-327: `this.Tg = UpgradeLevel` and
        // `this.D6 = <Upgrades Template>`; `dkb` L342 then walks the inline
        // `<Upgrades><Upgrade>` rows into `eB` (each via `wf.Qd`).
        ci.upgrade_level = sf2::data::xml_attr_int(item, "UpgradeLevel", 0);
        if (const pugi::xml_node ups = item.child("Upgrades")) {
            if (ups.attribute("Template")) {
                ci.upgrade_template = ups.attribute("Template").value();
            }
            for (const pugi::xml_node up : ups.children("Upgrade")) {
                UpgradeRow r;
                r.tc = sf2::data::xml_attr_int(up, "UpgradeLevel", 0);
                r.level = sf2::data::xml_attr_int(up, "Level", 0);
                r.price = up.attribute("Price") ? up.attribute("Price").as_llong() : 0;
                r.bonus_price = sf2::data::xml_attr_int(up, "BonusPrice", 0);
                r.milestone = sf2::data::xml_attr_int(up, "Milestone", 0);
                r.delivery_sec = sf2::data::xml_attr_int(up, "DeliveryTime", 0);
                r.delivery_gems = sf2::data::xml_attr_int(up, "BonusDeliveryPrice", 0);
                for (const ShopAttributeDef& def : shop_attribute_defs()) {
                    if (up.attribute(def.name.c_str())) {
                        r.attributes[def.name] = sf2::data::xml_attr_int(up, def.name.c_str(), 0);
                    }
                }
                ci.upgrades.push_back(std::move(r));
            }
            // `dkb` L342 sorts `eB` by `Tc` (`Wy` L354 = `pb(values.Tc,...)`).
            std::sort(ci.upgrades.begin(), ci.upgrades.end(),
                      [](const UpgradeRow& a, const UpgradeRow& b) { return a.tc < b.tc; });
        }
        // JS item ctor: `for(e of v.eo.attributes) { let f=a.attributes.get(e.name);
        // f!=null && this.attributes.set(e.name, u.I(f)) }` — the item's combat
        // stats, keyed by the `internal_settings.xml` attribute names.
        for (const ShopAttributeDef& def : shop_attribute_defs()) {
            if (item.attribute(def.name.c_str())) {
                ci.attributes[def.name] =
                    sf2::data::xml_attr_int(item, def.name.c_str(), 0);
            }
        }
        // `<Perks>` + `<Enchantments>` rows (JS `xe.Qd` be-entries, L1257):
        // perk name + `<Set>` overrides (numeric vs string by parse).
        for (const char* section : {"Perks", "Enchantments"}) {
            const pugi::xml_node sec = item.child(section);
            if (!sec) continue;
            const bool enchant = std::string(section) == "Enchantments";
            for (const pugi::xml_node perk : sec.children("Perk")) {
                if (!perk.attribute("Name")) continue;
                ItemPerkRef ref;
                ref.name = perk.attribute("Name").value();
                ref.enchant = enchant;
                const pugi::xml_node set = perk.child("Set");
                if (set) {
                    for (const pugi::xml_attribute a : set.attributes()) {
                        try {
                            std::size_t pos = 0;
                            const double d = std::stod(a.value(), &pos);
                            if (pos == std::string(a.value()).size()) {
                                ref.set_num[a.name()] = d;
                                continue;
                            }
                        } catch (...) {
                        }
                        ref.set_str[a.name()] = a.value();
                    }
                }
                ci.perks.push_back(std::move(ref));
            }
        }
        // Shop-offer definition (JS item ctor L162822-16733x). `mt.Mga`
        // L175260: `a.Yb != "Offer" ? a.Yb == "DailyOffer" : true` — i.e. the
        // `SubType` is "Offer" or "DailyOffer" (both carry `Type="RealMoneyItem"`,
        // `I.wk` L1272054).
        ci.is_offer = ci.subtype == "Offer" || ci.subtype == "DailyOffer";
        if (ci.is_offer) {
            ci.offer_kind = ci.subtype;
            if (item.attribute("Text")) ci.offer_text = item.attribute("Text").value();
            if (item.attribute("Description"))
                ci.offer_description = item.attribute("Description").value();
            if (item.attribute("ProfitImage"))
                ci.offer_profit_image = item.attribute("ProfitImage").value();
            if (item.attribute("ButtonImage"))
                ci.offer_button_image = item.attribute("ButtonImage").value();
            if (item.attribute("RealPrice"))
                ci.offer_real_price = item.attribute("RealPrice").value();
            if (item.attribute("FocusOnBuy"))
                ci.offer_focus_on_buy = item.attribute("FocusOnBuy").value();
            ci.offer_show_last_chance =
                attr_bool_str(item.attribute("ShowLastChance").value());
            ci.offer_duration = sf2::data::xml_attr_int(item, "Duration", 0);
            // `<OfferItems><Item Name=..>` -> `Ht` (Lt entries; the shipped
            // `AllItemsRecieved`/`Nga` paths read only the names).
            const pugi::xml_node offer_items = item.child("OfferItems");
            if (offer_items) {
                for (const pugi::xml_node oi : offer_items.children("Item")) {
                    if (oi.attribute("Name"))
                        ci.offer_items.push_back(oi.attribute("Name").value());
                }
            }
            // `<OfferConditions>` -> `CE` (leaf conditions; `And`/`Operator`
            // nested rows are kept as-is and never pass — no shipped use).
            const pugi::xml_node offer_conditions = item.child("OfferConditions");
            if (offer_conditions) {
                for (const pugi::xml_node oc : offer_conditions.children()) {
                    if (oc.type() != pugi::node_element) continue;
                    OfferCondition cond;
                    cond.kind = oc.name();
                    if (oc.attribute("Value1")) cond.value1 = oc.attribute("Value1").value();
                    if (oc.attribute("Value2")) cond.value2 = oc.attribute("Value2").value();
                    cond.invert = attr_bool_str(oc.attribute("Not").value());
                    ci.offer_conditions.push_back(std::move(cond));
                }
            }
        }
        // JS `kt.create` L176873 (the `it.PUa.create` gate `Lia` runs per
        // `<Items>` child): `rcb` drops a row whose non-empty `Labels` list
        // lacks "PAID" (`kt.mUa`), and `qcb` drops `Type="Free"` (`I.Bu`).
        // Both return null -> the row never enters `p.items.Xm`. The port
        // previously kept every `<Item>`, so `Unlimited_Energy`
        // (Type="Consumable", Labels="CHINAF2P") leaked into the RUBY tab.
        {
            bool labels_ok = true;
            if (!ci.labels.empty()) {
                labels_ok = false;
                std::size_t p = 0;
                for (;;) {
                    const std::size_t q = ci.labels.find('|', p);
                    const std::string tok =
                        ci.labels.substr(p, q == std::string::npos ? q : q - p);
                    if (tok == "PAID") {  // `a.includes(this.mUa)`
                        labels_ok = true;
                        break;
                    }
                    if (q == std::string::npos) break;
                    p = q + 1;
                }
            }
            if (!labels_ok || ci.type == "Free") continue;
        }
        out.push_back(std::move(ci));
    }
    return out;
}

// The shop-visible items (JS `Oa.f5` tab lists): the non-hidden, non-paid,
// gold-priced Weapon/Armor/Helm entries.
std::vector<CatalogItem> shop_items(const std::vector<CatalogItem>& all) {
    std::vector<CatalogItem> out;
    for (const CatalogItem& ci : all) {
        if (ci.type != kTypeWeapon && ci.type != kTypeArmor && ci.type != kTypeHelm) {
            continue;
        }
        if (ci.shop_hide || ci.hidden || ci.paid) {
            continue;
        }
        if (ci.price <= 0) {
            continue;
        }
        out.push_back(ci);
    }
    return out;
}

// `it.qkb` L164: each `<UpgradeList><Upgrades Name= ItemType=>` block with its
// `<Upgrade>` rows (`wf.Qd` L354). `S7a` L165 then finds a block by `Name`.
std::vector<UpgradeTemplate> parse_upgrade_list(const std::string& xml_text) {
    std::vector<UpgradeTemplate> out;
    sf2::data::xml_doc doc;
    doc.parse(xml_text);
    const pugi::xml_node root = doc.root().first_child();
    if (root == nullptr || std::string(root.name()) != "List") return out;
    const pugi::xml_node list = root.child("UpgradeList");
    if (!list) return out;
    for (const pugi::xml_node block : list.children("Upgrades")) {
        UpgradeTemplate t;
        if (block.attribute("Name")) t.name = block.attribute("Name").value();
        if (block.attribute("ItemType")) t.item_type = block.attribute("ItemType").value();
        for (const pugi::xml_node up : block.children("Upgrade")) {
            UpgradeRow r;
            r.tc = sf2::data::xml_attr_int(up, "UpgradeLevel", 0);
            r.level = sf2::data::xml_attr_int(up, "Level", 0);
            r.price = up.attribute("Price") ? up.attribute("Price").as_llong() : 0;
            r.bonus_price = sf2::data::xml_attr_int(up, "BonusPrice", 0);
            r.milestone = sf2::data::xml_attr_int(up, "Milestone", 0);
            r.delivery_sec = sf2::data::xml_attr_int(up, "DeliveryTime", 0);
            r.delivery_gems = sf2::data::xml_attr_int(up, "BonusDeliveryPrice", 0);
            for (const ShopAttributeDef& def : shop_attribute_defs()) {
                if (up.attribute(def.name.c_str())) {
                    r.attributes[def.name] = sf2::data::xml_attr_int(up, def.name.c_str(), 0);
                }
            }
            t.rows.push_back(std::move(r));
        }
        out.push_back(std::move(t));
    }
    return out;
}

// `it.S7a(item)` L165: `for(d of Eia) if(d.type == a.D6) return d; return null`.
const UpgradeTemplate* find_upgrade_template(const std::vector<UpgradeTemplate>& templates,
                                             const CatalogItem& item) {
    if (item.upgrade_template.empty()) return nullptr;  // `a.D6`
    for (const UpgradeTemplate& t : templates) {
        if (t.name == item.upgrade_template) return &t;
    }
    return nullptr;
}

// `item.zz(a,b)` L337: candidates = inline `eB` + template rows with
// `Tc > W8a()` (the max inline `Tc`), sorted by `Tc`; then keep those with
// (`!a || Tc > this.Tg`) and `level <= b` (`b` default 1E6).
std::vector<UpgradeRow> item_upgrade_candidates(const CatalogItem& item,
                                                const std::vector<UpgradeTemplate>& templates,
                                                bool only_above_tier) {
    std::vector<UpgradeRow> d = item.upgrades;  // `m.addRange(d, this.eB)`
    int max_inline = -2147483647 - 1;           // `W8a` L336 seed (INT_MIN)
    for (const UpgradeRow& r : item.upgrades) {
        if (r.tc > max_inline) max_inline = r.tc;
    }
    if (const UpgradeTemplate* t = find_upgrade_template(templates, item)) {
        for (const UpgradeRow& r : t->rows) {
            if (r.tc > max_inline) d.push_back(r);  // `h.values.Tc > e`
        }
    }
    std::sort(d.begin(), d.end(),
              [](const UpgradeRow& a, const UpgradeRow& b) { return a.tc < b.tc; });
    std::vector<UpgradeRow> c;
    for (const UpgradeRow& r : d) {
        if ((!only_above_tier || r.tc > item.upgrade_level) && r.level <= 1000000) {
            c.push_back(r);
        }
    }
    return c;
}

// `v.xIa.Gb(type)` (`bw` from `internal_settings.xml` `<OutdateLevels>`,
// `v.xIa.parse` @593527). `Gb` (L609451): return the first row whose `Type`
// list contains `type` (`cw.Xcb`), else row 0's value, else 0.
int shop_upgrade_level_cap(const std::string& type) {
    if (!g_tables_loaded) ensure_shop_tables_loaded();
    for (const OutdateLevel& d : g_outdate_levels) {
        for (const std::string& t : d.types) {
            if (t == type) return d.value;  // `cw.Xcb`
        }
    }
    return g_outdate_levels.empty() ? 0 : g_outdate_levels[0].value;
}

// `item.vu(a,b,c,d)` L340: `a` = player level, `b` = the entry tier (`Ce`).
// `c.G` = `Xv(f)` where `f` has `Tc == b`; `d.G` = `Xv(g or h)` — the next
// milestone row (`Og>0 && level >= b/100 + cap`) or the next non-milestone row.
ItemUpgradeState resolve_item_upgrade(const CatalogItem& item,
                                      const std::vector<UpgradeTemplate>& templates,
                                      int tier, int player_level) {
    ItemUpgradeState st;
    const std::vector<UpgradeRow> rows = item_upgrade_candidates(item, templates, false);
    st.has_rows = !rows.empty();  // `RB` = `ib.zz().length > 0`
    const int cap = shop_upgrade_level_cap(item.type);  // `k`
    const double l = static_cast<double>(tier) / 100.0;  // `l = b/100`
    const UpgradeRow* g = nullptr;  // milestone candidate (smallest Tc)
    const UpgradeRow* h = nullptr;  // non-milestone candidate (largest Tc)
    for (const UpgradeRow& q : rows) {
        const int r = q.tc;
        if (r == tier) {  // `r==b && (f=q)`
            st.current = q;
            st.has_current = true;
        }
        if (q.level <= player_level && r > tier) {  // `q.values.level<=a && r>b`
            if (q.milestone > 0) {
                if (static_cast<double>(q.level) >= l + cap &&
                    (g == nullptr || g->tc < r)) {
                    g = &q;
                }
            } else if (h == nullptr || h->tc > r) {
                h = &q;
            }
        }
    }
    if (g != nullptr) {  // `g!=null ? d.G=Xv(g) : h!=null && (d.G=Xv(h))`
        st.next = *g;
        st.has_next = true;
    } else if (h != nullptr) {
        st.next = *h;
        st.has_next = true;
    }
    st.maxed = st.has_rows && !st.has_next;  // `zN` L1260
    return st;
}

// Fills the runtime tables from an already-loaded `internal_settings.xml`
// (JS `v.eo.parse(a.A("Attributes"))` @591524 + `v.Ova.parse(a.A("BarScales"))`
// @593981). Idempotent; called once at config load.
void load_shop_tables_from_settings(const std::string& xml_text) {
    std::vector<ShopAttributeDef> defs;
    std::vector<ShopBarScale> scales;
    std::vector<OutdateLevel> outdates;
    parse_shop_tables_xml(xml_text, &defs, &scales, &outdates);
    g_attr_defs = std::move(defs);
    g_bar_scales = std::move(scales);
    g_outdate_levels = std::move(outdates);
    g_tables_loaded = true;
}

namespace {

// Deterministic FNV-1a over the table values (float/int raw bytes).
std::uint64_t fnv_mix(std::uint64_t h, const void* data, std::size_t n) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}
std::uint64_t table_checksum(const std::vector<ShopAttributeDef>& defs,
                             const std::vector<ShopBarScale>& scales) {
    std::uint64_t h = 1469598103934665603ULL;
    for (const ShopAttributeDef& d : defs) {
        h = fnv_mix(h, d.name.data(), d.name.size());
        h = fnv_mix(h, d.icon.data(), d.icon.size());
        h = fnv_mix(h, d.bar_scale.data(), d.bar_scale.size());
        const std::uint8_t flags = static_cast<std::uint8_t>(
            (d.hidden ? 1 : 0) | (d.shop_hidden ? 2 : 0) | (d.profile_hidden ? 4 : 0));
        h = fnv_mix(h, &flags, 1);
    }
    for (const ShopBarScale& s : scales) {
        h = fnv_mix(h, s.name.data(), s.name.size());
        h = fnv_mix(h, s.type.data(), s.type.size());
        h = fnv_mix(h, &s.power, sizeof(s.power));
        h = fnv_mix(h, &s.min, sizeof(s.min));
        for (const std::vector<ShopBarScaleLimit>* lim : {&s.attribute_limits, &s.item_limits}) {
            for (const ShopBarScaleLimit& l : *lim) {
                h = fnv_mix(h, &l.level_multiplier, sizeof(l.level_multiplier));
                h = fnv_mix(h, &l.shift, sizeof(l.shift));
                h = fnv_mix(h, &l.left_limit, sizeof(l.left_limit));
                h = fnv_mix(h, &l.right_limit, sizeof(l.right_limit));
                for (int lv : l.levels) h = fnv_mix(h, &lv, sizeof(lv));
            }
        }
    }
    return h;
}

} // namespace

// [probe] `--shop-tables-probe`: re-parse the shipped `internal_settings.xml`
// and assert the RUNTIME tables equal it (the JS `v.eo`/`v.Ova`). Logs counts
// + a value checksum; returns the mismatch count (0 = PASS).
int run_shop_tables_probe() {
    int fails = 0;
    std::string text;
    {
        std::ifstream in("reference/extracted/xml/res/internal_settings.xml",
                         std::ios::binary);
        if (!in) {
            std::fprintf(stdout, "[shoptables] FAIL open internal_settings.xml\n");
            return 1;
        }
        text.assign((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    }
    std::vector<ShopAttributeDef> xml_defs;
    std::vector<ShopBarScale> xml_scales;
    std::vector<OutdateLevel> xml_outdates;
    parse_shop_tables_xml(text, &xml_defs, &xml_scales, &xml_outdates);
    const std::vector<ShopAttributeDef>& rt_defs = shop_attribute_defs();
    const std::vector<ShopBarScale>& rt_scales = shop_bar_scales();
    if (!g_tables_loaded) ensure_shop_tables_loaded();
    const std::vector<OutdateLevel>& rt_outdates = g_outdate_levels;
    if (rt_outdates.size() != xml_outdates.size()) {
        std::fprintf(stdout, "[shoptables] OUTDATE COUNT %zu != %zu\n",
                     rt_outdates.size(), xml_outdates.size());
        ++fails;
    } else {
        for (std::size_t i = 0; i < xml_outdates.size(); ++i) {
            if (rt_outdates[i].value != xml_outdates[i].value ||
                rt_outdates[i].types != xml_outdates[i].types) {
                std::fprintf(stdout, "[shoptables] OUTDATE[%zu] MISMATCH\n", i);
                ++fails;
            }
        }
    }
    std::fprintf(stdout, "[shoptables] outdate rows=%zu\n", xml_outdates.size());
    std::size_t xml_limits = 0;
    for (const ShopBarScale& s : xml_scales)
        xml_limits += s.attribute_limits.size() + s.item_limits.size();
    std::size_t rt_limits = 0;
    for (const ShopBarScale& s : rt_scales)
        rt_limits += s.attribute_limits.size() + s.item_limits.size();
    const std::uint64_t rt_sum = table_checksum(rt_defs, rt_scales);
    const std::uint64_t xml_sum = table_checksum(xml_defs, xml_scales);
    std::fprintf(stdout,
                 "[shoptables] xml defs=%zu scales=%zu limits=%zu | runtime defs=%zu "
                 "scales=%zu limits=%zu\n",
                 xml_defs.size(), xml_scales.size(), xml_limits, rt_defs.size(),
                 rt_scales.size(), rt_limits);
    std::fprintf(stdout, "[shoptables] checksum runtime=%016llx xml=%016llx %s\n",
                 static_cast<unsigned long long>(rt_sum),
                 static_cast<unsigned long long>(xml_sum),
                 rt_sum == xml_sum ? "MATCH" : "DIFFER");
    std::fflush(stdout);
    if (rt_defs.size() != xml_defs.size()) {
        std::fprintf(stdout, "[shoptables] DEF COUNT %zu != %zu\n", rt_defs.size(),
                     xml_defs.size());
        ++fails;
    } else {
        for (std::size_t i = 0; i < xml_defs.size(); ++i) {
            const ShopAttributeDef& a = rt_defs[i];
            const ShopAttributeDef& b = xml_defs[i];
            if (a.name != b.name || a.icon != b.icon || a.bar_scale != b.bar_scale ||
                a.hidden != b.hidden || a.shop_hidden != b.shop_hidden ||
                a.profile_hidden != b.profile_hidden) {
                std::fprintf(stdout,
                             "[shoptables] DEF[%zu] %s: icon '%s'->'%s' bar '%s'->'%s' "
                             "hidden %d->%d shop %d->%d profile %d->%d\n",
                             i, b.name.c_str(), a.icon.c_str(), b.icon.c_str(),
                             a.bar_scale.c_str(), b.bar_scale.c_str(), a.hidden, b.hidden,
                             a.shop_hidden, b.shop_hidden, a.profile_hidden,
                             b.profile_hidden);
                ++fails;
            }
        }
    }
    if (rt_scales.size() != xml_scales.size()) {
        std::fprintf(stdout, "[shoptables] SCALE COUNT %zu != %zu\n", rt_scales.size(),
                     xml_scales.size());
        ++fails;
    } else {
        for (std::size_t i = 0; i < xml_scales.size(); ++i) {
            const ShopBarScale& a = rt_scales[i];
            const ShopBarScale& b = xml_scales[i];
            bool ok = a.name == b.name && a.type == b.type && a.power == b.power &&
                      a.min == b.min &&
                      a.attribute_limits.size() == b.attribute_limits.size() &&
                      a.item_limits.size() == b.item_limits.size();
            if (ok) {
                for (std::size_t k = 0; k < b.attribute_limits.size() && ok; ++k) {
                    const ShopBarScaleLimit& x = a.attribute_limits[k];
                    const ShopBarScaleLimit& y = b.attribute_limits[k];
                    ok = x.level_multiplier == y.level_multiplier && x.shift == y.shift &&
                         x.left_limit == y.left_limit && x.right_limit == y.right_limit &&
                         x.levels == y.levels;
                }
                for (std::size_t k = 0; k < b.item_limits.size() && ok; ++k) {
                    const ShopBarScaleLimit& x = a.item_limits[k];
                    const ShopBarScaleLimit& y = b.item_limits[k];
                    ok = x.level_multiplier == y.level_multiplier && x.shift == y.shift &&
                         x.left_limit == y.left_limit && x.right_limit == y.right_limit &&
                         x.levels == y.levels;
                }
            }
            if (!ok) {
                std::fprintf(stdout, "[shoptables] SCALE[%zu] '%s' MISMATCH\n", i,
                             b.name.c_str());
                ++fails;
            }
        }
    }
    std::fprintf(stdout, "[shoptables] RESULT %s (%d fail)\n", fails == 0 ? "PASS" : "FAIL",
                 fails);
    std::fflush(stdout);
    return fails;
}

} // namespace sf2::app
