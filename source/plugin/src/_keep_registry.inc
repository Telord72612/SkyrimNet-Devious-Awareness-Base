// ★ THE RESTRAINT REGISTRY (2026-08-24)
//
// User: "i want to make it easy for user to add gears OR spell to it… a .ini,
// where added gear name or ref id is added… one row for gears, one for
// action/arms, action/leg and action/all. and we should add blindness too."
//
// FIVE CATEGORIES, each answering one question about the BODY — because that
// is the distinction the SkyrimNet prompts act on, not what family a device
// belongs to:
//     gag   speech blocked      blind  sight blocked
//     arms  arms/hands unusable legs   walking cut to a shuffle
//     all   cannot act at all   (rendered INSTEAD of arms+legs)
//
// Devious Devices' own gear is built in below and needs no INI line. The INI
// only ADDS — it can never remove a built-in, so a broken INI degrades to
// "DD works, extras ignored" rather than to silence.
//
// ⚠ WHY THIS IS C++ AND NOT PAPYRUS. The prompts need this per NPC per render.
// Papyrus cannot parse an INI without MiscUtil string surgery on every call,
// and the templates' own native `worn_has_keyword` can only ask about keywords
// — it cannot match a NAME or a FormID, and it cannot see a spell at all. One
// native answering the whole question keeps a single source of truth.
//
// ⚠ FORMS RESOLVE AT kDataLoaded, NEVER BEFORE. `0x12345~Plugin.esp` needs the
// data handler and the load order; parsing at plugin-load silently yields
// nullptr for every one of them.
// ═══════════════════════════════════════════════════════════════════════════
enum RestraintCat : int { kCatGag = 0, kCatBlind, kCatArms, kCatLegs, kCatAll, kCatCount };

const char* CatName(int c)
{
    switch (c) {
    case kCatGag:   return "gag";
    case kCatBlind: return "blind";
    case kCatArms:  return "arms";
    case kCatLegs:  return "legs";
    case kCatAll:   return "all";
    default:        return "?";
    }
}

// DD (and the two other frameworks this load order carries) — always active.
// A keyword EditorID that does not exist in a given load order simply never
// matches, so listing NG-only and ZaZ-only names here costs nothing.
const char* const kBuiltinKw[kCatCount][12] = {
    /* gag   */ { "zad_DeviousGag", "zad_DeviousGagLarge", "zad_DeviousGagPanel",
                  "zad_DeviousGagBit", "zad_DeviousGagRing", "zad_DeviousGagTape",
                  "zad_DeviousGagInflatable", "zbfWornGag", "DOMWornGag", nullptr },
    /* blind */ { "zad_DeviousBlindfold", "zad_DeviousHood", "zbfWornBlindfold", nullptr },
    /* arms  */ { "zad_DeviousHeavyBondage", "zad_DeviousArmbinder", "zad_DeviousArmbinderElbow",
                  "zad_DeviousYoke", "zad_DeviousYokeBB", "zad_DeviousElbowTie",
                  "zad_DeviousBondageMittens", "zad_DeviousGloves", "zad_DeviousCuffsFront",
                  nullptr },
    /* legs  */ { "zad_DeviousHobbleSkirt", "zad_DeviousHobbleSkirtRelaxed",
                  "zad_DeviousAnkleShackles", "zad_DeviousPonyGear", "zad_DeviousBoots",
                  nullptr },
    /* all   */ { "zad_DeviousStraitJacket", "zad_DeviousPetSuit", nullptr },
};

struct RestraintRule {
    std::string   nameNeedle;      // already lowercased; empty = unused
    std::string   keyword;         // keyword EditorID; empty = unused
    std::uint32_t formId = 0;      // resolved FormID; 0 = unused
};
std::vector<RestraintRule> g_rules[kCatCount];
bool g_rulesLoaded = false;

std::string LowerOf(const char* s)
{
    std::string o = s ? s : "";
    for (auto& c : o) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    return o;
}

// Every ARMOR she is wearing, one call. DD's rendered half is in here, which is
// the half that carries the class keywords (report 23 §30). `fn` returns false
// to stop early.
template <class F>
void ForEachWorn(RE::Actor* a, F&& fn)
{
    if (!a) return;
    for (int b = 0; b < 32; ++b) {
        auto* w = a->GetWornArmor(
            static_cast<RE::BGSBipedObjectForm::BipedObjectSlot>(1u << b));
        if (w && !fn(w)) return;
    }
}

