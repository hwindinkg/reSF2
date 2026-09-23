// Item catalog implementation — parses res/list.xml <Items> (JS `it` g="5A").

#include "app/item_catalog.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "xml_doc.hpp"

namespace sf2::app {

namespace {

// The JS `I` item-type enum values (L1271963).
constexpr const char* kTypeWeapon = "Weapon";
constexpr const char* kTypeArmor = "Armor";
constexpr const char* kTypeHelm = "Helm";

bool attr_bool_str(const char* v) { return v != nullptr && std::string(v) == "1"; }

} // namespace

// `internal_settings.xml` `<Attributes>` (L21744-23450), file order. The JS
// `ow.parse` (L615263) reads Name/Icon/Hidden + `BarScale` (`gp.bP`, L615845);
// `ms`/`fi` (L2274-2275) consume them in this order.
const std::vector<ShopAttributeDef>& shop_attribute_defs() {
    static const std::vector<ShopAttributeDef> kDefs = {
        {"HeadDefense", "head_armor", "HeadDefense", false},
        {"BodyDefense", "body_armor", "BodyDefense", false},
        {"UnarmedDamage", "unarmed_attack", "BodyDefense", false},  // BarScale=BodyDefense
        {"WeaponDamage", "weapon_attack", "WeaponDamage", false},
        {"RangedDamage", "ranged_attack", "RangedDamage", false},
        {"MagicDamage", "magic_attack", "MagicDamage", false},
        {"CriticalChance", "critical_chance", "Chance", true},
        {"CriticalRating", "critical_chance", "Enchantment", false},  // ShopHidden, not Hidden
        {"BlockDamageFactor", "", "", true},
        {"DamageFactor", "", "", true},
        {"RangedQuantity", "ranged_quantity", "RangedQuantity", true},
        {"CriticalDamage", "", "", true},
        {"MagicInitialCharge", "", "", true},
        {"MagicPainRecharge", "", "", true},
        {"MagicDamageRecharge", "", "", true},
        {"RegenerationRate", "", "", true},
        {"Lifesteal", "", "", true},
        {"ShockCriticalHitChance", "", "", true},
        {"ShockHeadHitChance", "", "", true},
        {"EnchantmentResistance", "", "", true},
    };
    return kDefs;
}

namespace {

// `internal_settings.xml` `<BarScales>` — the `v.Ova` `Mv` table (JS L604556;
// `Mv.parse` reads Name/Type/Power/Min + the `<AttributeLimits>`/`<ItemLimits>`
// rows via `Nv.kBa` L659xxx). Each row is `{LevelMultiplier, Shift, LeftLimit,
// RightLimit, Level[]}` (`Ew`, defaults -1/-1/-1/-1/empty). `v.BP`
// (`<DamageDoublingRange Value="10"/>`) is the `Exp` formula divisor.
constexpr float kDamageDoublingRange = 10.0f;

const std::vector<ShopBarScale>& shop_bar_scales() {
    static const std::vector<ShopBarScale> kScales = {
        {"WeaponDamage", "Exp", 0.5f, 0.03f,
         { // AttributeLimits
            {10.0f, 10, -1, -1, {1}},
            {27.0f, -7, -1, -1, {2}},
            {35.0f, -16, -1, -1, {3, 4, 5}},
            {41.0f, -46, -1, -1, {6}},
            {35.0f, -10, -1, -1, {7, 8, 9, 10, 11}},
            {41.0f, -76, -1, -1, {12}},
            {35.0f, -4, -1, -1, {13, 14, 15, 16, 17}},
            {41.0f, -106, -1, -1, {18}},
            {35.0f, 2, -1, -1, {19, 20, 21, 22, 23}},
            {41.0f, -136, -1, -1, {24}},
            {35.0f, 8, -1, -1, {25, 26, 27, 28, 29}},
            {41.0f, -166, -1, -1, {30}},
            {35.0f, 14, -1, -1, {31, 32, 33, 34, 35}},
            {41.0f, -196, -1, -1, {36, 37, 38, 39, 40, 41, 42, 43}},
            {35.0f, 62, -1, -1, {44, 45, 46, 47, 48, 49, 50, 51}},
            {41.0f, -244, -1, -1, {52}},
            {41.0f, -244, -1, -1, {}},
         },
         { // ItemLimits
            {25.0f, -20, -1, -1, {1}},
            {25.0f, -30, -1, -1, {2}},
            {25.0f, -21, -1, -1, {3, 4, 5}},
            {31.0f, -51, -1, -1, {6}},
            {25.0f, -15, -1, -1, {7, 8, 9, 10, 11}},
            {31.0f, -81, -1, -1, {12}},
            {25.0f, -9, -1, -1, {13, 14, 15, 16, 17}},
            {31.0f, -111, -1, -1, {18}},
            {25.0f, -3, -1, -1, {19, 20, 21, 22, 23}},
            {31.0f, -141, -1, -1, {24}},
            {25.0f, 3, -1, -1, {25, 26, 27, 28, 29}},
            {31.0f, -171, -1, -1, {30}},
            {25.0f, 9, -1, -1, {31, 32, 33, 34, 35}},
            {31.0f, -201, -1, -1, {36, 37, 38, 39, 40, 41, 42, 43}},
            {25.0f, 57, -1, -1, {44, 45, 46, 47, 48, 49, 50, 51}},
            {31.0f, -249, -1, -1, {52}},
            {31.0f, -249, -1, -1, {}},
         }},
        {"UnarmedDamage", "Exp", 0.5f, 0.03f,
         { // AttributeLimits
            {10.0f, 10, -1, -1, {1}},
            {27.0f, -7, -1, -1, {2}},
            {35.0f, -16, -1, -1, {3, 4, 5}},
            {41.0f, -46, -1, -1, {6}},
            {35.0f, -10, -1, -1, {7, 8, 9, 10, 11}},
            {41.0f, -76, -1, -1, {12}},
            {35.0f, -4, -1, -1, {13, 14, 15, 16, 17}},
            {41.0f, -106, -1, -1, {18}},
            {35.0f, 2, -1, -1, {19, 20, 21, 22, 23}},
            {41.0f, -136, -1, -1, {24}},
            {35.0f, 8, -1, -1, {25, 26, 27, 28, 29}},
            {41.0f, -166, -1, -1, {30}},
            {35.0f, 14, -1, -1, {31, 32, 33, 34, 35}},
            {41.0f, -196, -1, -1, {36, 37, 38, 39, 40, 41, 42, 43}},
            {35.0f, 62, -1, -1, {44, 45, 46, 47, 48, 49, 50, 51}},
            {41.0f, -244, -1, -1, {52}},
            {41.0f, -244, -1, -1, {}},
         },
         { // ItemLimits
            {25.0f, -20, -1, -1, {1}},
            {27.0f, -32, -1, -1, {2}},
            {25.0f, -21, -1, -1, {3, 4, 5}},
            {31.0f, -51, -1, -1, {6}},
            {25.0f, -15, -1, -1, {7, 8, 9, 10, 11}},
            {31.0f, -81, -1, -1, {12}},
            {25.0f, -9, -1, -1, {13, 14, 15, 16, 17}},
            {31.0f, -111, -1, -1, {18}},
            {25.0f, -3, -1, -1, {19, 20, 21, 22, 23}},
            {31.0f, -141, -1, -1, {24}},
            {25.0f, 3, -1, -1, {25, 26, 27, 28, 29}},
            {31.0f, -171, -1, -1, {30}},
            {25.0f, 9, -1, -1, {31, 32, 33, 34, 35}},
            {31.0f, -201, -1, -1, {36, 37, 38, 39, 40, 41, 42, 43}},
            {25.0f, 57, -1, -1, {44, 45, 46, 47, 48, 49, 50, 51}},
            {31.0f, -249, -1, -1, {52}},
            {31.0f, -249, -1, -1, {}},
         }},
        {"BodyDefense", "Exp", 0.5f, 0.03f,
         { // AttributeLimits
            {10.0f, 10, -1, -1, {1}},
            {27.0f, -7, -1, -1, {2}},
            {35.0f, -16, -1, -1, {3, 4, 5}},
            {41.0f, -46, -1, -1, {6}},
            {35.0f, -10, -1, -1, {7, 8, 9, 10, 11}},
            {41.0f, -76, -1, -1, {12}},
            {35.0f, -4, -1, -1, {13, 14, 15, 16, 17}},
            {41.0f, -106, -1, -1, {18}},
            {35.0f, 2, -1, -1, {19, 20, 21, 22, 23}},
            {41.0f, -136, -1, -1, {24}},
            {35.0f, 8, -1, -1, {25, 26, 27, 28, 29}},
            {41.0f, -166, -1, -1, {30}},
            {35.0f, 14, -1, -1, {31, 32, 33, 34, 35}},
            {41.0f, -196, -1, -1, {36, 37, 38, 39, 40, 41, 42, 43}},
            {35.0f, 62, -1, -1, {44, 45, 46, 47, 48, 49, 50, 51}},
            {41.0f, -244, -1, -1, {52}},
            {41.0f, -244, -1, -1, {}},
         },
         { // ItemLimits
            {25.0f, -20, -1, -1, {1}},
            {27.0f, -32, -1, -1, {2}},
            {25.0f, -21, -1, -1, {3, 4, 5}},
            {31.0f, -51, -1, -1, {6}},
            {25.0f, -15, -1, -1, {7, 8, 9, 10, 11}},
            {31.0f, -81, -1, -1, {12}},
            {25.0f, -9, -1, -1, {13, 14, 15, 16, 17}},
            {31.0f, -111, -1, -1, {18}},
            {25.0f, -3, -1, -1, {19, 20, 21, 22, 23}},
            {31.0f, -141, -1, -1, {24}},
            {25.0f, 3, -1, -1, {25, 26, 27, 28, 29}},
            {31.0f, -171, -1, -1, {30}},
            {25.0f, 9, -1, -1, {31, 32, 33, 34, 35}},
            {31.0f, -201, -1, -1, {36, 37, 38, 39, 40, 41, 42, 43}},
            {25.0f, 57, -1, -1, {44, 45, 46, 47, 48, 49, 50, 51}},
            {31.0f, -249, -1, -1, {52}},
            {31.0f, -249, -1, -1, {}},
         }},
        {"HeadDefense", "Exp", 0.5f, 0.03f,
         { // AttributeLimits
            {10.0f, 10, -1, -1, {1}},
            {27.0f, -7, -1, -1, {2}},
            {35.0f, -16, -1, -1, {3, 4, 5}},
            {41.0f, -46, -1, -1, {6}},
            {35.0f, -10, -1, -1, {7, 8, 9, 10, 11}},
            {41.0f, -76, -1, -1, {12}},
            {35.0f, -4, -1, -1, {13, 14, 15, 16, 17}},
            {41.0f, -106, -1, -1, {18}},
            {35.0f, 2, -1, -1, {19, 20, 21, 22, 23}},
            {41.0f, -136, -1, -1, {24}},
            {35.0f, 8, -1, -1, {25, 26, 27, 28, 29}},
            {41.0f, -166, -1, -1, {30}},
            {35.0f, 14, -1, -1, {31, 32, 33, 34, 35}},
            {41.0f, -196, -1, -1, {36, 37, 38, 39, 40, 41, 42, 43}},
            {35.0f, 62, -1, -1, {44, 45, 46, 47, 48, 49, 50, 51}},
            {41.0f, -244, -1, -1, {52}},
            {41.0f, -244, -1, -1, {}},
         },
         { // ItemLimits
            {25.0f, -15, -1, -1, {1}},
            {13.0f, -13, -1, -1, {2}},
            {25.0f, -23, -1, -1, {3, 4, 5}},
            {31.0f, -53, -1, -1, {6}},
            {25.0f, -17, -1, -1, {7, 8, 9, 10, 11}},
            {31.0f, -83, -1, -1, {12}},
            {25.0f, -11, -1, -1, {13, 14, 15, 16, 17}},
            {31.0f, -113, -1, -1, {18}},
            {25.0f, -5, -1, -1, {19, 20, 21, 22, 23}},
            {31.0f, -143, -1, -1, {24}},
            {25.0f, 1, -1, -1, {25, 26, 27, 28, 29}},
            {31.0f, -173, -1, -1, {30}},
            {25.0f, 7, -1, -1, {31, 32, 33, 34, 35}},
            {31.0f, -203, -1, -1, {36, 37, 38, 39, 40, 41, 42, 43}},
            {25.0f, 55, -1, -1, {44, 45, 46, 47, 48, 49, 50, 51}},
            {31.0f, -251, -1, -1, {52}},
            {31.0f, -251, -1, -1, {}},
         }},
        {"RangedDamage", "Exp", 0.5f, 0.03f,
         { // AttributeLimits
            {10.0f, 10, -1, -1, {1}},
            {27.0f, -7, -1, -1, {2}},
            {35.0f, -16, -1, -1, {3, 4, 5}},
            {41.0f, -46, -1, -1, {6}},
            {35.0f, -10, -1, -1, {7, 8, 9, 10, 11}},
            {41.0f, -76, -1, -1, {12}},
            {35.0f, -4, -1, -1, {13, 14, 15, 16, 17}},
            {41.0f, -106, -1, -1, {18}},
            {35.0f, 2, -1, -1, {19, 20, 21, 22, 23}},
            {41.0f, -136, -1, -1, {24}},
            {35.0f, 8, -1, -1, {25, 26, 27, 28, 29}},
            {41.0f, -166, -1, -1, {30}},
            {35.0f, 14, -1, -1, {31, 32, 33, 34, 35}},
            {41.0f, -196, -1, -1, {36, 37, 38, 39, 40, 41, 42, 43}},
            {35.0f, 62, -1, -1, {44, 45, 46, 47, 48, 49, 50, 51}},
            {41.0f, -244, -1, -1, {52}},
            {41.0f, -244, -1, -1, {}},
         },
         { // ItemLimits
            {25.0f, -15, -1, -1, {1}},
            {25.0f, -25, -1, -1, {2}},
            {25.0f, -16, -1, -1, {3, 4, 5}},
            {31.0f, -46, -1, -1, {6}},
            {25.0f, -10, -1, -1, {7, 8, 9, 10, 11}},
            {31.0f, -76, -1, -1, {12}},
            {25.0f, -4, -1, -1, {13, 14, 15, 16, 17}},
            {31.0f, -106, -1, -1, {18}},
            {25.0f, 2, -1, -1, {19, 20, 21, 22, 23}},
            {31.0f, -136, -1, -1, {24}},
            {25.0f, 8, -1, -1, {25, 26, 27, 28, 29}},
            {31.0f, -166, -1, -1, {30}},
            {25.0f, 14, -1, -1, {31, 32, 33, 34, 35}},
            {31.0f, -196, -1, -1, {36, 37, 38, 39, 40, 41, 42, 43}},
            {25.0f, 62, -1, -1, {44, 45, 46, 47, 48, 49, 50, 51}},
            {31.0f, -244, -1, -1, {52}},
            {31.0f, -244, -1, -1, {}},
         }},
        {"MagicDamage", "Exp", 0.5f, 0.03f,
         { // AttributeLimits
            {10.0f, 10, -1, -1, {1}},
            {27.0f, -7, -1, -1, {2}},
            {35.0f, -16, -1, -1, {3, 4, 5}},
            {41.0f, -46, -1, -1, {6}},
            {35.0f, -10, -1, -1, {7, 8, 9, 10, 11}},
            {41.0f, -76, -1, -1, {12}},
            {35.0f, -4, -1, -1, {13, 14, 15, 16, 17}},
            {41.0f, -106, -1, -1, {18}},
            {35.0f, 2, -1, -1, {19, 20, 21, 22, 23}},
            {41.0f, -136, -1, -1, {24}},
            {35.0f, 8, -1, -1, {25, 26, 27, 28, 29}},
            {41.0f, -166, -1, -1, {30}},
            {35.0f, 14, -1, -1, {31, 32, 33, 34, 35}},
            {41.0f, -196, -1, -1, {36, 37, 38, 39, 40, 41, 42, 43}},
            {35.0f, 62, -1, -1, {44, 45, 46, 47, 48, 49, 50, 51}},
            {41.0f, -244, -1, -1, {52}},
            {41.0f, -244, -1, -1, {}},
         },
         { // ItemLimits
            {25.0f, -15, -1, -1, {1}},
            {25.0f, -25, -1, -1, {2}},
            {25.0f, -16, -1, -1, {3, 4, 5}},
            {31.0f, -46, -1, -1, {6}},
            {25.0f, -10, -1, -1, {7, 8, 9, 10, 11}},
            {31.0f, -76, -1, -1, {12}},
            {25.0f, -4, -1, -1, {13, 14, 15, 16, 17}},
            {31.0f, -106, -1, -1, {18}},
            {25.0f, 2, -1, -1, {19, 20, 21, 22, 23}},
            {31.0f, -136, -1, -1, {24}},
            {25.0f, 8, -1, -1, {25, 26, 27, 28, 29}},
            {31.0f, -166, -1, -1, {30}},
            {25.0f, 14, -1, -1, {31, 32, 33, 34, 35}},
            {31.0f, -196, -1, -1, {36, 37, 38, 39, 40, 41, 42, 43}},
            {25.0f, 62, -1, -1, {44, 45, 46, 47, 48, 49, 50, 51}},
            {31.0f, -244, -1, -1, {52}},
            {31.0f, -244, -1, -1, {}},
         }},
        {"Chance", "Linear", 1.0f, 0.0f,
         { // AttributeLimits
            {-1.0f, -1, 0, 10000, {}},
         },
         { // ItemLimits
            {-1.0f, -1, 0, 10000, {}},
         }},
        {"Enchantment", "Exp", 0.128f, 0.03f,
         { // AttributeLimits
            {10.0f, 50, -1, -1, {1}},
            {27.0f, 31, -1, -1, {2}},
            {35.0f, 0, -1, -1, {3, 4, 5, 6}},
            {35.0f, 6, -1, -1, {7, 8, 9, 10, 11, 12}},
            {35.0f, 12, -1, -1, {13, 14, 15, 16, 17, 18}},
            {35.0f, 18, -1, -1, {19, 20, 21, 22, 23, 24}},
            {35.0f, 24, -1, -1, {25, 26, 27, 28, 29, 30}},
            {35.0f, 30, -1, -1, {31, 32, 33, 34, 35, 36}},
            {41.0f, -186, -1, -1, {37, 38, 39, 40, 41, 42, 43}},
            {35.0f, 72, -1, -1, {44, 45, 46, 47, 48, 49, 50, 51, 52}},
            {35.0f, 0, -1, -1, {}},
         },
         { // ItemLimits
            {10.0f, 50, -1, -1, {1}},
            {27.0f, 31, -1, -1, {2}},
            {35.0f, 0, -1, -1, {3, 4, 5, 6}},
            {35.0f, 6, -1, -1, {7, 8, 9, 10, 11, 12}},
            {35.0f, 12, -1, -1, {13, 14, 15, 16, 17, 18}},
            {35.0f, 18, -1, -1, {19, 20, 21, 22, 23, 24}},
            {35.0f, 24, -1, -1, {25, 26, 27, 28, 29, 30}},
            {35.0f, 30, -1, -1, {31, 32, 33, 34, 35, 36}},
            {41.0f, -186, -1, -1, {37, 38, 39, 40, 41, 42, 43}},
            {35.0f, 72, -1, -1, {44, 45, 46, 47, 48, 49, 50, 51, 52}},
            {35.0f, 0, -1, -1, {}},
         }},
    };
    return kScales;
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
float shop_attribute_bar_fill(const char* bar_scale, int value, int player_level) {
    const ShopBarScale* bs = nullptr;
    if (bar_scale != nullptr && bar_scale[0] != '\0') {
        for (const ShopBarScale& s : shop_bar_scales()) {
            if (std::string(s.name) == bar_scale) {
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
                     (static_cast<float>(value) - baseline) * power / kDamageDoublingRange);
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
        if (item.attribute("Model")) ci.model = item.attribute("Model").value();
        if (item.attribute("Image")) ci.image = item.attribute("Image").value();
        ci.price = sf2::data::xml_attr_int(item, "Price", 0);
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
        // JS item ctor: `for(e of v.eo.attributes) { let f=a.attributes.get(e.name);
        // f!=null && this.attributes.set(e.name, u.I(f)) }` — the item's combat
        // stats, keyed by the `internal_settings.xml` attribute names.
        for (const ShopAttributeDef& def : shop_attribute_defs()) {
            if (item.attribute(def.name)) {
                ci.attributes[def.name] =
                    sf2::data::xml_attr_int(item, def.name, 0);
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

} // namespace sf2::app