// ⚠ `Actor::WornHasKeyword` is PAPYRUS-ONLY - there is no such member in
// CommonLibVR. Walk her worn armor and ask each piece instead, which is what
// the Papyrus native does anyway. (One slot can report the same ARMO several
// times; that costs a redundant compare and nothing else.)
bool WornHasKw(RE::Actor* a, const char* editorId)
{
    if (!a || !editorId || !editorId[0]) return false;
    bool hit = false;
    ForEachWorn(a, [&](RE::TESObjectARMO* w) {
        auto* kwf = w->As<RE::BGSKeywordForm>();
        if (!kwf) return true;
        for (std::uint32_t i = 0; i < kwf->GetNumKeywords(); ++i) {
            auto kw = kwf->GetKeywordAt(i);
            if (!kw.has_value() || !kw.value()) continue;
            const char* id = kw.value()->GetFormEditorID();
            if (id && _stricmp(id, editorId) == 0) { hit = true; return false; }
        }
        return true;
    });
    return hit;
}

// `outName` gets a display name when one can be found; a match with no name is
// still a match (DD's worn half has no FULL record — §30), so the caller falls
// back to a generic noun rather than treating "" as "not restrained".
bool MatchesRule(RE::Actor* a, const RestraintRule& r, std::string* outName)
{
    if (!r.keyword.empty() && WornHasKw(a, r.keyword.c_str())) return true;

    bool hit = false;
    if (r.formId) {
        // worn armor by form
        ForEachWorn(a, [&](RE::TESObjectARMO* w) {
            if (w->GetFormID() == r.formId) {
                hit = true;
                if (outName && w->GetName()) *outName = w->GetName();
                return false;
            }
            return true;
        });
        if (hit) return true;
        // …or a spell she carries, or an effect running on her right now
        if (auto* f = RE::TESForm::LookupByID(r.formId)) {
            if (auto* sp = f->As<RE::SpellItem>()) {
                if (a->HasSpell(sp)) {
                    if (outName && sp->GetName()) *outName = sp->GetName();
                    return true;
                }
            }
            if (auto* mg = f->As<RE::EffectSetting>()) {
                if (a->AsMagicTarget() && a->AsMagicTarget()->HasMagicEffect(mg)) {
                    if (outName && mg->GetName()) *outName = mg->GetName();
                    return true;
                }
            }
        }
    }

    if (!r.nameNeedle.empty()) {
        ForEachWorn(a, [&](RE::TESObjectARMO* w) {
            const char* nm = w->GetName();
            if (nm && nm[0] && LowerOf(nm).find(r.nameNeedle) != std::string::npos) {
                hit = true;
                if (outName) *outName = nm;
                return false;
            }
            return true;
        });
        if (hit) return true;
    }
    return false;
}

bool ActorRestrained(RE::Actor* a, int cat, std::string* outName)
{
    if (!a || cat < 0 || cat >= kCatCount) return false;
    for (int i = 0; i < 12 && kBuiltinKw[cat][i]; ++i) {
        if (WornHasKw(a, kBuiltinKw[cat][i])) return true;
    }
    for (const auto& r : g_rules[cat]) {
        if (MatchesRule(a, r, outName)) return true;
    }
    return false;
}

// "0x0012345~Some Mod.esp" -> a resolved FormID, or 0. Accepts a bare hex id
// too (interpreted against the AddOn's own — i.e. nothing — so it is rejected
// loudly rather than silently pointing at a random Skyrim.esm record).
std::uint32_t ParseFormRef(const char* v)
{
    const char* tilde = std::strchr(v, '~');
    if (!tilde) {
        logger::info("[RESTRAINT] form '{}' has no ~Plugin.esp - ignored "
                     "(a bare FormID cannot be resolved safely)", v);
        return 0;
    }
    std::string idPart(v, tilde - v);
    std::string plugin(tilde + 1);
    while (!idPart.empty() && (idPart.back() == ' ' || idPart.back() == '\t')) idPart.pop_back();
    while (!plugin.empty() && (plugin.back() == ' ' || plugin.back() == '\t' ||
                               plugin.back() == '\r' || plugin.back() == '\n')) plugin.pop_back();
    const std::uint32_t raw = static_cast<std::uint32_t>(std::strtoul(idPart.c_str(), nullptr, 16));
    auto* dh = RE::TESDataHandler::GetSingleton();
    if (!dh) return 0;
    auto* form = dh->LookupForm(raw & 0x00FFFFFF, plugin);
    if (!form) {
        logger::info("[RESTRAINT] form '{}' did not resolve - is '{}' in the load order?",
                     v, plugin);
        return 0;
    }
    return form->GetFormID();
}

void LoadRestraintIni()
{
    for (auto& v : g_rules) v.clear();
    g_rulesLoaded = true;

    const char* path = "Data/SKSE/Plugins/VRTE_DDZaZ_Restraints.ini";
    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") != 0 || !f) {
        logger::info("[RESTRAINT] no VRTE_DDZaZ_Restraints.ini - built-in DD/ZaZ coverage only");
        return;
    }
    int cat = -1, added = 0, bad = 0;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p || *p == '#' || *p == ';' || *p == '\n' || *p == '\r') continue;

        if (*p == '[') {                                   // section
            char name[32] = {};
            if (std::sscanf(p, " [ %31[A-Za-z] ]", name) == 1) {
                cat = -1;
                for (int c = 0; c < kCatCount; ++c) {
                    if (_stricmp(name, CatName(c)) == 0) { cat = c; break; }
                }
                if (cat < 0) {
                    logger::info("[RESTRAINT] unknown section '[{}]' - lines under it are ignored "
                                 "(valid: gag, blind, arms, legs, all)", name);
                }
            }
            continue;
        }

        char key[32] = {}, val[400] = {};
        if (std::sscanf(p, " %31[A-Za-z] = %399[^\r\n]", key, val) != 2) { ++bad; continue; }
        for (int i = static_cast<int>(std::strlen(val)) - 1;
             i >= 0 && (val[i] == ' ' || val[i] == '\t'); --i) val[i] = '\0';
        if (cat < 0) {
            logger::info("[RESTRAINT] '{} = {}' appears before any [section] - ignored", key, val);
            ++bad;
            continue;
        }
        RestraintRule r{};
        if      (_stricmp(key, "name") == 0)    r.nameNeedle = LowerOf(val);
        else if (_stricmp(key, "keyword") == 0) r.keyword    = val;
        else if (_stricmp(key, "form") == 0 || _stricmp(key, "spell") == 0)
                                                r.formId     = ParseFormRef(val);
        else {
            logger::info("[RESTRAINT] unknown key '{}' - use name, form or keyword", key);
            ++bad;
            continue;
        }
        if (r.nameNeedle.empty() && r.keyword.empty() && !r.formId) { ++bad; continue; }
        g_rules[cat].push_back(std::move(r));
        ++added;
    }
    fclose(f);
    logger::info("[RESTRAINT] VRTE_DDZaZ_Restraints.ini: {} rule(s) loaded, {} rejected "
                 "| gag={} blind={} arms={} legs={} all={}",
                 added, bad, g_rules[kCatGag].size(), g_rules[kCatBlind].size(),
                 g_rules[kCatArms].size(), g_rules[kCatLegs].size(), g_rules[kCatAll].size());
}

} // namespace

namespace DeviceEquip {

// ── the restraint registry, for main.cpp's Papyrus natives ─────────────────
void LoadRestraints() { ::LoadRestraintIni(); }

bool IsRestrained(RE::Actor* a, int cat)
{
    if (!g_rulesLoaded) ::LoadRestraintIni();
    return ::ActorRestrained(a, cat, nullptr);
}

// The restraining item's display name, or "" when it has none. ⚠ "" does NOT
// mean unrestrained — DD's worn half carries no FULL record, so the caller must
// ask IsRestrained separately and fall back to a generic noun.
std::string RestraintName(RE::Actor* a, int cat)
{
    if (!g_rulesLoaded) ::LoadRestraintIni();
    std::string nm;
    if (!::ActorRestrained(a, cat, &nm)) return "";
    return nm;
}

// ═══════════════════════════════════════════════════════════════════════════
// ★ THE PPB HANDOVER GATE (2026-08-27).
//
// The hand-gesture device layer - equip, eject, undress, plug pull - MOVED to
// PPB. That is the right home for it: the orifice model is PPB's, the capsules
// are PPB's, and the motion that uses them belongs beside them. This add-on
// keeps what is genuinely DD: the wear clock, the strain model, the worn-device
// report, the restraint registry, the SkyrimNet decorators, the effect edges
// (vibration / shock / the magicka-reactive cast), and the TESEquipEvent sink
// that turns any device state change into an event.
//
// ⛔ BUT TWO DETECTORS FOR ONE GESTURE IS WORSE THAN EITHER ALONE. Both would
// register a HIGGS frame callback, both would see the same dwell, both would
// dispatch an equip, and both would emit VRTE_DDZaZ_DeviceEquipped - so one
// placement would narrate twice and DD would be asked to equip the same device
// twice. Deleting this file's gesture outright is the eventual answer, but it
// cannot be done on a guess about PPB's build state: if this add-on stands down
// while PPB has not yet shipped, the feature is simply gone.
//
// ★ So the handover is decided at runtime by the ONE artifact PPB must ship for
// its gesture to work at all. PPB dispatches to Papyrus class `PPB_DeviceEquip`
// (DeviceGesture.cpp: DoEquip and DoUnequipDevice both DispatchStaticCall to
// it). If that script class is loaded, PPB's gesture layer is live and this one
// stands down. If it is not, PPB has not shipped yet and this one carries on.
// Race-free in both orders, and it needs no INI and no user action.
bool PpbOwnsGestures()
{
    auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
    if (!vm) return false;
    RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> info;
    return vm->GetScriptObjectType(RE::BSFixedString("PPB_DeviceEquip"), info) &&
           info != nullptr;
}