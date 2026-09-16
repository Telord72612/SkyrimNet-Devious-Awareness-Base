#include "WornDevices.h"
#include "DeviceEquip.h"
#include "PromptState.h"
#include "TaskRelay.h"   // 1.3.6

#include <filesystem>
#include <system_error>

namespace logger = SKSE::log;

namespace WornDevices {
namespace {

// ─────────────────────────────────────────────────────────────────────────────
// small helpers
// ─────────────────────────────────────────────────────────────────────────────

// Local copy of DeviceEquip's walk, deliberately: that file is 3,000 lines of
// working gesture code and this feature has no reason to touch it. One slot can
// report the same ARMO several times; that costs a redundant compare.
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

bool HasKw(RE::TESForm* f, const char* editorId)
{
    auto* kwf = f ? f->As<RE::BGSKeywordForm>() : nullptr;
    if (!kwf || !editorId) return false;
    for (std::uint32_t i = 0; i < kwf->GetNumKeywords(); ++i) {
        auto kw = kwf->GetKeywordAt(i);
        if (!kw.has_value() || !kw.value()) continue;
        const char* id = kw.value()->GetFormEditorID();
        if (id && _stricmp(id, editorId) == 0) return true;
    }
    return false;
}

// ★ MANNEQUINS ARE NEVER PEOPLE (the user, 2026-09-10: "Mannequin must always be
// excluded"). A HearthFires mannequin is a real Actor: it passes IsDead / IsDisabled /
// IsChild and can have line of sight. In the 2026-09-10 key test VRTE handed one a
// witness line (SkyrimNet: "Could not determine name for actor 0x30009AC" =
// BYOHHouse1InteriorRoom02Part125Mannequin2ndFloor, base BYOHHouseMannequin). Two tests,
// because either alone misses a case: ManakinRace (10760A:Skyrim.esm) catches every
// vanilla-race mannequin whatever a mod calls it, and a NAMELESS actor is one SkyrimNet
// cannot address anyway (Femmequins.esp strips that NPC's name).
// Here it keeps a mannequin from counting as an audience (StrangerWatching) and from
// being a blind NPC's nearest body (the mis-target).
bool IsMannequin(RE::Actor* o)
{
    if (!o) return true;
    auto* rf      = RE::TESForm::LookupByID(0x0010760A);          // ManakinRace, Skyrim.esm
    auto* manakin = rf ? rf->As<RE::TESRace>() : nullptr;
    if (manakin && o->GetRace() == manakin) return true;
    const char* n = o->GetDisplayFullName();
    return !n || !n[0];
}

// Case-insensitive substring. Device MATERIAL and soulgem TIER are encoded in
// the display name and nowhere else — "Plug (Black Soulgem) (Anal)" — which is
// the same place DD, ZaZ and every content mod put it. Verified against the
// shipping records rather than assumed (Report 26 §3 and the plug census).
bool NameHas(const char* name, const char* needle)
{
    if (!name || !needle || !name[0] || !needle[0]) return false;
    const std::size_t n = std::strlen(needle);
    for (const char* p = name; *p; ++p)
        if (_strnicmp(p, needle, n) == 0) return true;
    return false;
}

// "zad_DeviousHeavyBondage" -> "HeavyBondage". Empty when the keyword is not a
// class marker. Mirrors DeviceEquip::ClsSuffix so the two can never disagree.
// How specific a class token is. LOWER wins. A device commonly carries several
// class keywords and only one of them describes the thing as a whole.
int ClassRank(const char* k)
{
    // Plugs first: the equip sink's insertion/removal tier depends on this
    // answer, and a plug that loses to a co-keyword is narrated as ordinary gear.
    if (_strnicmp(k, "Plug", 4) == 0) return 0;
    // ⛔ A SPECIFIC VARIANT MUST BEAT ITS GENERIC PARENT (2026-08-27). Each of
    // these co-occurs with the parent it refines, so at equal rank the winner
    // was decided by keyword storage order - i.e. by FormID, i.e. by chance.
    // ⚠ "Hood" IS HERE FOR A DIFFERENT REASON THAN THE OTHERS, and it is the
    // counterexample to the longer-token tie-break below. A hood carries
    // zad_DeviousHood AND zad_DeviousBlindfold (and often zad_DeviousGag), all at
    // the same rank - and "Blindfold" is the LONGER token, so it won. 39 of the
    // 113 hoods in this load order were being reported as "covers the eyes",
    // losing the fact that a hood also dulls sound.
    // ★ The length heuristic works for PREFIX pairs (Gag/GagRing), where the
    // longer token really is the refinement. It does not work between unrelated
    // tokens where one device SUBSUMES another: a hood is a blindfold plus more,
    // and the more encompassing device has to win regardless of spelling.
    static const char* variant[] = { "YokeFront", "YokeBB", "ArmbinderElbow", "Hood" };
    for (auto* v : variant) if (_stricmp(k, v) == 0) return 2;

    // Structural garments that HOLD the other things: a body harness carries
    // Belt/Collar/PiercingsNipple keywords for the parts it includes.
    static const char* whole[] = { "Harness", "PetSuit",
                                   "StraitJacket", "Corset" };
    for (auto* w : whole) if (_stricmp(k, w) == 0) return 1;

    // Generic components that are frequently a PART of something larger.
    static const char* part[] = { "Belt", "Collar", "PiercingsNipple",
                                  "PiercingsVaginal", "Bra", "Gloves", "Boots" };
    for (auto* p : part) if (_stricmp(k, p) == 0) return 5;

    // ⛔ "Suit" IS NOT A SHAPE, AND RANKING IT 1 WAS A REAL BUG (2026-08-27).
    // zad_DeviousSuit (02AFA3, Devious Devices - Assets.esm) is DD's generic
    // "this garment covers the whole body" tag - 163 ARMO records carry it,
    // straitjackets and box binders and the Iron Breast Yoke among them. Assets
    // .esm loads before Integration.esm, where zad_DeviousStraitJacket (060A46)
    // lives, so on a record carrying both, Suit sits EARLIER in the keyword
    // array. With both at rank 1 the tie broke to storage order and Suit won -
    // so a straitjacket resolved to class "Suit", which is in NO strain tier,
    // and got no wear-clock strain at all.
    // Rank 4: loses to every class that names a MECHANISM, still beats the
    // rank-5 component tags, so a catsuit that also carries Belt reads as a suit.
    if (_stricmp(k, "Suit") == 0) return 4;

    // ⛔ AND NEITHER IS HeavyBondage. It is DD's CAPABILITY flag - the one
    // zadLibs.IsBound() tests - carried by every armbinder, yoke, straitjacket
    // and box binder alike. It says what a device DOES, not what it IS, so it
    // must only win when nothing more specific is present. Rank 6 puts it last.
    // ⚠ It must rank BELOW Suit, not equal: at an equal rank the tie would break
    // to load order and resolve the same wrong way this comment exists to fix.
    if (_stricmp(k, "HeavyBondage") == 0) return 6;

    // ⛔ AND NEITHER IS PonyGear (2026-08-27, the user on device #49): "pony gear
    // fit that description, if they are all on. the collar is just that, a collar.
    // part of a set, that one is more about completing the look of the pony set."
    //
    // It is a SET marker, and only 3 devices in this load order carry it - a
    // collar, a pair of boots and an elbow armbinder - each of which ALSO carries
    // its real class. At rank 1 it beat all three, so a pony COLLAR was reported
    // as "forces an upright posture and a high step", which is the boots' doing,
    // not the collar's. Rank 6 puts it below even the rank-5 component tags, so
    // each piece now reads as the piece it is.
    //
    // ⚠ That leaves ClassLine's "PonyGear" row reachable only by a device
    // carrying PonyGear and nothing else. None do today. The row is kept rather
    // than deleted because a content mod could ship exactly that, and an
    // unmatched class is DROPPED from the block entirely rather than degraded.
    if (_stricmp(k, "PonyGear") == 0) return 6;

    return 3;                                   // an ordinary standalone class
}

// ═══════════════════════════════════════════════════════════════════════════
// ⛔ THE MIS-TAGGED BLINDFOLDS (2026-09-08, found in VR: Sofia in a plain
// blindfold was roleplaying DEAF, and the user called it).
//
// FIVE Devious Devices records are named "... Blocking Blindfold", render a
// BLINDFOLD mesh, and yet carry `zad_DeviousHood` alongside
// `zad_DeviousBlindfold`. That second keyword did two things, both wrong:
//   1. ClassRank ranks Hood ABOVE Blindfold on purpose (a hood really is a
//      blindfold plus more - it fixed 39 of 113 hoods that were reporting only
//      "covers the eyes"). So these classed as Hood, lost their own name in the
//      worn block, and were described as "dulling sight AND SOUND".
//   2. The deaf row keys on bare `zad_DeviousHood`, so the wearer was told she
//      could not hear. Measured in one session: 69 renders carried the hood
//      line and 30 carried the deaf instruction.
//
// ★ THE GROUND TRUTH IS THE MESH, as it is for the collar rules above. These
// render `blindfold01.nif` / `BlindfEbonite.nif`; real hoods render
// `old_rubber_hood.nif`, `GasMask.nif`, `hoodExtreme*.nif`.
//
// ⛔ AND THE OBVIOUS TEST IS A TRAP. Two genuine hoods are called
// `hoodExtremeBlindLeather01.nif` and `hoodExtremeBlindEbonite01.nif`, so a
// "mesh contains blind" check would misclassify SIX REAL HOODS - recreating
// the exact defect the Hood rank was added to fix. The basename must START
// with "blindf".
//
// ⚠ The ARMATURE mesh, not the world model: `worldModels` is the dropped
// `_go.nif`, which is not what she is wearing.
// ═══════════════════════════════════════════════════════════════════════════
bool WornMeshIsBlindfold(RE::TESObjectARMO* w)
{
    if (!w) return false;
    for (auto* aa : w->armorAddons) {
        if (!aa) continue;
        for (int s = 0; s < 2; ++s) {                 // female then male
            const char* p = aa->bipedModels[s].GetModel();
            if (!p || !p[0]) continue;
            const char* b = p;                        // -> basename
            for (const char* q = p; *q; ++q)
                if (*q == '\\' || *q == '/') b = q + 1;
            if (_strnicmp(b, "blindf", 6) == 0) return true;
        }
    }
    return false;
}

// ★ 2026-09-12: does any worn armature mesh path contain `needle`? The generic
// form of WornMeshIsBlindfold. It exists because several records this AddOn must
// recognise carry NO display name on either half (Devious Lore's pony gags and
// tails) or NO site keyword at all (ZaZ's Shibari) - the mesh is the only signal
// every one of them has. ⚠ Only key on a mesh proven unique to its family by the
// census; a shared mesh drags every device that uses it along.
bool ContainsNoCase(const char* hay, const char* needle);   // defined below; ClassOf needs it here

bool WornMeshHas(RE::TESObjectARMO* w, const char* needle)
{
    if (!w || !needle || !needle[0]) return false;
    for (auto* aa : w->armorAddons) {
        if (!aa) continue;
        for (int s = 0; s < 2; ++s) {                 // female then male
            const char* p = aa->bipedModels[s].GetModel();
            if (p && ContainsNoCase(p, needle)) return true;
        }
    }
    return false;
}

// ═══ JOB B - THE KEYWORD-LESS DEVICES (2026-09-13, the user: "just add it") ═══════════════
// 23 restraint items carry NO framework keyword at all - no zad_Lockable, no zad_Devious*, no
// zbfWornDevice, no DOMWornDevice - so IsCoveredDevice dropped them and the LLM was told nothing.
// The user's scope (handover 36 §10): "basic awareness for the wearer and the surrounding NPC is
// enough" - a class so the worn block and the observer view can speak; NO effects, NO climax, NO
// animation ("Pama controls them").
// ★ ADDRESSED BY RECORD (plugin + local FormID), because there is nothing else to key on: the
// names are generic ("ropes", "Noose", "shackles") and ARMO EditorIDs are not loaded at runtime.
// A plugin that is not in the load order simply resolves nothing. Classes are the ones the user
// reviewed on the visualiser's "would be" preview, except where a ruling said otherwise (1545
// "shackles, behind the back").
struct KeywordlessDevice {
    const char*   plugin;
    std::uint32_t local;
    const char*   cls;
    bool          rope;        // Pama's nooses are rope without saying "rope" anywhere
    bool          handsBack;   // 1545 only
};
const KeywordlessDevice kKeywordless[] = {
    // Deviously Accessible - rows 1472, 1473, 1495 ("just a collar, chafing 1-2") and the three
    // fetish pieces 1484, 1485, 1496 + the Dibellan harness 1497 ("good as-is")
    { "DeviouslyAccessible.esp", 0x102391, "Collar",  false, false },   // dwp_apocollarcharged
    { "DeviouslyAccessible.esp", 0x102390, "Collar",  false, false },   // dwp_apocollardark
    { "DeviouslyAccessible.esp", 0x001DE9, "Collar",  false, false },   // Neck Brace
    { "DeviouslyAccessible.esp", 0x0F71B4, "Corset",  false, false },   // Corset with garters
    { "DeviouslyAccessible.esp", 0x015B71, "Collar",  false, false },   // Your Collar
    { "DeviouslyAccessible.esp", 0x0012E1, "Corset",  false, false },   // Fashionable Corset
    { "DeviouslyAccessible.esp", 0x0222D6, "Harness", true,  false },   // Loose Dibellan Rope Harness
    // Pama furniture set, rows 1520-1533 - "generic shackles / gag rules", awareness only
    { "PamaFurnitureScr.esp", 0x027647, "Gag",      false, false },     // apple
    { "PamaFurnitureScr.esp", 0x017DBF, "Collar",   true,  false },     // Noose
    { "PamaFurnitureScr.esp", 0x056D6D, "Collar",   true,  false },     // Noose
    { "PamaFurnitureScr.esp", 0x059519, "ArmCuffs", false, false },     // pama_CuffsMetalLowRust
    { "PamaFurnitureScr.esp", 0x025069, "ArmCuffs", false, false },     // pama_CuffsMetalRedRust
    { "PamaFurnitureScr.esp", 0x0084DE, "ArmCuffs", true,  false },     // ropes
    { "PamaFurnitureScr.esp", 0x0084DF, "ArmCuffs", true,  false },     // ropes
    { "PamaFurnitureScr.esp", 0x05951B, "GagTape",  false, false },     // Cloth gag
    { "PamaFurnitureScr.esp", 0x05BCC0, "GagPanel", false, false },     // Leather gag
    { "PamaFurnitureScr.esp", 0x034337, "Collar",   true,  false },     // Noose (lethal)
    { "PamaFurnitureScr.esp", 0x065B3A, "Collar",   true,  false },     // Noose (lethal)
    { "PamaFurnitureScr.esp", 0x056D78, "ArmCuffs", true,  false },     // ropes
    { "PamaFurnitureScr.esp", 0x02F252, "ArmCuffs", true,  false },     // ropes
    { "PamaFurnitureScr.esp", 0x030281, "ArmCuffs", false, false },     // shackles
    // Dark Desires - row 1545 "shackles, behind the back", row 1551 "blindfold"
    { "DarkDesiresCircleOfLust.esp", 0x86FF5A, "ArmCuffs",  false, true  },  // Hand Cuffs Backside Black
    { "DarkDesiresCircleOfLust.esp", 0x31B2BA, "Blindfold", false, false },  // Blindfold of Desire
};
// Resolved FormID -> row. Written ONCE at kDataLoaded (Install, before any sink is attached) and
// only read afterwards, so it needs no lock.
std::unordered_map<std::uint32_t, const KeywordlessDevice*> g_keywordless;

void ResolveKeywordless()
{
    auto* dh = RE::TESDataHandler::GetSingleton();
    if (!dh) return;
    int found = 0;
    for (const auto& k : kKeywordless) {
        auto* f = dh->LookupForm(k.local, k.plugin);
        if (f && f->As<RE::TESObjectARMO>()) { g_keywordless[f->GetFormID()] = &k; ++found; }
    }
    logger::info("[WORN] keyword-less devices covered by record: {} of {} resolved "
                 "(a missing plugin resolves nothing)", found,
                 sizeof kKeywordless / sizeof kKeywordless[0]);
}

const KeywordlessDevice* KeywordlessOf(RE::TESObjectARMO* w)
{
    if (!w || g_keywordless.empty()) return nullptr;
    auto it = g_keywordless.find(w->GetFormID());
    return it == g_keywordless.end() ? nullptr : it->second;
}

void ClassOf(RE::TESObjectARMO* w, char* out, std::size_t cap)
{
    if (!out || !cap) return;
    out[0] = '\0';
    // Job B: a keyword-less device has nothing below to read - its class comes from its record.
    if (const auto* kl = KeywordlessOf(w)) { std::snprintf(out, cap, "%s", kl->cls); return; }
    int         bestRank = 99;
    std::size_t bestLen  = 0;
    auto* kwf = w ? w->As<RE::BGSKeywordForm>() : nullptr;
    if (!kwf) return;
    for (std::uint32_t i = 0; i < kwf->GetNumKeywords(); ++i) {
        auto kw = kwf->GetKeywordAt(i);
        if (!kw.has_value() || !kw.value()) continue;
        const char* id = kw.value()->GetFormEditorID();
        if (!id) continue;
        const char* p = std::strstr(id, "Devious");
        if (!p) continue;
        const char* k = p + 7;
        if (!k[0] || _stricmp(k, "Device") == 0) continue;
        // ⛔ NOT "first real class wins" any more (2026-08-26). That walked the
        // keyword array in STORAGE order, and a census of the live load order
        // found it mislabelling 23 of 175 rendered harnesses: several list
        // Belt, Collar or PiercingsNipple before Harness, so a body harness came
        // back as "Collar" (wrong strain line) or "Belt" (no wear clock at all).
        // DeviceEquip.cpp:196-198 documents the same defect in its own scanner.
        // Rank decides instead: the most SPECIFIC class wins, storage order only
        // breaks ties.
        // ⛔ AND ON A TIE, THE LONGER TOKEN WINS (2026-08-27). This is the third
        // time the same defect has been found, so it is fixed generically rather
        // than by listing another exception:
        //   Suit    beat StraitJacket  (Assets 02AFA3 < Integration 060A46)
        //   Yoke    beat YokeFront     (fixed by hand at rank 2)
        //   Gag     beat GagRing       (Assets 007EB8 < Integration 08C854)
        // The pattern is structural, not coincidental: DD keeps the GENERIC class
        // keywords in Assets.esm and the SPECIFIC ones in Integration.esm or
        // Expansion.esm, so the generic always sits earlier in a record's keyword
        // array and always won the tie. Verified on zad_gag01ring_scriptInstance,
        // which carries zad_DeviousGag AND zad_DeviousGagRing - every ring, bit,
        // tape, large and inflatable gag was reporting as a plain "Gag".
        //
        // ★ Where one class token is a PREFIX of another, the longer is the more
        // specific by construction - GagRing, PlugAnal, YokeFront, ArmbinderElbow,
        // CuffsFront. Preferring length on a tie resolves the whole family at once.
        // ⛔ 2026-09-08: a "Hood" keyword on a device that RENDERS A BLINDFOLD is
        // DD's tagging error, not a hood. Skipping the token here lets Blindfold
        // win on its own rank, so the device keeps its name, its "covers the
        // eyes" line, and its wearer keeps her hearing. See WornMeshIsBlindfold.
        if (_stricmp(k, "Hood") == 0 && WornMeshIsBlindfold(w)) continue;

        const int rank = ClassRank(k);
        const std::size_t klen = std::strlen(k);
        if (out[0] && (rank > bestRank ||
                       (rank == bestRank && klen <= bestLen))) continue;
        bestRank = rank;
        bestLen  = klen;
        std::snprintf(out, cap, "%s", k);
    }

    // ★ A COLLAR WITH A NIPPLE CHAIN IS A COLLAR (2026-09-13, the user on #299: "change it").
    // #299 "Iron Collar with Chained Nipple Clamps" and its rusty twin #322 "Rusty Iron Collar with
    // Nipple Chain" carry zad_DeviousCollar AND zad_DeviousPiercingsNipple, and the piercing wins the
    // rank - so the clamp curve, the Internal layer and the piercing line. The name-keyed
    // ReclassByName fixed Report only, and only when the name had been pushed; NoteAftermath and
    // WearCredit never reclassed at all, so #299 came off on the 5-minute CLAMP curve. Keyed on the
    // keyword pair HERE, every path agrees with no name needed. Census: exactly these two resolve
    // PiercingsNipple with a collar keyword (the chain harnesses win as Harness and are untouched).
    if (_stricmp(out, "PiercingsNipple") == 0 && HasKw(w, "zad_DeviousCollar"))
        std::snprintf(out, cap, "%s", "Collar");

    // ═══ THE SECOND RESOLVER (2026-08-30) - report 29 §0.6 step 1, built on the
    // user's "fix it". A ZaZ or DoM record carries no zad_Devious* class, so the
    // walk above yields nothing; its OWN site vocabulary maps onto the class
    // tokens the whole machine already speaks. Specific before generic, always.
    if (!out[0]) {
        auto has = [&](const char* k2) { return HasKw(w, k2); };
        const auto m = static_cast<std::uint32_t>(w->GetSlotMask().underlying());
        const char* t = nullptr;
        // plugs first - the sink's tier decision hangs on it (DoM's pair)
        if      (has("DOMWornPlugVaginal"))                 t = "PlugVaginal";
        else if (has("DOMWornPlugAnal"))                    t = "PlugAnal";
        // ⛔ the ball family: nine ZaZ GENITAL devices mis-keyworded as gags -
        // slot 52 separates them 9/9 (§0.6b). They are CLAMP-mechanism gear
        // (the user: "Definitely for pain"), so they take the Clamps token.
        else if (has("zbfWornGag") && (m & (1u << 22)))     t = "Clamps";
        // ★ the breast ropes (§0.6c): "Same as the scrotum one, but for women.
        // Pain scale for sure." Clamp mechanism - compression of soft tissue -
        // and the §0.6c note rules the clamp tier is reused unchanged.
        else if (has("zbfWornBreastRope"))                  t = "Clamps";
        // the binder poses - all behind-the-back arm binders
        else if (has("zbfAnimHandsArmbinder") || has("zbfAnimHandsBoxTied") ||
                 has("zbfAnimHandsFullyRopedArms") ||
                 has("zbfAnimHandsAndNeckVerticallyPoled") ||
                 has("zbfAnimHandsHandsTiedToNeck") ||
                 has("DOMWornArmbinder"))                   t = "Armbinder";
        else if (has("zbfWornElbows"))                      t = "ElbowTie";
        else if (has("zbfWornFiddle"))                      t = "YokeFront";
        // ★ 2026-09-13 (the user on rows 1265/1266 "Smooth / Coarse Ropes Hands Around Neck": "It's a
        // wrist rope shackle type"). ZaZ's RopeShackles set - a wrist rope plus a neck rope, pose
        // ZazAPOA017 "Hands Around Neck". It fell to the bare zbfWornWrist row (ArmCuffs) and its
        // NoSprint effect made RestraintOf report the LEGS. A wrist shackle binds the wrists together.
        else if (has("zbfAnimHandsAroundNeck"))             t = "CuffsFront";
        else if (has("zbfWornYoke") || has("zbfWornCrossPole") ||
                 has("DOMWornYoke"))                        t = "Yoke";
        // the ZaZ straitjacket has no pose keyword - NoFighting on a body slot
        else if (has("zbfEffectNoFighting") && (m & (1u << 2))) t = "StraitJacket";
        // head gear: hood BEFORE the blindfold it usually also carries
        else if (has("zbfWornHood"))                        t = "Hood";
        else if (has("zbfWornGag") || has("DOMWornGag"))    t = "Gag";
        else if (has("zbfWornBlindfold") || has("DOMWornBlindfold")) t = "Blindfold";
        else if (has("zbfWornBelt"))                        t = "Belt";
        else if (has("zbfWornBra"))                         t = "Bra";
        else if (has("zbfWornPiercingNipple"))              t = "PiercingsNipple";
        else if (has("zbfWornCollar") || has("DOMWornCollar")) t = "Collar";
        else if (has("DOMWornCuffsFront") || has("DOMWornCuffsCrossed")) t = "CuffsFront";
        else if (has("zbfWornWrist") || has("DOMWornWrist")) t = "ArmCuffs";
        else if (has("zbfWornAnkles") || has("DOMWornAnkle")) t = "LegCuffs";
        else if (has("zbfWornWaist"))                       t = "Harness";
        // ★ 2026-09-12: ZaZ's Smooth Rope Shibari (zbfBodyBondageShibari,
        // visualiser row 1288). The user: "a full body chest harness made of rope,
        // same rules as a leather harness but with rope". It carries only
        // zbfWornDevice / ManualRemovable / Sliceable - NO site keyword - so no row
        // above can see it and it had no class at all.
        // ⛔ Keyed HERE, in ClassOf, on its unique mesh (Shibari_1.nif, 1 device in
        // the census), NOT in ReclassByName: that runs on only 2 of the ~12 class
        // paths and skips Report entirely, so the LLM would have gone on hearing
        // nothing. Rope follows automatically - IsRopeDevice reads the name.
        else if (WornMeshHas(w, "Shibari"))                 t = "Harness";
        if (t) std::snprintf(out, cap, "%s", t);
    }
}


// ★★ IS THIS A COVERED DEVICE? (2026-08-30, the incorporation.) The old gate -
// zad_Lockable || zad_DeviousPlug - excluded all 202 ZaZ wearables, all 18 DoM
// records, and even DD-classed records that lack zad_Lockable (the Devious
// Lore Seer set among them). The user: "fix it". A device is anything carrying
// a zad_Devious* class, ZaZ's zbfWornDevice, or DoM's DOMWornDevice - which
// keeps the 23 ZaZ marker records (no zbfWornDevice) and ordinary armor out.
bool IsCoveredDevice(RE::TESObjectARMO* w)
{
    if (!w) return false;
    if (KeywordlessOf(w)) return true;                  // Job B (2026-09-13), by record
    if (HasKw(w, "zad_Lockable") || HasKw(w, "zad_DeviousPlug")) return true;
    if (HasKw(w, "zbfWornDevice") || HasKw(w, "DOMWornDevice"))  return true;
    auto* kwf = w->As<RE::BGSKeywordForm>();
    if (!kwf) return false;
    for (std::uint32_t i = 0; i < kwf->GetNumKeywords(); ++i) {
        auto kw = kwf->GetKeywordAt(i);
        if (!kw.has_value() || !kw.value()) continue;
        const char* id = kw.value()->GetFormEditorID();
        if (id && _strnicmp(id, "zad_Devious", 11) == 0) return true;
    }
    return false;
}

// ★ Strip authoring noise from a display name before anyone reads it aloud:
// 39 ZaZ records are named "zbf Ankle Iron 2 Black" and would print RAW.
void CleanDeviceName(std::string& nm)
{
    if (nm.rfind("zbf ", 0) == 0) nm.erase(0, 4);
    const auto p = nm.find(" 2 ");
    if (p != std::string::npos) nm.erase(p, 2);   // "Ankle Iron 2 Black" -> "Ankle Iron Black"
}

float GameDaysNow()
{
    // 0x39 is the vanilla GameDaysPassed global — the same clock DD's own
    // DeviceEquippedAt uses, so our elapsed figures are directly comparable to
    // its lock timers.
    auto* g = RE::TESForm::LookupByID<RE::TESGlobal>(0x00000039);
    return g ? g->value : 0.0f;
}

// Load-order-safe lookup. ⚠ NEVER TESForm::LookupByEditorID for these: SSE/VR
// discard most EditorIDs and a nullptr would silently disable the whole effect
// path with no error (report 23 §36 §4, which cost a build to learn).
RE::TESForm* DdForm(std::uint32_t rawId, const char* plugin)
{
    auto* dh = RE::TESDataHandler::GetSingleton();
    return dh ? dh->LookupForm(rawId, plugin) : nullptr;
}

constexpr const char* kIntegration = "Devious Devices - Integration.esm";

RE::TESFaction* VibratorFaction()
{
    static RE::TESFaction* f = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (auto* raw = DdForm(0x029568, kIntegration)) f = raw->As<RE::TESFaction>();
        if (!f) logger::info("[WORN] zadVibratorFaction did not resolve - "
                             "vibration edges will not be reported.");
    }
    return f;
}

// A CACHE, PUSHED IN - AND THE PREVIOUS VERSION OF THIS WAS DEAD CODE.
//
// It read the rank of the `sla_Arousal` faction, on the reasonable-looking
// assumption that SexLab Aroused still keeps exposure there. In THIS load order
// arousal comes from OSL Aroused (Arousal Reborn), whose Papyrus layer is a stub
// over a native DLL: it writes `slaNakedFaction` and `slaGenderPreference` and
// NEVER `sla_Arousal`. GetFactionRank on a non-member returns -1, so this
// returned -1 for every actor and the `if (ar < 0) return;` below meant the
// shock path and the climax detection NEVER RAN AT ALL. Verified rather than
// assumed: nothing in the load order writes that rank.
//
// The real API, OSLArousedNative.GetArousalNoSideEffects, is a Papyrus global
// native and unreachable from here. But OSL Aroused raises
// `OSLA_ActorArousalUpdated` with the ACTOR as `sender` and the new value as
// numArg - a push, not a poll. The quest script feeds it in through NoteArousal.
// ⚠ THE ONE LOCK for every cross-thread map in this file: g_arousal, g_names,
// g_on, g_fx, g_gestureClaim, g_menuClaim and g_pendingOff. Declared THIS early
// because g_arousal, just below, is the first of them. VM natives run on the
// VM thread, the engine sinks on the main thread, ClaimGesture on the render
// thread - any two can interleave.
// ⛔ g_fx AND g_arousal WERE NOT UNDER IT until 2026-08-27, while the comment
// at the old declaration site claimed they were. An unordered_map operator[]
// INSERTS, and an insert that rehashes while another thread walks a bucket is
// a hard crash with nothing in any log pointing here - the identical defect
// class already fixed twice, for g_names and for g_gestureClaim.
std::mutex g_mtx;

std::unordered_map<std::uint32_t, int> g_arousal;

int ArousalOf(RE::Actor* a)
{
    if (!a) return -1;
    // ⛔ Read here on the main thread (the sinks), written by NoteArousal on
    // the VM thread - unguarded since birth, the same rehash race as g_fx.
    std::scoped_lock lk(g_mtx);
    auto it = g_arousal.find(a->GetFormID());
    return it == g_arousal.end() ? -1 : it->second;
}

// Every shock device she is wearing, counted - not just the first one found.
//
// ★ THE USER'S RULE (2026-08-26): "shock is shock, so no real escalation for
// that one, more like intensity according to how many device is worn that do
// the shock. make them all fire together when they do, just bring the chance
// down, but scale with the amount of device."
// So: ONE event, never one per device; the chance scales with the count and the
// per-device rate is low; and the count IS the intensity.
//
// ⚠ ALL FOUR SHOCK DEVICES IN DD ARE PLUGS OR PIERCINGS - there is no shock
// collar. Verified against the load order: only zad_plugShocker,
// zadx_plugShockSoulgemVag/An and the two zadx_piercing*ShockSoul records carry
// zad_EffectShocking. Anything else claiming a shock is a content mod's own.
struct ShockScan {
    int count = 0;    // how many shock devices are worn (periodic + on-full together)
    int kind  = 0;    // 2 = periodic; 3 chaos retired (1.2.5)
    int onFull = 0;   // ★ 1.2.9: how many carry zad_EffectShockOnFullArousal (the Black Soulgem plugs)
    // WHERE it lands, as a mask: 1 plug (inside), 2 nipple piercings,
    // 4 intimate piercing. The user asked for the shock to be described by site
    // and by how many went at once, so the site has to travel with the event -
    // a bool could only ever have said "inside or not".
    int sites = 0;
};

ShockScan ScanShock(RE::Actor* a)
{
    ShockScan r;
    if (!a) return r;
    std::vector<std::uint32_t> seen;
    ForEachWorn(a, [&](RE::TESObjectARMO* w) {
        const std::uint32_t fid = w->GetFormID();
        if (std::find(seen.begin(), seen.end(), fid) != seen.end()) return true;
        seen.push_back(fid);

        // ★ ONLY the zad_EffectShocking family is a jolt device (the user, 2026-09-13: "the only
        // jolt plug are the shock one"). Devious Devices agrees in BEHAVIOUR: zadEventPeriodicShocker
        // is the one script that ever casts ShockEffect, and it tests zad_EffectShocking. The two
        // other keywords DD's data carries are INTENTS DD NG never implemented - nothing in its
        // scripts or its DLL reads zad_EffectShockOnFullArousal ("instead of vibrating, shock on full
        // arousal": the two Black Soulgem plugs) or zad_EffectChaosPlug (the three chaos / filled
        // plugs; there is no zadEventChaosPlug in DD NG at all). Until 1.2.5 this mod discharged all
        // five from those keywords alone. A Black Soulgem plug is the very-strong vibrator the player
        // knows it as, nothing more.
        const bool shocking = HasKw(w, "zad_EffectShocking");

        // ⛔ TWO SHOCKER GENERATIONS SHARE ONE DISPLAY NAME, and only one of them
        // is reachable by keyword. "Shocking Soulgem Vaginal Plug" exists twice:
        //   Expansion  zadx_plugShockSoulgemVag  -> carries zad_EffectShocking ✅
        //   Integration zad_plugShocker (050C87) -> carries NO effect keyword ⛔
        // DD drives the second one from zadEventPeriodicShocker, which is
        // PLAYER-ONLY - so on an NPC it never discharged at all, and DoShock
        // exists precisely to close that gap. Measured: its rendered half reports
        // an empty effect-keyword set.
        //
        // ⚠ MATCHED BY FORM, NOT BY NAME (the user's ruling: "it should be our own
        // device only"). A name test on "Shock" would catch any content mod's
        // device merely NAMED for one and start discharging things we know nothing
        // about. This is one known DD record, addressed as itself.
        // ⚠ The rendered half is the one worn, so 050C87 is the right half of the
        // pair - the inventory twin is 050C89 and never reaches ForEachWorn.
        // ⚠ NOT cached in a function-local static: this can be reached before the
        // data handler is ready, and a static would latch that nullptr for the
        // whole session. LookupForm is a hash lookup and this loop already walks
        // worn armor, so the cost is noise.
        RE::TESForm* origForm = DdForm(0x050C87, kIntegration);
        const bool origShocker = (origForm && w == origForm);

        // ★★ 1.2.9 (the user, 2026-09-14: "treat it as a shock plug"): zad_EffectShockOnFullArousal is
        // BACK, as its own kind. The 1.2.5 note above ("DD NG never implemented it") was wrong: DD 5.2's
        // zadEventVibrate.Execute - a script DD NG does not override - discharges it for real at arousal
        // >= 99 (ShockEffect + exposure -> 1), through DD's own NPC slot loop. Our lane: at >= 99 the
        // plug's discharge IS the shock (deterministic, the shock cooldown paces it), never the 100
        // lane's vibration finish. It still builds arousal like any soulgem on the way up (ScanForCast).
        const bool onFull = HasKw(w, "zad_EffectShockOnFullArousal");
        if (!shocking && !origShocker && !onFull) return true;

        r.count += 1;
        if (onFull) r.onFull += 1;
        // origShocker rides with `shocking`: an ordinary shocking soulgem plug, the same
        // arousal >= 60 gate. The kind field stays in the payload for the Controller.
        if (shocking || origShocker) r.kind = 2;

        char cls[64];
        ClassOf(w, cls, sizeof cls);
        if (_strnicmp(cls, "Plug", 4) == 0)                r.sites |= 1;
        else if (_stricmp(cls, "PiercingsNipple") == 0)    r.sites |= 2;
        else if (_stricmp(cls, "PiercingsVaginal") == 0)   r.sites |= 4;
        return true;
    });
    return r;
}

bool HasEffect(RE::Actor* a, std::uint32_t rawId)
{
    if (!a || !a->AsMagicTarget()) return false;
    auto* f = DdForm(rawId, kIntegration);
    auto* mg = f ? f->As<RE::EffectSetting>() : nullptr;
    return mg && a->AsMagicTarget()->HasMagicEffect(mg);
}

// ─────────────────────────────────────────────────────────────────────────────
// THE STRAIN MODEL
//
// Phase boundaries in GAME HOURS, from the user's x4 mapping of the source
// table's nominal minutes. See WornDevices.h for why this needs no setting.
// ─────────────────────────────────────────────────────────────────────────────
constexpr float kPhaseH[4] = { 1.0f, 3.0f, 6.0f, 12.0f };   // -> phases 2,3,4,5

enum StrainTier {
    kStrainNone = 0,
    kStrainArms,    // arms held in a fixed loaded position — the source's own
                    // phenomenology, shoulders and neck, word for word
    kStrainLimb,    // other loaded structures: knees, hips, ankles
    kStrainStrap,   // straps only: compression and possible nerve involvement,
                    // but NO sustained joint loading
    kStrainYoke,    // a rigid frame holding the wrists at or above shoulder
                    // height - severe, but it never reaches nerve block
    kStrainRope,    // wide wrapped load: some soreness, mostly stiffness
    kStrainSuit,    // NOT a pain scale at all - sealed heat and damp

    // ★ TWO GAG TIERS, because one line cannot honestly fit the family.
    // ⚠ A held-open mouth DRIES and a filled mouth FLOODS, and that is measured
    // rather than assumed: evaporation from an open mouth reaches ~0.21 ml/min
    // against an unstimulated salivary flow of only 0.1-0.3 ml/min, so an open
    // mouth can consume the entire resting supply. Meanwhile the
    // masticatory-salivary reflex fires on any object held between the teeth -
    // non-gustatory, purely mechanical - and mechanical stimulation alone can
    // raise flow to ~1.5 ml/min. So a ring gag dries while a ball gag floods.
    // The old single SlowLine row said "the jaw aches and the mouth has gone
    // dry", which was true of one half of the family and false of the other.
    kStrainGagOpen, // ring / bit: jaw held open, mouth dries
    kStrainGagFill, // ball / large / inflatable / panel: mouth filled, floods

    // ★ THE SECOND GENUINELY NON-MONOTONE TIER. Sustained clamping is compression
    // ischemia, and the ischemic nerve-conduction literature gives a measured
    // curve: impairment begins within about ten minutes and reaches its NADIR at
    // 45-60, with sensory axons failing first and fastest. So reported sensation
    // genuinely FALLS while the ischemia is at its worst - the same shape the
    // arms tier's phase 5 already models, on a clock roughly ten times faster.
    // The event every source agrees is significant is REMOVAL: reperfusion into
    // vasodilated tissue, reported as sharper than the wear itself.
    kStrainClamp,

    // ★ THE MITTEN (2026-08-28, the user on #293/#333/#336-338/#364-366):
    // "they are mitten, completely englobing the hand, pain scale to 5 with
    // those, very painfull. Also, prevent action as finger aren't accessible
    // anymore."
    //
    // ⛔ IT NEEDED ITS OWN TIER RATHER THAN A SEAT IN kStrainArms, and putting
    // it there would have been the FIFTH violation of the rule this file has
    // already fixed four times: a tier line must fit EVERY class in the tier.
    // The arms lines speak of the shoulders and neck, of arms grown congested
    // and heavy - all false of a mitten, which loads nothing above the wrist.
    // What a mitten actually does is hold every finger joint in one fixed
    // position with no way to flex it, which is a different mechanism on the
    // same clock: mult 1.0, and ceiling 5 because the user is explicit that it
    // reaches the top.
    kStrainMitt
};

struct TierRule { float mult; int ceiling; };

// ★ THE CONSERVATISM IS THE USER'S, AND IT IS RIGHT (2026-08-26):
//   "armbinder are creating pain, but normal wrist or ankle cuff can be wear
//    for day without never getting to the nerve damage part."
// So a plain cuff is NOT a slower armbinder — it is a different curve with a
// hard ceiling. It reaches a dull chafe and stops there forever. Capping it is
// what stops the model claiming nerve damage from a pair of bracelets, which
// would be both wrong and the kind of overreach that discredits the whole block.
constexpr TierRule kTier[11] = {
    { 0.0f, 0 },   // none
    { 1.0f, 5 },   // arms  — full curve: 1 / 3 / 6 / 12 game hours
    { 1.5f, 5 },   // limb  — same shape, slower: 1.5 / 4.5 / 9 / 18
    { 6.0f, 2 },   // strap — 6 game hours to a dull ache, and NEVER past it

    // ★ YOKE — CEILING 4, the user's own ladder (2026-08-27): "normal cuff only
    // goes to level two of pain. yoke could go up to 4." It stops one short of
    // phase 5, which is nerve conduction block: a yoke is as bad as sustained
    // pain gets and still never crosses into nerve damage.
    // ⚠ mult 1.25, i.e. SLOWER than an armbinder, and that is measured rather
    // than guessed. FK on DD NG's pose files: an armbinder holds the elbow at
    // 4 degrees with the shoulder-to-wrist chain at its full 38.9-unit reach -
    // zero slack anywhere, the joint pinned at its limit. A yoke holds the elbow
    // at 125 degrees with the chain folded to half its reach, so there IS slack
    // to shift within. It arrives later; it arrives just as hard.
    { 1.25f, 4 },  // yoke

    // ★ ROPE — mostly stiffness (user: "some pain, but mostly stiffness").
    // ⚠ mult 2.0, i.e. twice as slow as the same device in leather or steel, and
    // the reason is mechanical: many turns of rope spread their tension over a
    // broad band of skin, where a rigid band concentrates the same load on one
    // narrow line. Lower contact pressure means circulation and nerve
    // compression arrive far later. Ceiling 4 for the same reason as the yoke -
    // held long enough it genuinely stiffens, but it does not produce the
    // nerve-block endpoint a hard edge does.
    { 2.0f, 4 },   // rope

    // ★ SUIT — NOT A PAIN SCALE. A sealed suit loads no joint and holds no limb
    // at the end of its range, so there is no postural mechanism to model. What
    // it does is thermal: it stops evaporation, so heat and sweat accumulate
    // against the skin. Monotone by nature - it only accumulates - and paced at
    // 1.0 because that happens on the same sort of timescale as an ache does.
    { 1.0f, 5 },   // suit

    // ★ THE FASTEST CURVES IN THE MODEL, and that is evidence-led. The small
    // pterygoids fatigue after several minutes of sustained opening, and the
    // clinical literature finds a measured dose-response between how long a
    // surgical mouth gag is applied and post-operative jaw pain and trismus.
    // BDSM safety guidance for gags is quoted in MINUTES - 20-45 for a ball gag,
    // 10-45 for a ring gag - where no comparable minute-scale limit exists for
    // an armbinder or a cuff anywhere in the same body of writing.
    // mult 0.5: phases at 0.5 / 1.5 / 3 / 6 game hours.
    // ⚠ CEILING 4, NOT 5. Phase 5 in this model is nerve conduction block, which
    // is not what a gag produces - and the honest position is that there is no
    // literature at all on a conscious person gagged beyond about three hours.
    // Rather than invent an endpoint, the curve stops at its last supported
    // phase. A shorter honest scale beats a fabricated one.
    { 0.5f, 4 },   // gag, held open
    { 0.5f, 4 },   // gag, filled

    // ★ THE FASTEST CURVE IN THE MODEL BY AN ORDER OF MAGNITUDE, and it is
    // calibrated to the ischemia literature rather than chosen. mult 0.08 puts
    // the phases at roughly 5 / 14 / 29 / 58 MINUTES:
    //   phase 2 ~5 min   - the first compression effects
    //   phase 3 ~14 min  - where every safety source puts its limit (10-15 min,
    //                      up to 30 for a loose clamp)
    //   phase 4 ~29 min  - past all of that guidance
    //   phase 5 ~58 min  - the measured conduction nadir at 45-60 minutes
    // Ceiling 5, because unlike the yoke and rope tiers this one genuinely
    // reaches the nerve endpoint and must be allowed to report it.
    { 0.08f, 5 },  // clamp

    // ★ MITTEN - same pace as the arms curve, full ceiling. A hand held shut
    // for twelve hours is the user's "very painfull", and unlike a cuff there
    // is no position to shift into: the fingers cannot move at all.
    { 1.0f, 5 }    // mitt
};

// ═════════════════════════════════════════════════════════════════════════════
// ★★ TUNABLE SCALES — Data/SKSE/Plugins/VRTE_DDZaZ_Scales.ini
//
// The user's ask, 2026-09-03: "make it that everything that got a scale can be
// edited like that for the timer, so people can edit it to their own gamestyle",
// extended the same day to the chances.
//
// ⚠ WHY THIS DOES NOT CONTRADICT WornDevices.h. That header argues the phase
// boundaries are "timescale-independent by construction and need no setting",
// and it is RIGHT — about TIMESCALE. A player at timescale 20 lives in a faster
// world and reaches the boundaries sooner in real terms, which is correct
// because everything else in their game is faster too. This file is about a
// different axis entirely: PACE AS TASTE. A grim survival playthrough wants an
// armbinder punishing in three hours; a light fetish one wants it to take a day.
// The header never asked that question, so nothing here overrides it.
//
// ★ THE DEFAULTS ARE THE SHIPPED MODEL. kPhaseH and kTier above remain the
// source of truth — this struct is COPIED from them at load, so the reasoning in
// their comments stays where it belongs and a missing INI changes NOTHING.
// ═════════════════════════════════════════════════════════════════════════════
struct Scales
{
    float pace = 1.0f;                  // one dial over the whole strain model
    float phase[4]{};                   // <- kPhaseH
    TierRule tier[11]{};                // <- kTier
    int   ceilBoots = 3;                // CeilingFor's per-class overrides
    int   ceilHeels = 2;
    float dampMult = 2.0f;              // partial coverage
    float dampSealedMult = 1.0f;        // a full sealed garment
    int   dampCeil = 4;
    int   dampSealedCeil = 5;
    // real-second clocks
    // cdClaim 3.0 -> 3.5 (2026-09-14, the user: "3.5s is good"): PPB build 20106 verifies an equip at
    // 1.2 / 2.5 / 3.5 s, so a 3.0 s claim could expire before PPB's last look.
    double cdCast = 45.0, cdShock = 90.0, cdClaim = 3.5, cdMenuClaim = 10.0;
    double cdPlugEcho = 5.0, cdTrip = 30.0, cdMisTarget = 12.0;
    // percent chances
    int pctCastPlug = 15, pctCastPiercing = 5, pctShockPerDevice = 6;
    // ★ per-SITE arousal, summed over every worn device (user, 2026-09-06)
    int arNipple = 1, arAnal = 2, arClitoris = 3, arVaginal = 3;
    int pctTripWalk = 5, pctTripRun = 25, pctTripStairs = 50, pctMisTarget = 25;
    // ── aftermath, in GAME HOURS ────────────────────────────────────────
    // ⚠ Only `afterReperf` is anchored (a released ischemic block returns
    // over minutes). The rest is an argued SHAPE - ordered, roughly doubling.
    // Said plainly here and in the INI rather than dressed up as measurement.
    float afterMarks = 1.0f;      // phase 2
    float afterAche  = 2.0f;      // phase 3
    float afterDeep  = 4.0f;      // phase 4
    float afterReperf = 0.5f;     // phase 5 segment A  [ANCHORED]
    float afterDeficit = 6.0f;    // phase 5 segment B
    float afterDecay = 1.0f;      // the wear value winds down over this
    float afterStrapMin = 24.0f;  // a cuff/collar marks only past this much wear
};

Scales g_sc;
bool   g_scLoaded = false;

// ⚠ CLAMPED, and LOUDLY. A hand-edited file is a hostile input: a 0 multiplier
// makes every device reach its ceiling instantly, a negative one inverts the
// curve, and a ceiling above 5 indexes past every STRAIN_LINE table. Refusing a
// value and SAYING SO beats trusting it, and beats silently substituting one.
// Returns -1 as a sentinel meaning "keep your default".
template <class T>
static T ScClamp(const char* what, T v, T lo, T hi)
{
    if (v < lo || v > hi) {
        logger::info("[SCALES] '{}' = {} is outside [{}, {}] - kept the default",
                     what, v, lo, hi);
        return static_cast<T>(-1);
    }
    return v;
}

static void LoadScalesIni()
{
    g_scLoaded = true;
    for (int i = 0; i < 4; ++i)  g_sc.phase[i] = kPhaseH[i];
    for (int i = 0; i < 11; ++i) g_sc.tier[i]  = kTier[i];

    FILE* f = nullptr;
    if (fopen_s(&f, "Data/SKSE/Plugins/VRTE_DDZaZ_Scales.ini", "r") != 0 || !f) {
        logger::info("[SCALES] no VRTE_DDZaZ_Scales.ini - shipped model unchanged");
        return;
    }

    // ★ Tier names in kTier ORDER, so [multiplier] and [ceiling] key by NAME.
    // An appended tier therefore cannot silently shift someone's tuning onto a
    // different curve the way a positional list would.
    static const char* kTierKey[11] = {
        "none", "arms", "limb", "strap", "yoke", "rope", "suit",
        "gagopen", "gagfill", "clamp", "mitt"
    };

    char line[256];
    int  sec = 0, taken = 0, refused = 0;
    while (fgets(line, sizeof line, f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p || *p == ';' || *p == '#' || *p == '\n' || *p == '\r') continue;
        if (*p == '[') {
            sec = 0;
            if      (_strnicmp(p, "[pace]",        6) == 0) sec = 1;
            else if (_strnicmp(p, "[phase]",       7) == 0) sec = 2;
            else if (_strnicmp(p, "[multiplier]", 12) == 0) sec = 3;
            else if (_strnicmp(p, "[ceiling]",     9) == 0) sec = 4;
            else if (_strnicmp(p, "[cooldown]",   10) == 0) sec = 5;
            else if (_strnicmp(p, "[chance]",      8) == 0) sec = 6;
            else if (_strnicmp(p, "[aftermath]",  11) == 0) sec = 7;
            else if (_strnicmp(p, "[arousal]",     9) == 0) sec = 8;
            else logger::info("[SCALES] unknown section - ignored");
            continue;
        }
        char key[32] = {};
        double val = 0.0;
        // ⚠ KEYS ARE PURELY ALPHABETIC, matching the two INIs already shipped.
        // Their loaders scan %31[A-Za-z], so "phase2" is REJECTED there; keeping
        // the same rule here means ONE convention across all three files rather
        // than a per-file trap. Hence "first"/"second" and "gagopen".
        if (std::sscanf(p, " %31[A-Za-z] = %lf", key, &val) != 2) {
            logger::info("[SCALES] unreadable line - ignored (keys must be "
                         "letters only, values a number)");
            ++refused;
            continue;
        }
        bool hit = false;
        auto fl = [&](const char* k, float& dst, float lo, float hi) {
            if (hit || _stricmp(key, k) != 0) return;
            hit = true;
            const float v = ScClamp<float>(k, static_cast<float>(val), lo, hi);
            if (v >= 0.0f) { dst = v; ++taken; } else ++refused;
        };
        auto db = [&](const char* k, double& dst, double lo, double hi) {
            if (hit || _stricmp(key, k) != 0) return;
            hit = true;
            const double v = ScClamp<double>(k, val, lo, hi);
            if (v >= 0.0) { dst = v; ++taken; } else ++refused;
        };
        auto in = [&](const char* k, int& dst, int lo, int hi) {
            if (hit || _stricmp(key, k) != 0) return;
            hit = true;
            const int v = ScClamp<int>(k, static_cast<int>(val), lo, hi);
            if (v >= 0) { dst = v; ++taken; } else ++refused;
        };

        if (sec == 1) {
            fl("global", g_sc.pace, 0.05f, 20.0f);
        } else if (sec == 2) {
            fl("first",  g_sc.phase[0], 0.01f, 500.0f);
            fl("second", g_sc.phase[1], 0.01f, 500.0f);
            fl("third",  g_sc.phase[2], 0.01f, 500.0f);
            fl("fourth", g_sc.phase[3], 0.01f, 500.0f);
        } else if (sec == 3) {
            for (int t = 1; t < 11; ++t) fl(kTierKey[t], g_sc.tier[t].mult, 0.001f, 100.0f);
            fl("damp",       g_sc.dampMult,       0.001f, 100.0f);
            fl("dampsealed", g_sc.dampSealedMult, 0.001f, 100.0f);
        } else if (sec == 4) {
            for (int t = 1; t < 11; ++t) in(kTierKey[t], g_sc.tier[t].ceiling, 1, 5);
            in("damp",       g_sc.dampCeil,       1, 5);
            in("dampsealed", g_sc.dampSealedCeil, 1, 5);
            in("boots",      g_sc.ceilBoots,      1, 5);
            in("heels",      g_sc.ceilHeels,      1, 5);
        } else if (sec == 5) {
            db("cast",      g_sc.cdCast,      0.0, 100000.0);
            db("shock",     g_sc.cdShock,     0.0, 100000.0);
            db("claim",     g_sc.cdClaim,     0.0,    600.0);
            db("menuclaim", g_sc.cdMenuClaim, 0.0,    600.0);
            db("plugecho",  g_sc.cdPlugEcho,  0.0,    600.0);   // ⛔ retired 1.2.7 - read, ignored
            db("trip",      g_sc.cdTrip,      0.0, 100000.0);
            db("mistarget", g_sc.cdMisTarget, 0.0, 100000.0);
        } else if (sec == 6) {
            in("castplug",       g_sc.pctCastPlug,       0, 100);
            in("castpiercing",   g_sc.pctCastPiercing,   0, 100);
            in("shockperdevice", g_sc.pctShockPerDevice, 0, 100);
            in("tripwalking",    g_sc.pctTripWalk,       0, 100);
            in("triprunning",    g_sc.pctTripRun,        0, 100);
            in("tripstairs",     g_sc.pctTripStairs,     0, 100);
            in("mistarget",      g_sc.pctMisTarget,      0, 100);
        } else if (sec == 7) {
            fl("marks",          g_sc.afterMarks,    0.0f, 500.0f);
            fl("ache",           g_sc.afterAche,     0.0f, 500.0f);
            fl("deep",           g_sc.afterDeep,     0.0f, 500.0f);
            fl("reperfusion",    g_sc.afterReperf,   0.0f, 500.0f);
            fl("deficit",        g_sc.afterDeficit,  0.0f, 500.0f);
            fl("decay",          g_sc.afterDecay,    0.0f, 500.0f);
            fl("strapthreshold", g_sc.afterStrapMin, 0.0f, 5000.0f);
        } else if (sec == 8) {
            in("nipple",   g_sc.arNipple,   0, 100);
            in("anal",     g_sc.arAnal,     0, 100);
            in("clitoris", g_sc.arClitoris, 0, 100);
            in("vaginal",  g_sc.arVaginal,  0, 100);
        }
        if (!hit && sec) {
            logger::info("[SCALES] '{}' is not a key of that section - ignored", key);
            ++refused;
        }
    }
    fclose(f);

    // ★ THE PACE DIAL folds into the per-tier MULTIPLIERS, not the phase
    // boundaries, so the SHAPE of the ladder is preserved exactly: a reader
    // comparing two tiers still sees the relationship their comments explain,
    // and the user's own ladder (cuff 2, yoke 4) survives any global retune.
    if (g_sc.pace != 1.0f) {
        for (int t = 1; t < 11; ++t) g_sc.tier[t].mult *= g_sc.pace;
        g_sc.dampMult       *= g_sc.pace;
        g_sc.dampSealedMult *= g_sc.pace;
    }
    logger::info("[SCALES] {} value(s) taken, {} refused, pace x{:.2f}",
                 taken, refused, g_sc.pace);
}


// ⚠ `rope` is a MATERIAL and cuts across every class, so it cannot be a row in
// the class tables below - a rope armbinder is still class Armbinder. It is
// applied as an OVERRIDE after the class has chosen a tier, and only when the
// class earned a tier at all: a rope gag is class Gag, which is postural in no
// sense and must stay kStrainNone rather than acquiring a stiffness curve.
// `soft` forces kStrainNone: a garment that occupies a strained class's slot
// without doing what that class does. Handled HERE rather than at the call site
// so every tier decision stays in one function.
StrainTier TierOfClass(const char* cls, bool rope, bool clamp, bool soft)
{
    if (!cls || !cls[0] || soft) return kStrainNone;

    // ⚠ FIRST, and it overrides the class outright: a clamp resolves to class
    // PiercingsNipple (see IsClampDevice for why), which has no tier at all, so
    // without this a clamp reports as a piercing and accumulates nothing.
    if (clamp) return kStrainClamp;

    // ── the gag family, split by what it does to the mouth ──────────────────
    // ⚠ GagTape is deliberately in NEITHER: it seals the lips with the jaw
    // CLOSED, so there is no masticatory loading, no held-open drying and no
    // pooling. Putting it on a jaw scale would repeat the tier-line defect this
    // file has already fixed three times.
    // ⚠ GagPanel IS a filling gag, and that is DD's own answer rather than a
    // judgement: zadLibs ships PlugPanelGag() - a panel gag carries a plug.
    if (_strnicmp(cls, "Gag", 3) == 0) {
        if (_stricmp(cls, "GagRing") == 0 || _stricmp(cls, "GagBit") == 0)
            return kStrainGagOpen;
        if (_stricmp(cls, "GagTape") == 0) return kStrainNone;
        return kStrainGagFill;          // Gag, GagLarge, GagInflatable, GagPanel
    }

    // Sealed whole-body garments get the thermal scale, not a pain scale.
    // ⚠ ROPE OVERRIDES IT. A rope full-body bind is class Suit, and the thermal
    // lines would say "the lining has turned slick" and "the heat has nowhere to
    // go" about a garment that has neither a lining nor an inside - rope does not
    // seal, which is the entire premise of the thermal scale.
    if (_stricmp(cls, "Suit") == 0) return rope ? kStrainRope : kStrainSuit;

    // The yoke family: a rigid frame holding the wrists at or above the
    // shoulders. Separated from the arms tier so it can stop at phase 4.
    static const char* yoke[] = { "Yoke", "YokeBB", "YokeFront" };
    for (auto* y : yoke) if (_stricmp(cls, y) == 0)
        return rope ? kStrainRope : kStrainYoke;
    // arms held in a fixed position, loaded against their own range of motion
    // ⚠ YokeFront ADDED 2026-08-27. It had a ClassLine row but appeared in no
    // tier array, so a front yoke was described and then reported no strain at
    // all. A fiddle holds both wrists clamped at chin height in front of the
    // face: the arms are raised and held there continuously, which is a
    // sustained postural load on the shoulders exactly as the other yokes are.
    static const char* arms[] = { "HeavyBondage", "Armbinder", "ArmbinderElbow",
                                  "ElbowTie", "StraitJacket", "Boxbinder" };
    for (auto* s : arms) if (_stricmp(cls, s) == 0)
        return rope ? kStrainRope : kStrainArms;

    // ★ BondageMittens - its own curve, see the enum. Placed BEFORE the limb
    // and strap rows so it cannot be captured by a broader one later.
    if (_stricmp(cls, "BondageMittens") == 0) return kStrainMitt;

    // load carried elsewhere: folded limbs, forced plantarflexion, wrists locked
    // together in front
    static const char* limb[] = { "PetSuit", "PonyGear", "Boots" };
    for (auto* s : limb) if (_stricmp(cls, s) == 0)
        return rope ? kStrainRope : kStrainLimb;

    // straps that compress but do not hold a joint under load
    // ⚠ CuffsFront MOVED HERE 2026-08-26 (sweep). It was in the limb tier, on
    // the full curve, so wrists locked in FRONT would have reached numbness and
    // cold at 18 game hours. That directly contradicts the user's own governing
    // principle - "normal wrist or ankle cuff can be wear for day" - and front
    // cuffs restrict rather than load: the arms still hang naturally. It is a
    // cuff, so it belongs on the cuff curve.
    static const char* strap[] = { "ArmCuffs", "CuffsArms", "LegCuffs", "CuffsLegs",
                                   "AnkleShackles", "Harness", "Collar", "CuffsFront" };
    // ⛔ ROPE DOES *NOT* OVERRIDE THE STRAP TIER, and the first version of this
    // getting that wrong is worth recording. strap is mult 6.0 / ceiling 2;
    // rope is 2.0 / 4. Overriding here made a ROPE wrist cuff speak at 2 game
    // hours instead of 6 and climb to phase 4 instead of stopping at 2 - three
    // times sooner and two phases further than the same cuff in leather.
    // That inverted the physical argument the rope tier is built on (a wide
    // wrap spreads load, so compression arrives LATER) and broke the user's
    // governing ladder in the same stroke: "normal cuff only goes to level two
    // of pain".
    // ★ It also fixes a second defect for free: the rope lines describe held
    // muscle and stiffened joints, which is nonsense for a rope COLLAR - there
    // are no joints beneath a collar. Restricting the override to the postural
    // tiers means only devices that actually hold a posture get a posture line.
    for (auto* s : strap) if (_stricmp(cls, s) == 0) return kStrainStrap;

    // ⚠ "Suit" USED to get no strain tier, on the reasoning that a sealed suit
    // loads no joint so there is no postural mechanism to model. That half is
    // still true - which is why its tier is THERMAL and not a pain curve at all
    // (see kStrainSuit above, matched at the top of this function). The old
    // comment here said the omission was deliberate and pointed at SlowLine's
    // Suit row; that row is now unreachable, because kStrainSuit reaches phase 3
    // at the same 3.0 game hours SlowLine gated on and Report prefers `pain`.

    return kStrainNone;
}

// ★ THE DEVICE, NOT ONLY ITS CLASS (2026-09-13, the user on rows 1478/1479 Submissive Arm / Leg Cuffs:
// "no pain, chafing 1-2"). They are CUFFS that also carry a binding keyword - zad_DeviousArmCuffs with
// zad_DeviousArmbinder, zad_DeviousLegCuffs with zad_DeviousHobbleSkirt - so ClassOf answers Armbinder /
// HobbleSkirt and TierOfClass put the arm cuffs on the full ARMS pain curve. A cuff is a cuff: the strap
// tier (mult 6, ceiling 2). The restraint itself (hands back / short stride) is unchanged.
// Census: those two keyword pairs occur on exactly these two records.
// ⚠ Every tier question goes through here - Report, NoteAftermath, WearCredit - so the wear clock, the
// after-state and the re-equip credit can never disagree about which curve a device is on.
StrainTier TierOfDevice(RE::TESObjectARMO* w, const char* cls, bool rope, bool clamp, bool soft)
{
    if (w && ((HasKw(w, "zad_DeviousArmCuffs") && HasKw(w, "zad_DeviousArmbinder")) ||
              (HasKw(w, "zad_DeviousLegCuffs") && HasKw(w, "zad_DeviousHobbleSkirt"))))
        return (cls && cls[0] && !soft) ? kStrainStrap : kStrainNone;
    return TierOfClass(cls, rope, clamp, soft);
}

// ⚠ Forward declarations: CeilingFor needs both, and both live further down
// beside the other name-keyed predicates. Declaring rather than moving keeps
// each predicate next to its siblings, which is where a reader looks for it.
// (ContainsNoCase is itself a near-duplicate of NameHas at the top of this
// file - noted, not merged: two call sites' worth of churn for no behaviour.)
bool ContainsNoCase(const char* hay, const char* needle);
bool IsModestHeel(const char* cls, const char* label);

// ★ A PER-CLASS CEILING, on top of the tier's own (2026-08-28, the user on the
// pony boots #224-229): "I say scale up to 3 for that one, yes, can be
// painfull, but foot are used to feel pressure, not like an arm joint which is
// supposed to move all the time."
//
// ★ THE ARGUMENT IS PHYSIOLOGICAL AND IT GENERALISES, so it is applied to the
// whole Boots class (108 devices) rather than to the six they named: the foot
// is a weight-bearing structure adapted to sustained load, and a joint built to
// carry the body does not fail the way a shoulder held at the end of its range
// does. Ballet and pony boots are the hardest cases in the class and the user's
// ruling covers the hardest one.
// ⚠ Boots keep the LIMB tier's pace (1.5 / 4.5 / 9 / 18) - only the ceiling
// moves, so nothing about how fast it climbs changes; it simply stops at "the
// ache has moved into the joints" and never reaches nerve block.
int CeilingFor(const char* cls, StrainTier t, const char* label)
{
    if (!g_scLoaded) LoadScalesIni();
    int c = g_sc.tier[t].ceiling;
    if (cls && _stricmp(cls, "Boots") == 0) c = (std::min)(c, g_sc.ceilBoots);
    // A bondage heel is steep but ordinary footwear geometry - two, not three.
    if (IsModestHeel(cls, label)) c = (std::min)(c, g_sc.ceilHeels);
    return c;
}

int PhaseOf(StrainTier t, float hours, int ceiling)
{
    if (t == kStrainNone) return 0;
    if (!g_scLoaded) LoadScalesIni();
    const float m = g_sc.tier[t].mult;
    int p = 1;
    for (int i = 0; i < 4; ++i) if (hours >= g_sc.phase[i] * m) p = i + 2;
    return (std::min)(p, ceiling);
}

// Mechanism + sensation only. NEVER an emotion, an evaluation or a motivation —
// the standing rule (report 19 §1), and the one the positive-voice rewrite
// broke by drifting into wanting (report 23 §39 addendum). "The shoulders ache"
// is a body report. "She is desperate" is us writing her character for her.
const char* StrainLine(StrainTier t, int phase)
{
    if (t == kStrainArms) {
        switch (phase) {
        case 2: return "a deep ache has settled through the shoulders and neck and no longer eases between shifts";
        // ⚠ was "the straps" - false for Yoke, YokeBB and Boxbinder, which are
        // rigid bars and are in this same tier. The identical defect was fixed
        // in the LIMB tier on 08-26 and not applied to the tier above it.
        case 3: return "a hard throbbing pressure has built where it grips, the arms feel congested and heavy, and the fingers have begun to tingle";
        case 4: return "the ache has moved into the shoulder joints themselves as a deep grinding wrongness that no change of position relieves";
        case 5: return "the sharp pain has faded to numbness, the arms feel cold and distant, and the hands are slow and weak to answer";
        default: return nullptr;
        }
    }
    if (t == kStrainLimb) {
        switch (phase) {
        // ⚠ Worded for the whole tier, not for one member of it. These lines
        // used to say "the straps" and "the hips and knees" - true of a pet
        // suit, false of ballet boots, which have no straps and load the ankle
        // and arch. A tier line has to fit every class in the tier.
        case 2: return "a deep ache has settled through the legs and no longer eases between shifts";
        case 3: return "a hard throbbing pressure has built where the weight is carried, the legs feel congested and heavy, and the feet have begun to tingle";
        case 4: return "the ache has moved into the joints themselves as a deep grinding wrongness that no change of position relieves";
        case 5: return "the sharp pain has faded to numbness, the legs feel cold and distant, and footing is unsteady";
        default: return nullptr;
        }
    }
    if (t == kStrainStrap && phase >= 2)
        return "the edges have begun to chafe and there is a dull ache under them";

    // ★ THE YOKE SCALE. Same structures as the arms tier - it is still a rigid
    // frame holding the shoulders - but it stops at 4, so the nerve-block line
    // is unreachable by construction. Worded for arms held OUT and UP, which is
    // what the pose measurements show and what the arms tier's own lines do not
    // describe.
    if (t == kStrainYoke) {
        switch (phase) {
        // ⚠ WORDED FOR THE WHOLE TIER, NOT ONE MEMBER OF IT. These lines first
        // said "the arms out and up" and "heavy on the bar", which is true of a
        // plain yoke and false of the other two: a Breast Yoke carries the wrists
        // level and forward, and a YokeFront is a FIDDLE - a board with the
        // wrists clamped TOGETHER at chin height, not a bar with them spread. The
        // composed entry then contradicted itself inside one bullet. The common
        // denominator is the one the enum comment already had: a rigid frame
        // holding the wrists at or above shoulder height.
        // ⚠ This is the THIRD time this exact defect has been fixed in this
        // function - the arms tier said "the straps" (false for rigid yokes) and
        // the limb tier said "the hips and knees" (false for ballet boots).
        case 2: return "the shoulders have begun to burn from carrying the wrists up and fixed in place";
        case 3: return "the burn has spread across the upper back and the arms have grown heavy where they are held";
        case 4: return "the shoulder joints ache deeply and continuously, and the arms have started to shake even at rest";
        default: return nullptr;
        }
    }

    // ★ THE ROPE SCALE (user, 2026-08-27: "some pain, but mostly stiffness").
    // ⚠ It is a STIFFNESS curve, not a quieter pain curve. Muscle held in one
    // position under a broad wrap sets and shortens; that is what accumulates
    // here, rather than the compression and nerve involvement a hard edge
    // produces. The soreness is present and secondary, which is the balance the
    // user asked for.
    if (t == kStrainRope) {
        switch (phase) {
        case 2: return "the turns have pressed their pattern into the skin and the muscle under them has begun to stiffen";
        // ⚠ Reworded off "no longer loosen between shifts" - the arms tier's
        // phase 2 uses "no longer eases between shifts", and both are reachable
        // in the same block at the same wear time. Hearing the construction twice
        // in adjacent bullets reads as template output and tells a reader the two
        // devices are doing the same thing, when the whole point of this tier is
        // that they are not.
        case 3: return "the held muscles have set hard and stay set through every change of position, with a dull soreness along each wrap";
        // ⚠ The first version ended "movement would come back slowly and awkwardly
        // once it came off" - the only COUNTERFACTUAL in the whole table. Every
        // other line reports a present state; this one forecast a post-removal
        // one, handing the reader a removal scenario nobody asked it to consider,
        // and it is the line a rope device sits on permanently from 12h onward.
        case 4: return "the stiffness has reached the joints beneath the rope and they no longer move through their full range";
        default: return nullptr;
        }
    }

    // ★ THE SUIT SCALE — thermal, and deliberately NOT painful at any phase.
    // ⚠ MONOTONE, unlike the pain tiers: heat and moisture only accumulate.
    // There is no equivalent of phase 5's nerve block here, so nothing falls
    // away at the end and the last line is simply the wettest.
    // ★ A HELD-OPEN MOUTH. Jaw first, because that is what the dose-response
    // literature actually measures; drying second, because it follows from the
    // opening rather than from the device.
    if (t == kStrainGagOpen) {
        switch (phase) {
        case 2: return "the muscles that close the jaw have begun to fatigue against the hold";
        case 3: return "the jaw aches steadily now, and the mouth has dried out where the air passes over it";
        case 4: return "the jaw has stopped answering properly and would be slow to close, and the lips and tongue have gone dry and tacky";
        default: return nullptr;
        }
    }

    // ★ A FILLED MOUTH. The opposite fluid problem, and worth stating plainly:
    // this is a CONTAINMENT failure, not a production one. The medical
    // literature is consistent that drooling is impaired swallowing plus lips
    // that cannot close - so the line says saliva is not being swallowed, which
    // is true of every gag in this tier, rather than saying more is being made.
    if (t == kStrainGagFill) {
        switch (phase) {
        case 2: return "the jaw has begun to ache around what it is holding, and saliva is collecting because none of it can be swallowed";
        case 3: return "saliva has overflowed and is running freely, and the jaw aches continuously";
        case 4: return "the jaw is locked hard around it and slow to release, and the chin and throat are wet through";
        default: return nullptr;
        }
    }

    // ★ CLAMPS. Non-monotone, so phase 5 is quieter than phase 4 by design - and
    // it is the phase where a reader is most likely to draw the wrong conclusion,
    // which is what the appended standing note exists to correct.
    if (t == kStrainClamp) {
        switch (phase) {
        // ⚠ "the gripped flesh", not "the skin" (2026-08-27): the tier gained a
        // second member - the tongue grip - and a tongue is mucosa, not skin.
        // The tier-line rule again: a line must fit EVERY member of its tier.
        case 2: return "the pinch has settled into a steady hard pressure and the gripped flesh has gone pale";
        case 3: return "the held tissue has begun to throb between the jaws of it and feels tight and hot";
        case 4: return "the throb has turned into a deep constant burn and the flesh beyond the grip has gone cold";
        // ⚠ The reperfusion fact is stated as a PROPERTY OF THE DEVICE, present
        // tense - what is true now - and not as a forecast of what removal will
        // feel like. Every other line in this table reports a present state and
        // this one must too. It matters enough to say because it is the one thing
        // every source on the subject agrees is significant.
        case 5: return "the grip has gone numb and distant, and the blood held out of it would return all at once if it came off";
        default: return nullptr;
        }
    }

    // ★ THE MITTEN LINES. Fingers, not shoulders - the whole reason this is not
    // the arms tier. Phase 5 is nerve block, so it FALLS like the other
    // non-monotone tiers and is registered as one in Report's footnote test.
    if (t == kStrainMitt) {
        switch (phase) {
        case 2: return "the fingers have begun to ache from being held in one shape with no way to flex them";
        case 3: return "the hands throb steadily inside them and the knuckles have stiffened where they are folded";
        case 4: return "the joints of every finger burn from being held, and the hands have swollen against the lining";
        case 5: return "the hands have gone numb and clumsy inside them, and there is no feeling left in the fingertips at all";
        default: return nullptr;
        }
    }

    if (t == kStrainSuit) {
        switch (phase) {
        case 2: return "warmth has built underneath and the air against the skin no longer changes";
        case 3: return "sweat has nowhere to go and the lining has turned slick everywhere it touches";
        // ⚠ was "body heat is no longer carrying away" - ungrammatical, and the
        // correct phrasing already existed twelve lines away in SlowLine's own
        // suit row.
        case 4: return "the whole inside surface is wet through and the heat has nowhere to go";
        // ⚠ Phase 5 has to be strictly MORE than phase 4 in this tier, because
        // the tier is monotone and no reading rule is appended to correct a
        // plateau. The first version changed subject from the garment to the skin
        // and swapped an absolute for a duration, so it did not obviously rank
        // higher. It also conflated maceration (softening under trapped wet) with
        // pruning, which is a different mechanism.
        case 5: return "the heat has been trapped so long that the skin has gone soft and pale where the suit holds against it, and keeps the mark of every seam";
        default: return nullptr;
        }
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// WHAT A DEVICE IS, in one clause. Class first, then material/tier off the NAME.
// ─────────────────────────────────────────────────────────────────────────────

// ═══════════════════════════════════════════════════════════════════════════
// WHAT THE DEVICE ACTUALLY DOES - not what it is called or what it is made of.
//
// ★ THE USER'S RULE (2026-08-27): "don't just do names, look at what they do in
// game, like if any type of restraint is connected to that gear in the DD
// script. a harness that do nothing, no restriction, is decorative or is for
// support. same for cuff. some cuff, even if golden, make the NPC's hand bind in
// front or in the back, those are restrictive."
//
// ★ AND THE RECORDS BEAR IT OUT EXACTLY. Measured against the live load order:
//   - Of every record named "cuffs", only FOUR carry zad_DeviousHeavyBondage:
//     zadx_HR_IronCuffsFront and zadx_HR_RustyIronCuffsFront. Labelled cuffs,
//     they genuinely bind - in FRONT.
//   - THIRTY-TWO rope records carry it (zadx_Armbinder_Rope_*, the Devious Lore
//     HeavyArmbinderRope set, zadxNG_Simple_Rope_Arms). Rope that binds.
//   - zadx_cuffs_Padded_Arms_Gold carries NONE of it, has a null ObjectEffect and
//     sits on slot 59, not 46. Gold, and ornamental.
// So material and name predict nothing, and the behaviour is readable.
//
// ⛔ EVERY NAME-, MATERIAL- AND DIFFICULTY-BASED TEST WAS TRIED AND KILLED:
// zad_Lockable is anti-correlated (pet collars have it, a Devious Lore quest
// restraint does not); zad_Property_*/zad_Material_* are used by ZERO armor
// records; ZaZ ships gold handcuffs AND a gold yoke; escape difficulty is
// INVERTED (the ornamental Gold Ceremonial Arm Cuffs are uncuttable at
// BaseEscapeChance 0, the pet-play Kitten Collar is 10). Do not re-propose them.
//
// ★ DD ENCODES THIS IN TWO SEPARATE PREDICATES, and the difference matters:
//   zadLibs.IsBound()        (zadLibs.psc:2482) - hands genuinely unusable.
//                             HeavyBondage / Armbinder / ArmbinderElbow / Yoke /
//                             YokeBB / StraitJacket.
//   zadLibs.NeedsBoundAnim() (zadLibs.psc:2486) - the ARMS ARE RE-POSED but she
//                             may still act. Adds CuffsFront and PetSuit.
// ⚠ zad_DeviousCuffsFront is deliberately in the second and NOT the first: a
// front-cuffed actor keeps her fighting ability in DD's own model. Collapsing
// the two would tell the LLM she cannot use her hands when DD says she can.
//
// ★ FRONT vs BACK IS MACHINE-READABLE, and DD NG proves it: PartialAnimationReplacer
// ships one JSON per keyword whose ONLY condition is "WornHasKeyword <kw> == 1",
// each rewriting all 46 arm/hand/finger nodes. front-cuffs.json and
// frontyoke.json against armbinder.json / yoke.json / Boxbinder.json are the
// ground truth. ZaZ mirrors it with zbfAnimHandsInHandCuffsFront vs ...Backside,
// and Diary of Mine with DOMWornCuffsFront.
enum class Restraint {
    None,        // carries no restraint signal at all - ornament or support
    Movement,    // hobble, forced walk, disabled kick: the legs, not the hands
    HandsFront,  // arms re-posed in front; DD still lets her fight
    HandsBack,   // hands behind the back: unusable, and DD refuses every self-help
    // ★ THE ARMS ARE FREE AND THE HANDS ARE NOT (2026-08-28). A mitten binds
    // nothing - she can reach anywhere - but every finger is sealed inside, so
    // she cannot grip, hold, unbuckle or pick. Neither HandsFront nor HandsBack
    // says that, and using either would put her arms in a posture she is not in.
    HandsSealed,
    // ★ TWO MORE POSTURES (2026-08-28, the user): "1142 to 1144, 1163, 1164 say
    // bound behind the back. Those one is front. All 'straitjacket' are in
    // front. It's 'Boxbinder' that is behind." / "1148, 1166, is a yoke, say
    // hand behind back. It's side of head. the effect is the same, but wording
    // is important to LLM for context."
    //
    // ★ THEY ARE RIGHT ON BOTH, AND OUR OWN ClassLine ALREADY AGREED - it has
    // said "wraps the arms across the body and holds them there" for a
    // straitjacket all along, while RestraintOf answered "behind the back" for
    // the same device. Two halves of the model describing opposite postures.
    HandsCrossed,  // straitjacket: folded across the chest and buckled down
    HandsHead,     // yoke: wrists up at either side of the head on a rigid bar
};

// ═══════════════════════════════════════════════════════════════════════════
// IS IT ROPE?
//
// ★ THE USER'S IDENTIFICATION METHOD, and it is better than anything derivable
// from the records' own vocabulary: "the way to identify them is by their 3D
// floating item in menu, all of them have the same preview, a bundle of rope."
// That mesh is `devious\FeuerTin\ft_ropebundle_go.nif`, and 243 records in this
// load order carry it.
//
// ⛔ A NAME TEST WOULD MISS MOST OF THEM. Measured against the live records, all
// of these carry the rope mesh and none says "rope" in its name:
//     zbfBoxTied01/02            "zbf Box Tied 01"
//     zbfArmsAroundNeck01/02     "zbf Arms Around Neck 01"
//     zbfHandsCrossedFront01/02  "zbf Hands Crossed Front 01"
//     pama_Rope01CollarNoose*    "Noose"
//     pama_CuffsRope01/02        "ropes"        (lower case, and not the word alone)
// And in the other direction `zadxng_rope_cuffs_Legs` is named "Simple Ropes
// (Leg Cuffs)" - labelled a CUFF, made of rope, which is exactly the case the
// user described.
//
// ⚠ TWO TESTS, BECAUSE NEITHER COVERS EVERYTHING ALONE.
//   1. The worn record's own world model. This catches every SINGLE-RECORD
//      device - all of ZaZ, Pama Furniture, Diary of Mine - and the DD rendered
//      halves that happen to carry one (zadx_rope_crotch_Rendered does).
//   2. The pushed INVENTORY-half name. A DD pair's rendered half usually has no
//      world model at all, but DD's own rope devices are named for it: "Black
//      Rope Collar", "Rope Ball Gag", "Simple Ropes (Arms)".
// The two are complementary rather than redundant: the devices test 1 misses are
// named for rope, and the devices test 2 misses carry the mesh.
//
// ⚠ This is a MATERIAL question, not a behaviour question, so using the name here
// does not contradict the rule that behaviour must never be read from a name.
// Whether a rope binds is decided by RestraintOf, from DD's keywords, exactly as
// for iron or leather.
bool ContainsNoCase(const char* hay, const char* needle)
{
    if (!hay || !needle || !hay[0] || !needle[0]) return false;
    const std::size_t nl = std::strlen(needle);
    for (const char* p = hay; *p; ++p) {
        if (_strnicmp(p, needle, nl) == 0) return true;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// IS SHE BELTED, AND IS THE BELT HOLDING SOMETHING IN?
//
// ★ THE USER'S RULE (2026-08-24, and again 2026-08-27 on seeing the device list):
// "3 is an iron plug. it can't be locked in place. BUT a chastity belt can be put
// on top to lock those device in place. which is an important mechanism of DD in
// general."
//
// ★ AND THE RECORDS AGREE EXACTLY. "Plug (Iron) (Set)" carries zad_DeviousPlug,
// zad_DeviousPlugAnal and zad_DeviousPlugVaginal - and NOT zad_Lockable. It has
// no lock of its own. The chastity belts DO carry zad_Lockable. So a plug is not
// held in by its own lock, it is held in by the thing covering it.
//
// ⛔ THIS RULE ALREADY EXISTED AND WAS NEVER EXPOSED. The hand-gesture layer has
// enforced it since 2026-08-24 - a finger cannot work a belted plug loose, and
// that code now lives in PPB's DeviceGesture.cpp - but the worn-device block
// never said so. The LLM was handed two unrelated facts, "locked over the hips"
// and "seated inside", and no way to know the second could not be undone while
// the first was on. A mechanism enforced in the simulation and invisible to the
// narrator is the worst of both: she is refused something she has no idea she
// cannot do.
//
// ⚠ MATCHED BY CLASS, NOT BY SLOT. Slot 49 carries belts, corsets and plain
// underwear alike, and only one of those is a barrier. This is the same reasoning
// the gesture side uses.
// ═══════════════════════════════════════════════════════════════════════════
// WHAT THE DEVICES ACTUALLY SEAL — DD's own composite answer.
//
// ★ THE GENERAL FORM OF THE RULE THE USER SPOTTED. They noticed a plug has no
// lock of its own and is held in by the belt over it. DD writes that idea out for
// the whole body in zadBQ00: IsBlockedAnal / IsBlockedVaginal / IsBlockedBreast /
// IsBlockedOral fold belt, suit, plug, bra and gag together WITH every
// zad_Permit* exception, and answer "what is physically possible on this body
// right now". The AddOn exposed nothing like it.
//
// ⚠ TRANSCRIBED, NOT REIMPLEMENTED. These four are DD's own logic line for line
// (zadBQ00.psc:816-873). Written in C++ rather than called through Papyrus for
// two reasons: it is read by SkyrimNet on every render cycle for every nearby
// actor, which is exactly the cost the restraint registry was moved into C++ to
// avoid; and it needs no quest, so it works whether or not the ESP is ticked.
//
// ⚠ AND NOTE DD IS INCONSISTENT WITH ITSELF HERE, DELIBERATELY PRESERVED. These
// functions test zad_Permit* on the SPECIFIC covering device
// (`belt.HasKeyword(zad_PermitAnal)`), while the removal path at
// zadEquipScript.psc:436 tests it on the ACTOR
// (`npc.WornHasKeyword(zad_PermitAnal)`). Both are copied as they are: the belt
// clause on a plug's line mirrors the REMOVAL rule, and this block mirrors the
// ACCESS rule. Making them agree would be inventing a third behaviour that DD
// does not have.

// Mirrors zadLibs.GetWornRenderedDeviceByKeyword: the first worn record carrying
// the keyword.
RE::TESObjectARMO* WornByKw(RE::Actor* a, const char* kw)
{
    RE::TESObjectARMO* hit = nullptr;
    if (!a) return hit;
    ForEachWorn(a, [&](RE::TESObjectARMO* w) {
        if (HasKw(w, kw)) { hit = w; return false; }
        return true;
    });
    return hit;
}

// belt / suit / plug, each lifted by its OWN permit keyword.
bool BlockedSite(RE::Actor* a, const char* plugKw, const char* permitKw)
{
    const char* covers[3] = { "zad_DeviousBelt", "zad_DeviousSuit", plugKw };
    for (auto* c : covers) {
        auto* w = WornByKw(a, c);
        if (w && !HasKw(w, permitKw)) return true;
    }
    return false;
}

bool BlockedAnal(RE::Actor* a)    { return BlockedSite(a, "zad_DeviousPlugAnal",    "zad_PermitAnal"); }
bool BlockedVaginal(RE::Actor* a) { return BlockedSite(a, "zad_DeviousPlugVaginal", "zad_PermitVaginal"); }

// ⚠ No permit exception at all on this one - a bra or a suit blocks, full stop.
bool BlockedBreast(RE::Actor* a)
{
    // ⛔ 2026-08-30 (audit finding #117-#152 etc., 42 records): a suit "with
    // Chest Opening" carries zad_DeviousSuit AND zad_ExposedBreasts, and this
    // said "Sealed: ...the breasts" while the device's own line said "cut to
    // leave the breasts bare". A record that deliberately leaves the chest out
    // seals nothing there - the ExposedBreasts marker is DD's own exception.
    bool blocked = false;
    ForEachWorn(a, [&](RE::TESObjectARMO* w) {
        if ((HasKw(w, "zad_DeviousBra") || HasKw(w, "zad_DeviousSuit")) &&
            !HasKw(w, "zad_ExposedBreasts")) {
            blocked = true;
            return false;
        }
        return true;
    });
    return blocked;
}

// ⚠ TWO gags allow oral and both are exceptions here: a PANEL gag (which is why
// GagPanel is carved out by keyword rather than by class) and a RING gag, which
// DD equips carrying zad_PermitOral rather than marking the class.
bool BlockedOral(RE::Actor* a)
{
    // ⚠ ZaZ has its own answer and it is a POSITIVE marker rather than DD's
    // negative one: zbfWornPreventOral says outright that this gag blocks. 26
    // records carry it, and none of them would be caught by the DD test above.
    if (WornByKw(a, "zbfWornPreventOral")) return true;
    return WornByKw(a, "zad_DeviousGag")
        && !WornByKw(a, "zad_DeviousGagPanel")
        && !WornByKw(a, "zad_PermitOral");
}

// The one line. Says only what IS sealed - a body with nothing sealed produces
// nothing, which is the common case and must cost no tokens.
std::string SealedLine(RE::Actor* a)
{
    const char* parts[4] = { nullptr, nullptr, nullptr, nullptr };
    int n = 0;
    if (BlockedOral(a))    parts[n++] = "the mouth";
    if (BlockedVaginal(a)) parts[n++] = "the vagina";
    if (BlockedAnal(a))    parts[n++] = "the anus";
    if (BlockedBreast(a))  parts[n++] = "the breasts";
    if (!n) return {};

    std::string out = "Sealed by what is worn: ";
    for (int i = 0; i < n; ++i) {
        if (i) out += (i == n - 1) ? " and " : ", ";
        out += parts[i];
    }
    return out + ".\n";
}

// What is covering what, computed once per actor. One walk, several answers.
struct Covers {
    bool belt       = false;   // zad_DeviousBelt worn anywhere
    bool bra        = false;   // zad_DeviousBra worn anywhere
    bool permitAnal = false;   // zad_PermitAnal worn anywhere
};

Covers CoversOf(RE::Actor* a)
{
    Covers c;
    if (!a) return c;
    ForEachWorn(a, [&](RE::TESObjectARMO* w) {
        if (HasKw(w, "zad_DeviousBelt"))  c.belt = true;
        if (HasKw(w, "zad_DeviousBra"))   c.bra  = true;
        if (HasKw(w, "zad_PermitAnal"))   c.permitAnal = true;
        return true;
    });
    return c;
}

// ═══════════════════════════════════════════════════════════════════════════
// IS IT A CLAMP?
//
// ⛔ THE CLASS TOKEN CANNOT ANSWER THIS, and that is a live defect in DD's own
// data rather than in ours. zad_DeviousClamps (007EB9, Assets.esm) exists as a
// keyword and is carried by ZERO ARMO records in this load order - verified by
// reverse lookup. The two clamp devices that actually exist,
// zadx_HR_NippleClamps and zadx_HR_RustyNippleClamps, carry
// zad_DeviousPiercingsNipple instead. So until this, a clamped NPC was reported
// to the LLM as "set through the nipples" - a clamp described as a piercing,
// which is the same defect family as YokeFront being described as a spreader.
//
// ⚠ SO IT IS KEYED ON THE NAME - AND ON THE CLASS, because the name alone has a
// real false positive in this load order: zadx_HR_NippleChainCollarInventory is
// named "Iron Collar with Chained Nipple Clamps". That is a COLLAR that happens
// to carry clamps; matching it here would take away its collar treatment (the
// strap tier's chafe line) to give it a clamp curve. Requiring the class to be
// PiercingsNipple keeps the two real clamp devices and excludes the collar.
// The collar's clamps then go undescribed, which is the smaller error.
//
// ⚠ Like the damp axis, this needs the inventory-half name, which arrives from
// the quest script - so it returns false until the ESP is ticked, and a clamp
// then reads as a nipple piercing exactly as it does today. It degrades to the
// current behaviour, never to a wrong one.
bool IsClampDevice(const char* cls, const char* label)
{
    if (!cls || !label || !label[0]) return false;
    if (_stricmp(cls, "PiercingsNipple") == 0) return ContainsNoCase(label, "Clamp");
    // ★ THE TONGUE GRIP (2026-08-27, the user on #276/#279): "276 and 279 even
    // got tongue clip. very painful." Two scold's bridles carry a screw clamp
    // closed on the tongue - censused: exactly 2 records in the load order have
    // "Tongue" in the name, both class Gag, so there is no false positive.
    // A sustained clamp on the tongue is compression ischemia - the exact
    // mechanism the clamp tier is calibrated to (phases at ~5/14/29/58 min,
    // ceiling 5) - and the tongue, being highly vascular, is if anything the
    // conservative case for that curve. The gag tier it displaces capped at 4
    // with jaw wording that never mentioned the tongue at all.
    if (_strnicmp(cls, "Gag", 3) == 0) return ContainsNoCase(label, "Tongue Grip");
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// THE DAMP AXIS - partial-coverage gear that seals, but is not a full suit.
//
// ★ THE USER'S CASE (2026-08-27): "corset, some leg and arm leather cover and
// collar one. those usualy all come in a set of four, the neck, the body, the
// leg and arm. they should also get the damp scale, just not as much as it's
// not a full close suit."
//
// ★ THE SET OF FOUR IS REAL, and it is two product lines in DD Expansion: the
// RESTRICTIVE set (Collar / Corset / Gloves / Boots, in Leather, Ebonite and
// Transparent) and the CATSUIT set (the body suit plus a matching Collar,
// Gloves and Socks filling the neck, hands and feet its own slots leave bare).
// Those resolve to exactly four class tokens the DLL already computes.
//
// ⛔ IT IS NOT A TIER, AND IT CANNOT BE. Collar is already in the strap tier,
// Boots in the limb tier, and Report prints the strain line OR the slow line,
// never both - so a seventh tier would force a leather posture collar to give
// up its chafe line to gain its damp one, when it genuinely does both. Damp is
// a SECOND, INDEPENDENT axis on the same wear clock, appended as its own
// sentence, in the shape the rope surface clause already established.
//
// ⛔ AND NO MATERIAL TEST WORKS ALONE - all three obvious ones were checked and
// failed on the real records:
//   * the zad_Material_* keywords exist in Integration.esm and are referenced by
//     ZERO records of any type (re-verified by reverse lookup, not inherited).
//   * the material NAME alone dies on the Lore Patch, which renames DD's ebonite
//     to "Waxed" and its latex to "Oil" - the flagship latex catsuit is literally
//     "Black Oil Body Suit" and the ebonite corset is "Black Waxed Restrictive
//     Corset". Over a hundred latex records contain none of ebonite/rubber/latex.
//   * the rope-style mesh test cannot reach it: the material prefix lives in the
//     INVENTORY half's _go.nif, and ForEachWorn only ever sees the rendered half.
// What works is CLASS *and* a positive name test, because class alone is wrong
// for Collar and Gloves - half the collars in this load order are iron, steel or
// gold, and "Iron Chain Harness (Wrists)" is class Gloves and is a chain.
//
// ⚠ IT DEGRADES TO SILENCE, NOT TO A GUESS. The name arrives from the inventory
// half via the quest script, so until the ESP is ticked this returns false and
// no damp clause is written. A missing clause is recoverable; a wrong one is not.
// ★ A TALL COLLAR — the one test behind BOTH the head-restriction clause and
// the damp axis (2026-08-27, the user working through the collar list):
//   "251 and 255 are really good, as they restric the head movement and are big
//    enought for the damp scale too. some collar are small and got nothing
//    except the small chafing, some are fully decorating, some are just head
//    restrictive, other restrict and do damp."
//
// ★ AND THE RECORDS SEPARATE THEM CLEANLY, name and mesh agreeing in both
// directions, which is why one predicate can carry both facts:
//     Posture      15 collars, meshes collarposture{steel,leather,ebonite}_go.nif
//     Restrictive   8 collars, all restrictiveCollar_go.nif  (8/8 exactly)
//     the other 90  thin bands, harness straps, chains, rope, padded cuffs
//
// ★ THE PHYSICAL ARGUMENT IS THE USER'S OWN AND IT IS WHY ONE TEST SUFFICES: a
// collar is only able to hold the head because it is TALL, and a tall collar is
// also the only kind with enough surface against the neck to trap heat. Height
// causes both. A 2 cm strap does neither.
//
// ⚠ Class-gated: "Restrictive" also names corsets and boots, which are not this.
// ⚠ Name-keyed, like the posture collar it grew out of - DD ships no keyword.
bool IsTallCollar(const char* cls, const char* label)
{
    if (!cls || !label || _stricmp(cls, "Collar") != 0) return false;
    // ★ "Seer's Collar" (Devious Lore) added 2026-08-29 on the user's model
    // read: "it's a restrictive collar, hold the head in place and do damp
    // and everything." The name carries neither standing needle.
    return ContainsNoCase(label, "Posture") || ContainsNoCase(label, "Restrictive")
        || ContainsNoCase(label, "Seer's Collar");
}

// ★ AND IT HAS TWO RATES, NOT ONE (2026-08-27, the user on #141-149): "there
// are also oil body suit, not just strait jacket, so it also require the
// warm/moist scale up."
//
// ⛔ THEY FOUND A REAL HOLE, AND IT IS 134 DEVICES WIDE. `zad_DeviousSuit` is
// DD's "covers the whole body" tag, and 134 records carry it while resolving to
// a DIFFERENT class - 87 StraitJacket, 25 HobbleSkirt, 21 HobbleSkirtRelaxed,
// 1 YokeBB. Those all earn a POSTURAL tier (arms, legs), so kStrainSuit - which
// is only reachable when the class token is literally "Suit" - never fires for
// them, and the sealed-garment thermal fact was lost outright. "Black Oil Body
// Suit with Straitjacket" is a full latex suit; it was reported as an armbinder
// with no mention that it is sealed against the skin.
//
// ★ THE DAMP AXIS IS EXACTLY THE VEHICLE FOR THIS - it was built independent of
// the strain tier, in its own sentence, precisely so a garment can do two things
// at once. It only needed to learn that some garments seal MORE than a collar.
//
// Returns 0 none / 1 partial / 2 full.
// ⚠ FULL IS DECIDED BY DD'S KEYWORD, not by a class list - behaviour from the
// records, the standing rule.
// ⚠ class "Suit" returns 0 DELIBERATELY: it already gets kStrainSuit, which IS
// the thermal scale, and adding the damp clause on top would state the same
// fact twice in two registers inside one bullet.
// ⚠ THE MATERIAL GATE STILL APPLIES to both rates. A whole-body garment that is
// not leather/latex/rubber does not seal, and rope in particular must never
// acquire a thermal line - it is the one full-body material with airflow.
enum class BootGrade { None, Damp, Leg, Full };        // see BootGradeOf below
BootGrade BootGradeOf(const char* cls, const char* label);   // defined below, used by OcclusionOf
bool IsPointeBoot(const char* cls, const char* label);   // defined below, used by OcclusionOf

int OcclusionOf(RE::TESObjectARMO* w, const char* cls, const char* label)
{
    if (!cls || !label || !label[0]) return 0;
    // ⚠ "Seer's" added 2026-08-29: Devious Lore's Seer set is sealed leather
    // but its names carry no material word - the user ruled the collar and the
    // gloves both take the damp scale.
    // ★ 2026-09-12: "Scribe's" and "Seamstress'" join "Seer's" for the SAME
    // reason it was added - Devious Lore names its sealed-leather/latex sets
    // after their owner and puts no material word in the name at all, so the
    // gate below rejected them on a technicality. The user, reviewing the Other
    // tab: #1460 "latex glove", #1463 "full latex dress with really tight
    // around the leg", #1467 "add damp, it's full arm".
    // ⚠ This is a NAME gate standing in for a material fact. It is the weakest
    // test in this function and the reason three devices were silently dry.
    static const char* mat[] = { "Leather", "Waxed", "Oil", "Ebonite",
                                 "Latex", "Rubber",
                                 "Seer's", "Scribe's", "Seamstress'" };
    bool matOk = false;
    for (auto* m : mat) if (ContainsNoCase(label, m)) { matOk = true; break; }
    if (!matOk) return 0;
    // ★★ POINTE FOOTWEAR AND HEELS NEVER DAMP - AND THE MATERIAL IS IRRELEVANT
    // (the user, 2026-08-30: #335/#339/#340 "painful, but no dampness for
    // those", then #459/#462/#544-#557 "very sharp high heel shoes, no damp
    // scale"). ⛔ An earlier pass got this half-right and kept damp for the
    // SEALED ones; the user's second list is 15 latex records - "Glass-Oil High
    // Heels" and 14 "Oil Ballet Boots" - so material does NOT rescue it.
    // THE MECHANISM: a ballet boot or a heel holds the foot at an extreme angle
    // in an OPEN shoe. It does not sheath and seal the foot, so nothing is
    // trapped against the skin; the pain belongs to the forced angle, which the
    // limb tier and the full-pointe line already carry.
    // ⚠ SOCKS ARE THE DELIBERATE CONTRAST AND KEEP THE DAMP AXIS (#501-#515,
    // "latex type sock, under, the rest of the info is good") - IsSoftFootwear
    // sheathes the foot and lower leg in one seamless piece, which genuinely
    // does trap moisture. Same class, same material, opposite verdict, because
    // the shape differs.
    if (_stricmp(cls, "Boots") == 0 &&
        (IsPointeBoot(cls, label) || ContainsNoCase(label, "Heels")))
        return 0;

    if (_stricmp(cls, "Suit") == 0) return 0;      // kStrainSuit already says it
    if (HasKw(w, "zad_DeviousSuit"))  return 2;    // full coverage, thermal lost

    // ⛔ NOT ALL COLLARS - only the TALL ones (2026-08-27). This replaced two
    // earlier attempts, and the history is worth keeping because the user
    // corrected the same clause three times and was right each time:
    //   1. every collar got it (their 08-24 "set of four" spec) - fired on 55 of
    //      113 and they flagged #162/#178/#179: "it's just a collar".
    //   2. a third "minimal" RATE for all collars - still spoke at 6 game hours,
    //      which is exactly where they were reading, so it did not answer them.
    //   3. this: the question was never the RATE, it was WHICH COLLARS. A tall
    //      collar seals; a strap does not. 23 of 113 qualify.
    // ★ The minimal rate is gone with it - nothing else needed one, and a rate
    // with a single hand-picked user is a number pretending to be a model.
    if (_stricmp(cls, "Collar") == 0) return IsTallCollar(cls, label) ? 1 : 0;

    // ★ 2026-09-12: HobbleSkirt joins the partial-damp classes. The user on
    // #1463 (Seamstress' Dress): "add damp scale to it. It's a full latex dress
    // with really tight around the leg". A sealed latex skirt traps moisture
    // against the legs exactly as sealed gloves do against the hands.
    // ⚠ SCOPE: this admits every hobble skirt whose NAME carries a material
    // word (the gate above), not all of them - so a rope or chain hobble stays
    // dry, which is correct. It is still a broader change than one device.
    // ★ 2026-09-12: LEG-GRADE BOOTS HAVE NO DAMP (the user's 09-11 boot ruling:
    // "leg restraint - pain, no damp"). Full-grade keeps it ("pain and damp") and
    // so do socks and plain Oil Boots ("damp only"), which reach here as Damp.
    if (_stricmp(cls, "Boots") == 0 && BootGradeOf(cls, label) == BootGrade::Leg)
        return 0;
    static const char* worn[] = { "Corset", "Gloves", "Boots", "HobbleSkirt" };
    for (auto* c : worn) if (_stricmp(cls, c) == 0) return 1;   // partial
    return 0;
}

// ⚠ HALF THE SUIT'S RATE AND A LOWER CEILING - the user's "not as much" on both
// levers at once, which is how the rope tier expresses the same idea. A sealed
// suit reaches its skin-softening endpoint at 12 game hours; a collar and a pair
// of gloves never reach it at all, because they leave most of the body open to
// the air and the heat has somewhere to go.
// ⚠ Returns 0 below the first boundary, so the caller can test it. An earlier
// draft started the counter at 1 and could never say "not yet".
constexpr float kDampMult = 2.0f;

// ⚠ TWO RATES. Partial gear (a collar, a pair of gloves) leaves most of the body
// open to the air, so it is paced at half a suit's rate and stops at phase 4.
// A FULL sealed garment is a suit in everything but its class token, so it takes
// the suit's own rate and reaches phase 5 - anything else would have an
// identical latex catsuit report two different states depending on whether its
// straitjacket half happened to win ClassOf.
// TWO rates:
//   1 partial  corset / gloves / boots / a TALL collar   mult 2.0 ceil 4
//   2 full     anything carrying zad_DeviousSuit         mult 1.0 ceil 5
int DampPhaseOf(float hours, int occl)
{
    if (!g_scLoaded) LoadScalesIni();
    const float mult = (occl >= 2) ? g_sc.dampSealedMult : g_sc.dampMult;
    const int   ceil = (occl >= 2) ? g_sc.dampSealedCeil : g_sc.dampCeil;
    int p = 0;
    for (int i = 0; i < 4; ++i) if (hours >= g_sc.phase[i] * mult) p = i + 2;
    return (std::min)(p, ceil);
}

const char* DampLine(int phase)
{
    switch (phase) {
    case 2: return "It has grown warm underneath where it seals";
    case 3: return "The skin under it has stayed damp and has not dried";
    case 4: return "It is slick against the skin everywhere it holds, and stays that way";
    default: return nullptr;
    }
}

// ★ A BALLET BOOT IS NOT A HIGH HEEL (2026-08-27, the user on #156 "Iron Ring
// Slave Boots"): "those are high heel shoes, but the bdsm type. made of iron,
// force one to stay on their toe."
//
// ⛔ CLASS CANNOT TELL THEM APART. All 108 of them are class Boots and share one
// ClassLine, "forces the feet into a steep arch" - which is true of a heeled
// bondage boot and UNDERSTATES a ballet boot, where the foot is locked at full
// pointe and the entire body weight rides on the toes. That is a different
// mechanism, not a stronger version of the same one, and it is the reason these
// are worn as a restraint at all: at full pointe she cannot walk normally, run,
// or brace.
//
// ⚠ KEYED ON THE NAME, and legitimately so - the same argument as the posture
// collar. DD ships no keyword for it, the distinction is real, and the naming is
// consistent: 32 records say "Ballet" and 2 say "Ring Slave" (#156 and its
// unlocked twin), which is DD-Lore's name for the iron toe-ring version.
// ⚠ PONY BOOTS (19) ARE DELIBERATELY EXCLUDED. A pony boot is a hoof-style
// platform that forces a high step, which the PonyGear/limb handling already
// describes - it is not pointe.
// ⛔ SOCKS ARE NOT BONDAGE BOOTS (2026-08-28, found by a name-cluster sweep).
// 17 records named "<colour> Oil Socks" carry zad_DeviousBoots, so class Boots
// won and they took the LIMB tier - the full postural curve, climbing to phase 5
// NERVE DAMAGE - plus the line "forces the feet into a steep arch". A latex sock
// forces nothing. This was the model inventing sustained injury from socks, and
// it is the same defect family as the ballet boot in reverse: there the line was
// too weak, here the whole CURVE is fabricated.
//
// ⚠ NAME-KEYED, and zad_effect_noTripping is NOT the test: it is carried by 33
// records including Boots and Heels, so it marks "these do not trip you", not
// "these are soft". Checked before being used.
// ⚠ They KEEP the damp axis - a sealed latex sock genuinely traps moisture
// against the foot, and OcclusionOf reaches it through class Boots + "Oil".
bool IsSoftFootwear(const char* cls, const char* label)
{
    if (!cls || !label || _stricmp(cls, "Boots") != 0) return false;
    // ★ PLAIN LATEX BOOTS JOIN THE SOCKS (2026-08-28, the user on #847-862):
    // "just normal boots, just damp". 17 records end in "Oil Boots" - ordinary
    // footwear in a sealed material, which earns the damp axis and no pain at
    // all.
    // ⛔ AND THE TEST IS THE FULL PHRASE, NOT "no qualifier in the name". The
    // 22 boots with no ballet/pony/heel/socks qualifier ALSO include
    // "Iron Torture Boots" (#315/#317) and two ankle chain harnesses - a blanket
    // "unqualified means ordinary" rule would have disarmed the torture boots,
    // which is the exact opposite of what they are. Matched on the phrase the
    // ordinary family actually shares.
    // ⛔ SUPERSEDED 2026-09-12: this used to say it "catches #1094 Dwarven Gilded
    // Oil Boots too ... the same object in a fancier finish". The user's 09-11
    // boot ruling put #1094 in the LEG-restraint list, so it is excluded - and
    // it had been losing its restraint entirely as a result.
    return (ContainsNoCase(label, "Socks") || ContainsNoCase(label, "Oil Boots")) &&
           !ContainsNoCase(label, "Dwarven Gilded");
}

// ★★ THE BOOT GRADE (the user, 2026-09-11, all 108 DD boots). Mirrors
// dd_wornlogic.boot_grade(), whose rule was DERIVED from the user's three lists
// and verified to reproduce them 108/108 (41 leg / 34 full / 33 damp).
// ⛔ NAME-KEYED BECAUSE IT MUST BE: every one of the 108 carries exactly
// zad_Lockable + zad_DeviousBoots, so the grades are keyword-identical.
//   Damp (33) socks / plain Oil Boots - no restraint state, KEEP damp, no pain
//   Full (34) pain AND damp, and the legs restraint state (the shuffle sentence)
//   Leg  (41) pain only, NO damp, NOT the legs state - its own lighter sentence
// ★ "(Short)" OVERRIDES "(Tight)": the same ballet boot at full height is Full,
// its short-shaft twin is only Leg. The pairs interleave in the census.
BootGrade BootGradeOf(const char* cls, const char* label)
{
    if (!cls || !label || _stricmp(cls, "Boots") != 0) return BootGrade::None;
    // ⛔ A NAMELESS BOOT CANNOT BE GRADED, so it keeps its PRE-RULING behaviour
    // (legs state + damp = Full). Two real cases: Devious Lore's three pony boots
    // (#1446-1448, "the rules are good") have no display name on either half, and
    // any DD boot worn before the session starts is nameless until ReconcileNames
    // sweeps it. Grading "" by the rule below would fall through to Leg and
    // silently strip what those boots already had.
    if (!label[0]) return BootGrade::Full;
    if (IsSoftFootwear(cls, label)) return BootGrade::Damp;
    if (ContainsNoCase(label, "Pony") || ContainsNoCase(label, "Restrictive") ||
        ContainsNoCase(label, "Thelia's Training Boots"))
        return BootGrade::Full;
    if (ContainsNoCase(label, "(Tight)") && !ContainsNoCase(label, "(Short)"))
        return BootGrade::Full;
    return BootGrade::Leg;
}

// ★ A STEEP HEEL IS NOT A POINTE BOOT (2026-08-28, the user on #888-893):
// "scale 1 to 2, they are really steep high heel, but not that bad."
// ⛔ KEYED ON "Bondage Heels", NOT ON "Heels". The heel family also holds
// "Ballet Heels" (#335/#339/#340), which ARE pointe and keep the full boot
// ceiling - matching the shorter word would have quietly softened them.
bool IsModestHeel(const char* cls, const char* label)
{
    if (!cls || !label || _stricmp(cls, "Boots") != 0) return false;
    return ContainsNoCase(label, "Bondage Heels");
}

bool IsPointeBoot(const char* cls, const char* label)
{
    if (!cls || !label || _stricmp(cls, "Boots") != 0) return false;
    // ★ WIDENED 2026-08-30 (the user, on #156-#317): "ring boots, small, force
    // the foot a certain way, mid layer, for pain. 292, 304 is strait down."
    // Torture Boots and the Chain Harness (Ankles) pair join Ballet/Ring Slave
    // on the forced-foot line; the class keeps the Boots limb pain tier.
    return ContainsNoCase(label, "Ballet") || ContainsNoCase(label, "Ring Slave") ||
           ContainsNoCase(label, "Torture Boots") ||
           ContainsNoCase(label, "Chain Harness (Ankles)");
}

// ★ NAME-KEYED CLASS OVERRIDES (2026-08-30) - the record's own keywords
// mislead; the user rules by the device. ONE home, called by every resolver
// (Report, VisibleOn, ClothingBlockOn) so the three can never disagree.
//   #299 "Iron Collar with Chained Nipple Clamps" carries
//   zad_DeviousPiercingsNipple and ranked as a piercing - the user: "Make 299
//   a normal collar, outer, chafing 1-2." The clamp TIER already excluded it
//   by name (a collar, not clamps); now the class follows, which brings the
//   Outer layer and the strap chafing tier with it.
static void ReclassByName(char* cls, std::size_t cap, const char* label)
{
    if (!cls || !cls[0] || !label || !label[0]) return;
    if (_stricmp(cls, "PiercingsNipple") == 0 &&
        ContainsNoCase(label, "Collar with Chained Nipple"))
        std::snprintf(cls, cap, "Collar");
}

// ★ THE PONY-TAIL PLUGS (the user, 2026-08-30): "those are visible. All pony
// tail plug are visible, not the plug, the tails itself... they are still
// internal, but stay visible throughout." 4 DD records, all named
// "Pony Tail Plug"; the test is class Plug* + "Tail" so a content mod's fox
// or horse tail lands here too.
// ★ 2026-09-12: ask the WORN MESH as well as the name. Devious Lore's three pony
// tails (_DL_PonyTail01/02/03, visualiser rows 1456-1458) have an EMPTY display
// name on BOTH halves, so the name test alone failed them: they were treated as
// ordinary hidden plugs - never visible, no tail line - while DD's four "Pony Tail
// Plug" worked. All seven render devious\Heretic\Plugs\PlugPonyTail0*_1.nif, so
// the mesh is the one signal present on every one of them. It is also the right
// signal: the tail IS the worn mesh. Same shape as WornMeshIsBlindfold.
bool WornMeshIsTail(RE::TESObjectARMO* w)
{
    if (!w) return false;
    for (auto* aa : w->armorAddons) {
        if (!aa) continue;
        for (int s = 0; s < 2; ++s) {                 // female then male
            const char* p = aa->bipedModels[s].GetModel();
            if (p && ContainsNoCase(p, "PonyTail")) return true;
        }
    }
    return false;
}

bool IsTailPlug(const char* cls, const char* label, RE::TESObjectARMO* w)
{
    if (!cls || _strnicmp(cls, "Plug", 4) != 0) return false;
    if (label && ContainsNoCase(label, "Tail")) return true;
    return WornMeshIsTail(w);
}

bool IsRopeDevice(RE::TESObjectARMO* w, const char* label)
{
    if (const auto* kl = KeywordlessOf(w); kl && kl->rope) return true;   // Job B (Pama's nooses)
    if (w) {
        for (int i = 0; i < 2; ++i) {                       // male, female
            const char* path = w->worldModels[i].GetModel();
            if (ContainsNoCase(path, "ropebundle")) return true;
        }
    }
    // The name carries it for DD pairs, whose rendered half has no world model.
    // ⚠ "rope" as a substring is safe here: it is checked only on DEVICE names,
    // and no device class in this load order contains it incidentally.
    return ContainsNoCase(label, "rope");
}

// The classes whose NAME says nothing about whether they restrain. An armbinder
// or a straitjacket needs no extra clause - ClassLine already says what it does
// to the arms - but a "cuff", a "collar" and a "harness" can each be either a
// genuine restraint or an ornament, and that is precisely the ambiguity the user
// asked to resolve. The clause is spent only where it buys something.
bool ClassIsAmbiguous(const char* cls)
{
    // ⛔ "Boots" REMOVED 2026-08-27 — it made 107 of 108 boots contradict
    // themselves inside one sentence. ClassLine("Boots") already asserts a
    // restriction ("forces the feet into a steep arch"), and only ONE boot in
    // the load order carries a movement keyword, so every other one fell
    // through RestraintOf to the ornament clause and read:
    //     "forces the feet into a steep arch ... and it fastens to nothing and
    //      leaves the body free to move"
    // The ambiguity this list exists to resolve is real for a cuff or a collar,
    // which can genuinely be either. It is not real for a DD bondage boot:
    // there is no decorative reading, and the class line has already said what
    // it does. Nothing TRUE is lost — the single boot with a movement keyword
    // still has its class line saying it forces the arch.
    // ★ This also fixes the pointe boot for free, which would otherwise have
    // needed a posture-collar-style suppression of its own.
    // ⛔ Belt, Bra and Corset REMOVED 2026-08-30 (audit: 76 gated devices read
    // "locked over the hips ... and it fastens to nothing and leaves the body
    // free to move" in one sentence). The identical reasoning that removed
    // Boots on 08-27: their ClassLine already asserts a restriction, and no
    // record of those classes carries a restraint keyword, so nothing true is
    // lost. Collar, the cuffs, Harness and Gloves keep the clause - those can
    // genuinely be either.
    static const char* amb[] = {
        "ArmCuffs", "CuffsArms", "CuffsFront", "LegCuffs", "CuffsLegs",
        "AnkleShackles", "Collar", "Harness",
        // ⚠ BondageMittens ADDED 2026-08-28: without a seat here RestraintLine
        // is never consulted, so the new HandsSealed answer would have been
        // computed and then thrown away.
        "Gloves", "BondageMittens",
    };
    for (auto* a : amb) if (_stricmp(cls, a) == 0) return true;
    return false;
}

Restraint RestraintOf(RE::TESObjectARMO* w)
{
    if (!w) return Restraint::None;
    // Job B: no keywords to read - the record's row says. Only 1545 binds ("behind the back").
    if (const auto* kl = KeywordlessOf(w))
        return kl->handsBack ? Restraint::HandsBack : Restraint::None;

    // ⛔ SPECIFIC FRONT KEYWORDS FIRST - and this ordering is a live bug fix.
    // The back[] list below contains zad_DeviousHeavyBondage, which is DD's
    // GENERIC "the wrists are bound" capability flag, not a posture. Every
    // front-restraint device carries it too, so with back[] tested first, all
    // SEVEN devices in this load order that carry a front keyword resolved to
    // HandsBack - "Iron Handcuffs" and "Iron Yoke (Fiddle)" included - and the
    // HandsFront line never fired once in 1,221 devices. It was dead code, and
    // every front restraint was telling the LLM her hands were behind her back.
    //
    // ★ This is the same generic-beats-specific trap as ClassRank's Suit /
    // StraitJacket, Yoke / YokeFront and Gag / GagRing: DD keeps the broad
    // capability keyword and the precise posture keyword on the SAME record, and
    // whichever is tested first wins. The rule is always the same - the specific
    // one has to be asked first.
    // ⚠ FIRST of all, because a mitten also carries zad_DeviousGloves and would
    // otherwise fall through every posture list to None.
    if (HasKw(w, "zad_DeviousBondageMittens")) return Restraint::HandsSealed;

    static const char* front[] = {
        "zad_DeviousCuffsFront", "zadNG_DeviousYokeFront",
        "zbfAnimHandsInHandCuffsFront", "DOMWornCuffsFront",
        // ★ 2026-08-29, from the #1363-#1382 census: ZaZ states front/back
        // explicitly per record. Irons-in-front (7) and rope/scarf crossed in
        // front (3) said "behind the back" without these rows.
        "zbfAnimHandsToFrontInIrons", "zbfAnimHandsCrossedToFrontInRopes",
        // ★ the two ZaZ fiddles (#1389/#1390): wrists clamped in front of the
        // face, exactly DD's YokeFront - the user: "just in front of the body".
        "zbfAnimHandsFiddle", "zbfAnimHandsKoffiFiddle",
        // hands tied down to a waist rope sit at the FRONT of the body
        // (#1397/98, #1403/04); the generic fall-through said "behind".
        "zbfAnimHandsWaist",
        // DoM's Prisoner's Cuffs: wrists crossed in front (slot-33 hand items).
        "DOMWornCuffsCrossed",
        // ★ 2026-09-13: ZaZ's rope shackles tied to a neck rope (rows 1265/1266) - "a wrist rope
        // shackle type" (the user). Without this their NoSprint effect reported the LEGS.
        "zbfAnimHandsAroundNeck",
    };
    for (auto* k : front) if (HasKw(w, k)) return Restraint::HandsFront;

    // ⛔ A STRAITJACKET FOLDS THE ARMS IN FRONT (2026-08-28). It sat in back[]
    // and answered "behind the back" while ClassLine said "wraps the arms
    // across the body" on the same device - the model contradicting itself.
    if (HasKw(w, "zad_DeviousStraitJacket")) return Restraint::HandsCrossed;

    // ⛔ AND A YOKE HOLDS THEM AT THE HEAD, not behind. Same list, same defect.
    // ⚠ YokeFront is NOT here - it is already in front[] above, which is
    // correct: a fiddle clamps the wrists together in front of the face.
    if (HasKw(w, "zad_DeviousYoke") || HasKw(w, "zad_DeviousYokeBB") ||
        // ★ 2026-08-29 final sweep: the 11 ZaZ yokes and the 2 cross poles
        // carried no mapped pose and fell through to the generic flag -
        // "hands held behind the back" for a device that holds them out level.
        HasKw(w, "zbfAnimHandsYoke") || HasKw(w, "zbfAnimHandsCrossPole") ||
        // ★ 2026-09-12: DoM's Wooden Yoke. ClassOf's second resolver has mapped
        // DOMWornYoke -> Yoke since 2026-08-30, but RestraintOf never received
        // it, so the device was CLASSED as a yoke and simultaneously reported
        // as having free hands. The user, reviewing the Other tab: "yoke, can't
        // do action need to be applied".
        HasKw(w, "DOMWornYoke"))
        return Restraint::HandsHead;

    // ── hands bound BEHIND the back ─────────────────────────────────────────
    // ⚠ SPECIFIC POSTURES ONLY. The generic capability flags moved OUT of this
    // list, below the movement row - see why there.
    static const char* back[] = {
        "zad_DeviousArmbinder", "zad_DeviousArmbinderElbow", "zad_DeviousElbowTie",
        "zadNG_DeviousBoxbinder", "zad_DeviousButterfly",
        "zbfAnimHandsInHandCuffsBackside",
        // ★ 2026-08-29: the ZaZ binder poses are all behind-the-back — the
        // box-tie by the user's own ruling, the elbow ropes by SHARED MESH
        // with it (ElbowRope01/02.nif renders in one place), armbinder by name.
        "zbfAnimHandsElbows", "zbfAnimHandsBoxTied",
        "zbfAnimHandsArmbinder", "zbfAnimHandsFullyRopedArms",
        // ★ 2026-08-29 round 7: the back-pole binder (#1396) and the shaming
        // wheel (#1405) both fix the arms BEHIND the back.
        "zbfAnimHandsAndNeckVerticallyPoled", "zbfAnimHandsWheeled",
        // DoM (2026-08-29): its Wrist Rope poses the hands behind the back.
        "DOMWornCuffsBack",
        // ★ 2026-09-12: DoM's armbinder. Its CLASS row (DOMWornArmbinder ->
        // Armbinder) has existed since 2026-08-30; the posture row did not.
        "DOMWornArmbinder",
    };
    for (auto* k : back) if (HasKw(w, k)) return Restraint::HandsBack;

    // ── the legs, not the hands ─────────────────────────────────────────────
    static const char* move[] = {
        "zad_DeviousHobbleSkirt", "zad_BoundCombatDisableKick",
        "zad_DeviousPonyGear", "zad_DeviousPetSuit",
        "zbfEffectSlowMove", "zbfEffectNoSprint", "zbfEffectNoMove",
        // ★ 2026-09-12: DoM binds the ankles with its own keyword.
        "DOMWornAnkle",
    };
    // ★ 2026-09-13 (the user on row 1506 "Cuffs Rope", wrist AND ankle ropes: "wrist"). When a DoM
    // record binds both, the WRIST wins - so DOMWornAnkle yields to the generic wrist row below.
    // Census: 1506 is the only record carrying both.
    const bool domWrist = HasKw(w, "DOMWornWrist");
    for (auto* k : move) {
        if (domWrist && _stricmp(k, "DOMWornAnkle") == 0) continue;
        if (HasKw(w, k)) return Restraint::Movement;
    }

    // ⛔ THE GENERIC CAPABILITY FLAGS, ABSOLUTELY LAST - and moving them here is
    // a THIRD fix, not cosmetics. They used to sit at the end of back[], which
    // meant the WHOLE of back[] was tested before move[] ever ran: a PET SUIT
    // carries zad_DeviousPetSuit AND zad_DeviousHeavyBondage, so the generic
    // flag matched first and DD's own all-fours device reported "hands held
    // behind the back" (#1149). Same for anything that pairs a movement
    // restraint with the capability marker.
    // ★ One ordering rule covers all three of today's defects: EVERY specific
    // posture, then movement, then the flag that only says "bound, somewhere".
    // ★ 2026-09-12: DOMWornWrist is DoM's GENERIC "the wrists are bound" marker,
    // and it belongs HERE for exactly the reason the rest of this row exists.
    // Seven devices carry it, and on every one that also states a posture the
    // specific keyword must win: Wrist Rope pairs it with DOMWornCuffsBack,
    // Prisoner's Cuffs with DOMWornCuffsCrossed, the Wooden Yoke with
    // DOMWornYoke. Tested earlier it would have re-created the generic-beats-
    // specific bug this row was moved down here to fix.
    if (HasKw(w, "zad_DeviousHeavyBondage") || HasKw(w, "zbfEffectNoFighting") ||
        HasKw(w, "DOMWornWrist"))
        return Restraint::HandsBack;

    // ⚠ HobbleSkirtRelaxed is deliberately absent above: DD ships it as the
    // RELAXED counterpart, i.e. the state where the skirt is not restricting.
    return Restraint::None;
}

// The clause appended to a device line saying what it does to her. Empty for a
// device that does nothing, because a device that does nothing needs no clause -
// the class description has already said what it is.
//
// ⚠ Physical mechanism only. Whether being unable to use her hands is
// frightening, humiliating or unremarkable is the LLM's to decide.
const char* RestraintLine(Restraint r)
{
    switch (r) {
    // ⛔ THE FOURTH INSTANCE OF THE PRONOUN RULE BEING BROKEN (2026-08-27),
    // and the worst: FIVE pronouns across three lines, in the file that states
    // the rule twice. MaterialLine had seven, StrainLine two, OrnamentLine one.
    // Males have been covered since PPB 2.0.0. Write round it every time.
    case Restraint::HandsBack:
        return "the hands are held behind the back and cannot be used";
    case Restraint::HandsFront:
        return "the wrists are held together in front";
    case Restraint::Movement:
        return "the stride is shortened and no speed is possible";
    case Restraint::HandsSealed:
        return "the fingers are sealed inside and can take hold of nothing";
    case Restraint::HandsCrossed:
        return "the arms are folded across the chest and buckled down there";
    case Restraint::HandsHead:
        return "the wrists are held up at either side of the head";
    default:
        return "";
    }
}

// ★ The other half of the answer, and the one that stops a wrong inference.
// Cuffs and collars READ as restraint, so an LLM shown "she is wearing gold arm
// cuffs" will reasonably conclude her hands are tied - when in this load order
// almost none of them bind. Of every record named "cuffs", exactly four carry
// the binding keyword. Saying so explicitly is cheaper than the wrong assumption.
const char* OrnamentLine()
{
    // ⚠ was "leaves HER free to move" - a pronoun, in a file that states the
    // no-pronoun rule twice. Males wear these too.
    return "it fastens to nothing and leaves the body free to move";
}

const char* ClassLine(const char* cls)
{
    if (!cls || !cls[0]) return nullptr;
    struct Row { const char* c; const char* s; };
    static const Row rows[] = {
        // mouth
        { "Gag",            "fills the mouth and holds the jaw open" },
        { "GagLarge",       "fills the mouth completely and holds the jaw wide" },
        { "GagRing",        "holds the mouth open around a rigid ring" },
        { "GagBit",         "holds a bar between the teeth" },
        { "GagPanel",       "seals the mouth behind a fitted panel" },
        { "GagTape",        "seals the lips shut" },
        { "GagInflatable",  "fills the mouth and can be pumped tighter" },
        // sight
        { "Blindfold",      "covers the eyes" },
        { "Hood",           "encloses the head, dulling sight and sound" },
        // arms
        { "Armbinder",      "holds the arms folded behind the back" },
        { "ArmbinderElbow", "draws the elbows together behind the back" },
        { "ElbowTie",       "binds the elbows together" },
        { "StraitJacket",   "wraps the arms across the body and holds them there" },
        { "Yoke",           "holds the arms out and level on a rigid bar" },
        // ⚠ "BB" is BREAST - the inventory record is literally "Iron Breast Yoke", and
        // the pose file is breast-yoke.json. Measured by FK: the hands sit 72.6 cm
        // apart, level with the shoulders and carried forward of them. It is a
        // spreader held out in front of the body, and unlike every other yoke it
        // does NOT cross the neck.
        { "YokeBB",         "holds the wrists wide apart on a bar carried out in front of the chest" },
        // ⛔ YokeFront had NO ROW until 2026-08-26, and Report() returns early on
        // an unmapped class - so `zadNG_DeviousYokeFront` (Expansion.esm) was
        // dropped from the block ENTIRELY. An NPC locked into a front yoke read
        // as wearing nothing at all. It is the only DD class token in this load
        // order that had no row.
        // ⛔ CORRECTED 2026-08-27. The row here first said "holds the arms out and
        // level on a rigid bar in front of the body", which is not what this
        // device is. Forward kinematics on DD NG's own pose file frontyoke.json
        // puts the wrists TOGETHER - 11.3 cm apart - about 27 cm in front of the
        // throat at chin height. That is a shrew's FIDDLE: a board with three
        // holes, neck and both wrists, clamped in front of the face. DD names it
        // so itself (zadNG_yokeFront_ID = "Iron Yoke (Fiddle)").
        { "YokeFront",      "clamps the neck and both wrists into one board held up in front of the face" },
        { "Boxbinder",      "holds the forearms boxed together behind the back" },
        { "HeavyBondage",   "holds the arms bound and immobile" },
        { "BondageMittens", "encases the hands, leaving the fingers useless" },
        { "CuffsFront",     "locks the wrists together in front" },
        // legs
        { "AnkleShackles",  "links the ankles, shortening every step" },
        { "HobbleSkirt",    "binds the legs, shortening every step" },
        { "HobbleSkirtRelaxed", "binds the legs, shortening the stride" },
        { "PetSuit",        "holds the limbs folded, leaving only all fours" },
        { "PonyGear",       "forces an upright posture and a high step" },
        { "Boots",          "forces the feet into a steep arch" },
        // straps
        // ★ ARM cuffs sit on the ARM, not the wrist (the user, 2026-08-30).
        // Measured: all 33 ArmCuffs records are named "... Arm Cuffs" and carry
        // biped slot 59; the wrist-BINDING class is CuffsFront (slot 46) and it
        // keeps its own "wrists held together" line.
        { "ArmCuffs",       "locked around the arms" },
        { "CuffsArms",      "locked around the arms" },
        // ★ DECORATIVE LEG CUFFS, not shackles (the user, 2026-08-30): bands
        // around the mid upper and lower leg. They carry NO restraint keyword
        // (absent from the legs registry row, which holds AnkleShackles), and
        // they coexist with shackles - so they must not borrow the ankle
        // language. AnkleShackles keeps its own row below.
        { "LegCuffs",       "locked around the legs" },
        { "CuffsLegs",      "locked around the legs" },
        { "Collar",         "locked around the throat" },
        { "Harness",        "straps crossing the body" },
        // torso
        { "Corset",         "cinched tight around the ribs, so breathing stays shallow" },
        { "Bra",            "holds the breasts rigidly enclosed" },
        { "Suit",           "sealed against the skin, holding in heat and damp" },
        { "Belt",           "locked over the hips, covering everything beneath" },
        { "Gloves",         "sheathes the hands" },
        // intimate
        { "Plug",           "seated inside" },
        { "PlugVaginal",    "seated inside" },
        { "PlugAnal",       "seated inside" },
        { "PiercingsNipple","set through the nipples" },
        { "PiercingsVaginal","set through the intimate flesh" },
        { "Clamps",         "biting down on the flesh they grip" },
        // ⛔ CORRECTED 2026-08-30 (audit): zad_DeviousButterfly is DD's
        // butterfly ARMBINDER - 6 records, all class Armbinder, slot 46 - not
        // a genital device. Only keyword storage order kept the old row from
        // ever winning; now the row tells the truth for the day it does.
        { "Butterfly",      "folds the arms behind the back, elbows drawn out to either side" },
    };
    for (auto& r : rows) if (_stricmp(cls, r.c) == 0) return r.s;
    return nullptr;
}

// The MATERIAL / soulgem clause, read off the display name. Ordered strongest
// first so "Black Soulgem" is never caught by the bare "Soulgem" row.
//
// ★ The soulgem ladder is the user's own spec (2026-08-26): the plain and common
// gems tickle, and the greater gems reach into the wearer's own magicka. Black
// soul gems sit at the top because that is what they are in the game's fiction —
// they take a person's soul, not an animal's.
// ⚠ NO PRONOUNS IN ANY RETURNED STRING. This function does not know the
// wearer's sex, and males have been covered since PPB 2.0.0. Seven lines
// here said "her" until the 08-26 sweep. Write round it - "the skin",
// "the wearer's", "body heat" - never guess a pronoun.
const char* MaterialLine(const char* name, bool& magical)
{
    magical = false;
    if (!name || !name[0]) return nullptr;

    // ── ORDER IS LOAD-BEARING ────────────────────────────────────────────────
    // Every test is a substring of the display name, so the SPECIFIC rows have
    // to come before the general ones. "Plug (Soulgem) (Chargeable)" contains
    // "Soulgem"; "Black Soulgem Piercing" contains "Soulgem". First match wins,
    // so the ladder runs strongest-and-most-specific downward.

    // active hardware — says more about the device than its material does
    if (NameHas(name, "Chaos"))     { magical = true;  return "and it runs unpredictably, sometimes a buzz and sometimes a jolt"; }
    if (NameHas(name, "Shock"))     { magical = true;  return "and it delivers a sharp jolt from time to time"; }
    if (NameHas(name, "Chargeable")){ magical = true;  return "and it draws steadily on the wearer's own magicka to charge itself"; }
    if (NameHas(name, "Training"))  { magical = true;  return "and it answers whenever magicka is drawn"; }

    // ── THE SOULGEM LADDER ───────────────────────────────────────────────────
    // The user's spec (2026-08-26): "add something to each one that got soulgem
    // about vibration and warm, from mild to stronger. nothing burning, just make
    // it that lesser can be slightly felt, black and filled are strongest."
    //
    // ★ Note the ordering is by CHARGE, not by gem size, which is why Filled and
    // Black sit above Grand and Greater: a filled gem holds a soul and a black
    // one holds a person's, while an empty grand gem is just a large empty gem.
    // That is the game's own fiction and it matches the user's ranking exactly.
    //
    // ⚠ Warmth and tingling only. Nothing burns, nothing hurts — these are
    // magical devices working as intended, not injuries.
    if (NameHas(name, "Black Soulgem") || NameHas(name, "Black Soul Gem"))
        { magical = true; return "and the black gem runs warm and alive against the skin, a deep insistent tingling that rises and falls without ever quite stopping, drawing on the wearer's own magicka as it goes"; }
    if (NameHas(name, "Filled Soulgem") || NameHas(name, "Filled Soul Gem"))
        { magical = true; return "and the filled gem runs warm with a deep, steady tingling that keeps returning of its own accord"; }

    // ── WELKYND (Deviously Accessible) ───────────────────────────────────────
    // ⚠ THE NAME AND THE EDITORID DISAGREE: dwp_plugGreaterSoulgem* is named
    // "Plug (Welkynd)". Reading the name — which is what this function does and
    // what the player sees — would have missed it entirely and fallen through to
    // no clause at all. Found by the user naming the device (Brelyna wears them).
    //
    // The wording is not decoration: the Welkynd plugs really do carry
    // zad_EffectVibrateOnSpellCast, so "tingles harder when she draws on
    // magicka" is the actual mechanic, and the piercings carry
    // zad_EffectVibratingWeak.
    if (NameHas(name, "Atronach"))
        { magical = true; return "and the atronach crystal in it runs hot and cold by turns, never settling, with a charge that rises whenever magicka moves nearby"; }
    if (NameHas(name, "Welkynd"))
        { magical = true; return "and the Welkynd stone holds a steady warmth and a light tingling, sharpening for a moment whenever magicka is drawn"; }

    if (NameHas(name, "Grand Soulgem") || NameHas(name, "Grand Soul Gem"))
        { magical = true; return "and the grand gem gives off a steady warmth and a slow tingling that comes and goes on its own"; }
    if (NameHas(name, "Greater Soulgem") || NameHas(name, "Greater Soul Gem"))
        { magical = true; return "and the greater gem gives off a mild warmth and a slow tingling that comes and goes on its own"; }
    if (NameHas(name, "Soulgem") || NameHas(name, "Soul Gem"))
        { magical = true; return "and the gem gives off a faint warmth and a tingle barely at the edge of noticing"; }

    // ── other Deviously Accessible materials ─────────────────────────────────
    // "Unknown Crystal" / "Strange" gear carries zad_EffectVibratingRandom, so
    // the unpredictability in the wording is the device's real behaviour.
    // ⚠ The bare "Strange" needle was dropped in the 08-26 sweep. Deviously
    // Accessible names a Strange Chastity Belt, a Strange Collar and a Strange
    // Chain Harness - none of which contain a crystal - so it was inventing
    // hardware. A missing clause on Strange Piercings is the cheaper error.
    if (NameHas(name, "Unknown Crystal"))
        { magical = true; return "and the crystal in it warms and stirs at odd intervals, in no pattern that can be predicted"; }
    if (NameHas(name, "Stalhrim"))   return "and the stalhrim stays bitterly cold and never takes body heat";
    if (NameHas(name, "Soapstone"))  return "and the soapstone is smooth and faintly soft, warm where it rests";
    if (NameHas(name, "Copper"))     return "and the copper is smooth and quick to take body heat";

    // ⛔ SEALED MATERIALS BEFORE COLOUR WORDS (2026-08-30, audit finding).
    // The Gold row added on 08-29 hijacked the 13 "Golden Oil" devices - latex
    // in a gold FINISH - and told the LLM "the gold is smooth and heavy" about
    // a sealed rubber garment. A colour word must never outrank the material.
    //
    // ★ And fixing it closes a much older hole: 483 devices carry a sealed
    // material word in the name (Oil 249 / Waxed 174 / Ebonite 60) and 482 of
    // them got NO material clause at all, because the Lore Patch renames DD's
    // ebonite to "Waxed" and its latex to "Oil" and only the bare "Ebonite"
    // row existed. Same wording as that row - it is the same material.
    // ⚠ "Oil" as a bare substring is safe here, measured: 249 names contain it
    // and in 249 of 249 it stands as its own word. OcclusionOf and
    // IsSoftFootwear already trust it the same way.
    if (NameHas(name, "Oil") || NameHas(name, "Waxed") || NameHas(name, "Latex") ||
        NameHas(name, "Rubber") || NameHas(name, "Ebonite"))
        return "and the rubber is smooth and airtight";

    // ★ 2026-08-29, the final ZaZ sweep: 86 wearables missed every needle, and
    // the missing words are shared with DD (gold ceremonial gear!). Wood BEFORE
    // Gold - "Wooden Yoke with Golden Metalwork" is a wooden yoke.
    // ⚠ "with Wood Bit" is a PART, not the device (2026-08-30, audit finding):
    // 4 records are iron scold's bridles and iron gags carrying a wooden bit,
    // and the 08-29 Wood row described the whole cage as wood. The two real
    // "Wood Bit Gag" records ARE wood and keep the row - the discriminator is
    // the word "with", the same full-phrase technique as the Oil Boots rule.
    if (NameHas(name, "Wood") && !NameHas(name, "with Wood Bit"))
        return "and the wood is stiff and unyielding, rough-grained against the skin";
    if (NameHas(name, "Gold"))       return "and the gold is smooth and heavy, quick to take body heat";
    if (NameHas(name, "Silver"))     return "and the silver is smooth and cool against the skin";
    if (NameHas(name, "Chromed") || NameHas(name, "Shiny") || NameHas(name, "Polished"))
                                     return "and the polished metal is smooth and cold";
    if (NameHas(name, "Scarf"))      return "and it is soft cloth, wide enough to spread its hold";

    // mundane materials
    if (NameHas(name, "Inflatable")) return "and it can be pumped tighter from outside";
    if (NameHas(name, "Rusty"))      return "and the iron is rough and rusted";
    if (NameHas(name, "Primitive"))  return "and it is rough-hewn and unyielding";
    if (NameHas(name, "Iron"))       return "and the iron is cold and unyielding";
    if (NameHas(name, "Steel"))      return "and the steel is cold and unyielding";
    if (NameHas(name, "Ebonite"))    return "and the rubber is smooth and airtight";
    if (NameHas(name, "Leather"))    return "and the leather is stiff against the skin";
    if (NameHas(name, "Tail"))       return "and a tail hangs from it, swaying with every step";
    return nullptr;
}

// Slow, non-postural discomforts that still belong on a clock. Deliberately few:
// each is a real, ordinary bodily consequence of wearing that thing for hours,
// and nothing here escalates to injury.
const char* SlowLine(const char* cls, float hours)
{
    if (!cls || !cls[0]) return nullptr;
    const bool longWorn = hours >= 3.0f;
    if (!longWorn) return nullptr;
    // ⛔ THE OLD Gag* ROW IS GONE, and it was wrong for half the family: "the jaw
    // aches and the mouth has gone dry" is true of a ring gag and FALSE of a ball
    // gag, which floods. Both now have real tiers (kStrainGagOpen /
    // kStrainGagFill) and never reach this function.
    // ⚠ GagTape DOES still reach here, and it is the one gag this row must serve:
    // tape seals the lips with the jaw CLOSED, so there is no held-open drying,
    // no masticatory load and nothing pooling behind an open mouth. What it
    // actually does over hours is adhesive and moisture at the skin.
    if (_stricmp(cls, "GagTape") == 0)
        return "the adhesive has warmed and set against the skin, and the lips underneath have stayed sealed and damp";
    // ⚠ ClassLine already says a corset keeps breathing shallow, so the old
    // wording here repeated it inside the same sentence. This adds the part
    // that only TIME produces.
    if (_stricmp(cls, "Corset") == 0)    return "the ribs have begun to ache with it";
    // ⚠ The Suit row that used to sit here is GONE. Suit gained its own thermal
    // tier on 2026-08-27 which starts at 1 game hour, and Report prefers a tier
    // line over this function, so the row became unreachable the moment the tier
    // landed. A dead row that still reads as live is how the last two regressions
    // in this file started.
    if (_stricmp(cls, "Blindfold") == 0 || _stricmp(cls, "Hood") == 0)
        return "hearing and touch have taken over the work of seeing";
    // ⚠ was "the padding has grown warm and damp" alone, which reads in only one
    // register. Skyrim gear like this sits on captives as often as on the
    // willing, so the line has to work for a prisoner three hours in as well as
    // for play. Adding the chafe makes it a plain physical report either way.
    if (_stricmp(cls, "Belt") == 0)      return "the padding has grown warm and damp, and the rim has begun to chafe where it sits";
    return nullptr;
}

// DEVICE NAMES ARE PUSHED IN TOO, and this was the worst bug of the set.
// `ForEachWorn` walks biped slots, which returns the RENDERED half of a DD pair
// - and a rendered half HAS NO NAME (verified: zadx_plugBlackSoulgemAn_
// scriptInstance has none; its Inventory twin is "Black Soulgem Anal Plug").
// So MaterialLine was handed the class token "PlugAnal" instead of a device
// name: the ENTIRE soulgem ladder never matched, and PlugStrengthOf returned 0
// so the magicka-reactive trigger could never fire on any DD device at all.
//
// This is the pair-splitting rule this project documented itself (report 23
// section 30) and then read the wrong half of. The name lives only on the
// inventory half and only zadlibs resolves it, so the quest script does the
// two-step on DDI_DeviceEquipped and hands the answer here, keyed by class.
// (g_mtx, THE ONE LOCK, is declared near the top of this file, above
// g_arousal - the first cross-thread map it guards. It moved on 2026-08-27
// when g_arousal and g_fx were found living OUTSIDE it while the comment here
// said they were covered.)

std::unordered_map<std::uint32_t, std::unordered_map<std::string, std::string>> g_names;

std::string NameForClass(std::uint32_t afid, const char* cls)
{
    if (!cls || !cls[0]) return {};
    // ⚠ Locked since 2026-08-26: this used to be read only from Report(), on the
    // Papyrus thread that also writes it. The TESEquipEvent sink now reads it
    // too, from a different thread, which makes it the same race the claim
    // ledger has.
    std::scoped_lock lk(g_mtx);
    auto a = g_names.find(afid);
    if (a == g_names.end()) return {};
    auto n = a->second.find(cls);
    return n == a->second.end() ? std::string{} : n->second;
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★ NAMES BY THE EXACT RENDERED FORMID (2026-09-13 - the review's D4, D9, D10).
//
// g_names above is keyed (actor, class). It was FILLED under DD's PROPERTY token (the Controller's
// DDI_DeviceEquipped edge) and READ with ClassOf's rank winner - two resolvers, and on the 541 of
// 1,221 rendered halves that carry more than one class keyword they can disagree (#1183 Evelynn's
// Collar: property Collar, ClassOf StraitJacket - nameless on both gates even after it was
// announced). It was also NEVER ERASED, so the name of an earlier same-class device leaked onto
// the next one fitted (a Scold's bridle, then a plain ball gag labelled "Scold's..." and refused
// by a helmet). The key registry left class tokens for the rendered FormID on 09-10 and that whole
// class of bug vanished there; names now follow. Three sources, in this order:
//   1. g_devNames[actor][renderedFid] - pushed by the Controller for EVERY worn device (the 30 s
//      key sweep and the equip edge, NoteDeviceRecord), erased with the device (NoteOff).
//   2. the PAIR TABLE - the INVENTORY half's own name, through VRTE_DDZaZ_Pairs.ini (generated
//      from the census; see BuildPairMap for why the script VM cannot answer this). This is the
//      EQUIP gate's answer: a device not yet worn is in no per-actor map, and the 09-03
//      NameForClass fix was therefore live on the removal gate only (D4).
//   3. g_names (the class push) as a fallback, then the record's own name (ZaZ / DoM singles).
// ═══════════════════════════════════════════════════════════════════════════
std::unordered_map<std::uint32_t, std::unordered_map<std::uint32_t, std::string>> g_devNames;

// rendered FormID -> inventory half. Record data, not state: read once from the shipped table on
// the first ask (the load order must be resolved, i.e. in-game), kept for the session.
std::unordered_map<std::uint32_t, RE::TESObjectARMO*> g_pairInv;
bool   g_pairDone    = false;
double g_pairTriedAt = -1.0e9;
double NowSeconds();                       // defined below (same unnamed namespace)

// ⛔ THE VM ROUTE IS DEAD (measured in VR 2026-09-13: "0 pairs (0 DD-scripted of 2098 slot-less
// armors)" six times). The ObjectReference script on a DD inventory half is a per-REFERENCE
// template; the base form is never bound, so FindBoundObject on it finds nothing. DD NG's own
// natives reach the property by re-parsing the plugins at load. So the table is that parse, done
// once offline: Data/SKSE/Plugins/VRTE_DDZaZ_Pairs.ini, generated by
// tools/_research/devviz/gen_pairs.py from the census (1,217 pairs), one line per pair:
//     rendLocal|rendPlugin|invLocal|invPlugin      (local FormIDs, hex)
// Both halves resolve through TESDataHandler::LookupForm, so the load order does not matter; a pair
// whose plugin is not loaded is skipped; a device not in the table is name-blind on the equip ask,
// exactly as before 1.2.4. PPB was also asked to pass the held half (build 5, BlockedByHeld) -
// that closes the gap for content the table does not know.
void BuildPairMap()
{
    auto* dh = RE::TESDataHandler::GetSingleton();
    if (!dh) return;
    FILE* f = nullptr;
    if (fopen_s(&f, "Data/SKSE/Plugins/VRTE_DDZaZ_Pairs.ini", "r") != 0 || !f) {
        logger::info("[PAIR] no VRTE_DDZaZ_Pairs.ini - the equip gate stays name-blind for DD devices");
        std::scoped_lock lk(g_mtx);
        g_pairDone = true;
        return;
    }
    std::unordered_map<std::uint32_t, RE::TESObjectARMO*> out;
    std::size_t lines = 0, unresolved = 0;
    char buf[512];
    while (std::fgets(buf, sizeof buf, f)) {
        if (!buf[0] || buf[0] == ';' || buf[0] == '\r' || buf[0] == '\n') continue;
        char rl[16] = {}, rp[200] = {}, il[16] = {}, ip[200] = {};
        if (std::sscanf(buf, "%15[0-9A-Fa-f]|%199[^|]|%15[0-9A-Fa-f]|%199[^\r\n]", rl, rp, il, ip) != 4)
            continue;
        ++lines;
        auto* rf  = dh->LookupForm(static_cast<std::uint32_t>(std::strtoul(rl, nullptr, 16)), rp);
        auto* inf = dh->LookupForm(static_cast<std::uint32_t>(std::strtoul(il, nullptr, 16)), ip);
        auto* ra  = rf  ? rf->As<RE::TESObjectARMO>()  : nullptr;
        auto* ia  = inf ? inf->As<RE::TESObjectARMO>() : nullptr;
        if (!ra || !ia) { ++unresolved; continue; }
        out.emplace(ra->GetFormID(), ia);
    }
    std::fclose(f);
    logger::info("[PAIR] rendered->inventory table: {} pairs resolved of {} lines ({} unresolved - a plugin not loaded)",
                 out.size(), lines, unresolved);
    std::scoped_lock lk(g_mtx);
    g_pairInv.swap(out);
    g_pairDone = true;
}

// The inventory half a rendered half belongs to, or nullptr (ZaZ / DoM singles, an unbound VM).
RE::TESObjectARMO* PairedInventory(RE::TESObjectARMO* rendered)
{
    if (!rendered) return nullptr;
    bool build = false;
    {
        std::scoped_lock lk(g_mtx);
        if (!g_pairDone && NowSeconds() - g_pairTriedAt > 60.0) { g_pairTriedAt = NowSeconds(); build = true; }
    }
    if (build) BuildPairMap();
    std::scoped_lock lk(g_mtx);
    auto it = g_pairInv.find(rendered->GetFormID());
    return it == g_pairInv.end() ? nullptr : it->second;
}

// THE name of a worn (or about-to-be-worn) device, every source in order. `cls` is the caller's
// already-resolved class, used only for the class-keyed fallback.
std::string NameForDevice(std::uint32_t afid, RE::TESObjectARMO* w, const char* cls)
{
    if (w) {
        {
            std::scoped_lock lk(g_mtx);
            auto a = g_devNames.find(afid);
            if (a != g_devNames.end()) {
                auto d = a->second.find(w->GetFormID());
                if (d != a->second.end() && !d->second.empty()) return d->second;
            }
        }
        if (auto* inv = PairedInventory(w)) {
            const char* n = inv->GetName();
            if (n && n[0]) return n;
        }
    }
    std::string nm = NameForClass(afid, cls);
    if (nm.empty() && w) { const char* n = w->GetName(); if (n && n[0]) nm = n; }
    return nm;
}

// ─────────────────────────────────────────────────────────────────────────────
// THE WEAR CLOCK — one timestamp per (actor, worn device), in game days.
// ─────────────────────────────────────────────────────────────────────────────
// `estimated` marks a device that was already on her when we first looked -
// its stamp is a FLOOR, not the truth, and the report says "at least" for it
// forever rather than for only the one render that discovered it.
// ⚠ `rope` IS PERSISTED, and it has to be. IsRopeDevice answers from the worn
// record's world model OR from the device NAME - and for a DD pair the rendered
// half has neither, so the name arrives from g_names, which is session-only.
// Without this bit, after any reload a rope armbinder stopped reading as rope and
// fell back to the arms tier: the SAME device at the SAME 12 game hours jumped
// from rope phase 3 to arms phase 5, a louder line for a device that had not
// changed. Elapsed hours are already persisted, so only the material was lost.
struct Wear { float when = 0.0f; bool estimated = false; bool rope = false; };
std::unordered_map<std::uint32_t, std::unordered_map<std::uint32_t, Wear>> g_on;

// Effect edges we have already reported, so a two-minute vibration is one event
// and not one per poll.
// `arousal` is the last value we saw, and `climaxes` is how many finishes this
// actor has had with devices on. Both are transient: a reload starts the count
// again, which is the right behaviour - the escalation is about one continuous
// session, not a lifetime tally.
struct EffectState {
    bool vibrating = false;
    bool shocked   = false;
    int  arousal   = -1;
    int  climaxes  = 0;
    double shockCd = 0.0;
    // ★ 1.3.4: the 100 lane fires ONCE each time her arousal reaches full, and re-arms only on a real
    // reading below 99 (a fresh value - the cached one is the same number again until it changes).
    // Replaces the 300 s cooldown the user never asked for ("5 min guard? Where? Why? I never ask that?").
    bool fullArmed = true;

    // ── SUPPRESSION, added 2026-08-26 after the conflict audit ──────────────
    // Every one of these exists because an action of OURS produces a state
    // change that our own observer would otherwise report a second time, as if
    // it had happened independently.
    // ⛔ `climaxBlindUntil` REMOVED 2026-08-27. It was written nowhere and read
    // nowhere - a leftover of the INFERRED climax detection, which was deleted
    // when climaxes moved onto DD's own DeviceActorOrgasmEx (see NoteClimax and
    // the note in ReEvaluate). It read as live suppression state that did not
    // exist, which is precisely the hazard this file keeps re-learning.
    bool   weStartedVib     = false; // the cast lane already narrated this one
    // ★ D12 (2026-09-13): the claim above EXPIRES. DD refuses a VibrateEffect for an actor in an
    // engine scene, without 3D, or already vibrating - and a flag left armed by a refused dispatch
    // swallowed the NEXT genuine start, whose stop then narrated alone.
    double weStartedVibAt   = 0.0;
    // ★ D12: the vibration now running is our own cast's. Its STOP edge is as redundant as its
    // start was - the cast line told the whole thing - and reporting it made a second persistent,
    // a second witness set and a 60 s aftermath that REPLACED the climax's longer one.
    bool   castVib          = false;
    // ★ D5: the start edge went unsaid (a scene, or she was unconscious), so the stop edge must
    // not narrate a vibration nobody heard begin.
    bool   vibStartMuted    = false;
    // ★ THE WAKE (2026-09-13): unconscious on the previous pass - the first pass with it clear asks
    // Devious Devices to re-apply the bound-arm animation set (see ReEvaluate).
    bool   wasKo            = false;
    double kneelCd          = 0.0;   // the tired kneel: one per NPC per kKneelCdS (1.2.8)
    // ★ 1.3.5 THE CLIMAX POSE OWNS HER UNTIL THE CLIP ENDS (the user, 2026-09-14: "as it's an animation,
    // we can better control the flow by tracking how long it last and stopping anything else to trigger
    // after the animation stop"). Set when a pose is dispatched, cleared by ClimaxPose's NotePoseEnd once
    // the clip has played out. While set, no lane of ours starts anything on her (cast, 100, shock, trip,
    // tired kneel). NOT a timer: its length is the clip's own. OnRevert clears it with the rest of g_fx.
    bool   posing           = false;
    // DD's orgasm SIGNAL (OrgasmSink) dispatched the pose ahead of the orgasm EVENT; NoteClimax, arriving
    // after it, credits the climax and must not dispatch a second pose.
    bool   posePending      = false;
    // ★ 1.3.9 ONE START, ONE STOP PER CHAIN (the user, 2026-09-15: "Only the first start, last stop"). While the
    // device keeps going off again at full arousal (1.3.8), a stop edge is HELD instead of told; the next start
    // then continues the chain silently. A held stop that no new start follows is told as the chain's last stop.
    bool   stopHeld         = false;
    double stopHeldAt       = 0.0;
};
std::unordered_map<std::uint32_t, EffectState> g_fx;

// 1.3.5: true while ClimaxPose is playing on her (see EffectState::posing). Takes g_mtx itself - never
// call it with the lock held (the 1.3.1 CTD was exactly that relock).
bool PoseBusy(std::uint32_t fid)
{
    std::scoped_lock lk(g_mtx);
    const auto it = g_fx.find(fid);
    return it != g_fx.end() && it->second.posing;
}

float SinceHours(std::uint32_t actorFid, std::uint32_t devFid, float nowDays)
{
    std::scoped_lock lk(g_mtx);
    auto a = g_on.find(actorFid);
    if (a == g_on.end()) return -1.0f;
    auto d = a->second.find(devFid);
    if (d == a->second.end()) return -1.0f;
    return (nowDays - d->second.when) * 24.0f;
}

// A device already on her when this feature arrived has no stamp. Stamping it
// "now" is a FLOOR, not the truth, and the report says "at least" for it rather
// than inventing a history it cannot know.
// `ropeNow` is what IsRopeDevice can see THIS session; `outRope` comes back as
// the stored answer. A stored true is never cleared by a session that cannot
// see the name any more - that is the whole point of persisting it - but a
// stored false is upgraded the moment the name does arrive.
void EnsureStamped(std::uint32_t actorFid, std::uint32_t devFid, float nowDays,
                   bool& backfilled, bool ropeNow, bool& outRope)
{
    std::scoped_lock lk(g_mtx);
    auto& m = g_on[actorFid];
    auto it = m.find(devFid);
    if (it == m.end()) {
        m[devFid] = Wear{ nowDays, true, ropeNow };
        backfilled = true;
        outRope    = ropeNow;
        return;
    }
    backfilled = it->second.estimated;
    if (ropeNow) it->second.rope = true;      // upgrade once we can see it
    outRope = it->second.rope;
}

std::string HoursText(float h, bool backfilled)
{
    char buf[64];
    const char* pre = backfilled ? "at least " : "";
    if (h < 1.0f) {
        const int m = (std::max)(1, static_cast<int>(h * 60.0f));
        std::snprintf(buf, sizeof buf, "%s%d minute%s", pre, m, m == 1 ? "" : "s");
    } else if (h < 48.0f) {
        const int n = static_cast<int>(h + 0.5f);
        std::snprintf(buf, sizeof buf, "%s%d hour%s", pre, n, n == 1 ? "" : "s");
    } else {
        const int d = static_cast<int>(h / 24.0f);
        std::snprintf(buf, sizeof buf, "%s%d day%s", pre, d, d == 1 ? "" : "s");
    }
    return buf;
}

// ═══════════════════════════════════════════════════════════════════════════
// EFFECT GROUPING (2026-08-28) - the user's token-economy rule, roadmap #5:
//   "a ceremonial collar, leg cuff and arms cuff end up with all the same scale
//    effect, so instead of having the same prompt about 'chaffing' show three
//    time, burning useless token and polluting the LLM awareness, all three
//    items will be enumerated together in the effect prompt to the LLM."
//
// ★ THE GROUP KEY IS THE SENTENCE ITSELF, not (tier, phase). Two devices whose
// effect sentences are byte-identical are in the same physical state by
// construction - the tables are the only producers - and keying on the text
// makes one elegant case fall out free: a true catsuit (class Suit, PAIN field)
// and a straitjacket-catsuit (class StraitJacket, DAMP field) share the suit
// tier's wording, so they group across the two axes with no special case.
//
// HOW IT EMITS: an effect sentence carried by exactly ONE surviving device is
// appended to that device's own bullet - byte-identical to the old output, so
// the common case (and every single-device visualiser example) is unchanged. A
// sentence shared by 2+ survivors is printed ONCE, after the list:
//     - The collar, the arm cuffs and the leg cuffs alike: the edges have begun
//       to chafe and there is a dull ache under them.
//
// ⚠ DECIDED FROM THE SURVIVORS, like the nerve footnote: a group line must not
// enumerate a device the cap truncated away.
struct Entry {
    std::string line;          // identity + clauses; NO effect sentence
    std::string pain;          // the strain-or-slow sentence, "" if none
    std::string damp;          // the damp-axis sentence, "" if none
    std::string noun;          // short handle for the group enumeration
    int         rank  = 0;     // higher sorts first when the list is capped
    // ⚠ Carried so the non-monotone footnote can be decided from the lines that
    // SURVIVE the cap. Deciding it from every worn device let the footnote
    // outlive the line it refers to - "the most serious state on this list"
    // printed under a list the phase-5 device had been truncated out of.
    bool        nerve = false; // this line is an arms/limb device at phase 5
};

// The short handle a grouped effect line calls a device by. Full names are too
// long to enumerate three of ("Gold Ceremonial Collar, Iron Padded Arm
// Cuffs..."), and the identity bullet directly above already gave the full
// name - the handle only has to POINT at it.
const char* NounOf(const char* cls, const char* label)
{
    // ⚠ the one label-dependent case: the tongue-grip scold's bridle sits in
    // the CLAMP tier beside nipple clamps, and calling it "the gag" in a line
    // about clamp ischemia would read as a different device.
    if (_strnicmp(cls, "Gag", 3) == 0 && ContainsNoCase(label, "Tongue"))
        return "the tongue clamp";
    struct Row { const char* c; const char* n; };
    static const Row rows[] = {
        { "Collar", "the collar" },
        { "ArmCuffs", "the arm cuffs" }, { "CuffsArms", "the arm cuffs" },
        { "CuffsFront", "the wrist cuffs" },
        { "LegCuffs", "the leg cuffs" }, { "CuffsLegs", "the leg cuffs" },
        { "AnkleShackles", "the ankle shackles" },
        { "Harness", "the harness" }, { "Corset", "the corset" },
        { "Belt", "the belt" }, { "Bra", "the bra" },
        { "Gloves", "the gloves" }, { "Boots", "the boots" },
        { "Suit", "the suit" }, { "StraitJacket", "the straitjacket" },
        { "Armbinder", "the armbinder" }, { "ArmbinderElbow", "the armbinder" },
        { "ElbowTie", "the elbow tie" }, { "Boxbinder", "the binder" },
        { "Yoke", "the yoke" }, { "YokeBB", "the yoke" }, { "YokeFront", "the yoke" },
        { "HeavyBondage", "the restraint" },
        { "PetSuit", "the pet suit" }, { "PonyGear", "the pony gear" },
        { "HobbleSkirt", "the skirt" }, { "HobbleSkirtRelaxed", "the skirt" },
        { "BondageMittens", "the mittens" },
        { "Blindfold", "the blindfold" }, { "Hood", "the hood" },
        { "PiercingsNipple", "the clamps" },
        { "PiercingsVaginal", "the piercing" },
        { "Butterfly", "the binder" },
    };
    for (auto& r : rows) if (_stricmp(cls, r.c) == 0) return r.n;
    if (_strnicmp(cls, "Gag", 3) == 0)  return "the gag";
    if (_strnicmp(cls, "Plug", 4) == 0) return "the plug";
    return "the gear";
}

} // namespace

// =============================================================================
//  THE MAGICKA-REACTIVE TRIGGER, AND THE DISCRETION POLICY
//
//  ⚠ WHY THIS HAS TO EXIST. Devious Devices ships the keyword
//  `zad_EffectVibrateOnSpellCast` and a function `SpellCastVibrate(akActor, ...)`
//  written generically to take any actor — but it has exactly ONE caller,
//  `zadPlayerScript.psc:164`, inside OnSpellCast on the PLAYER alias, hardcoded
//  to libs.PlayerRef. The device reacts only if the PLAYER wears it. On an NPC
//  the keyword is inert: the promise is in the data, the delivery was never
//  written. This supplies the missing half from outside, touching no DD file.
//
//  ⚠ AND DD'S NPC LOOP IS PLUG-ONLY. ZadNPCQuestScript runs its Vibration event
//  only `if WornHasKeyword(zad_DeviousPlug)`, so an NPC wearing nothing but
//  vibrating piercings never vibrates at all. Ours counts piercings too.
//
//  Nothing of DD is copied or modified. `zadLibs.VibrateEffect` is a public
//  Papyrus function and we CALL it, exactly as we already call LockDevice and
//  UnlockDeviceByKeyword. DD then performs its own sound, moan, expression,
//  animation, arousal and climax. We supply only the trigger and the policy.
// =============================================================================
namespace {

// ── the chance model (the user's numbers, 2026-08-26) ────────────────────────
//   "15% for a plug, 5% for piercing, additif, so all 4 of those soul
//    plug/piercing worn would be 40% chance ... whatever the strengh, it's not
//    more %, just stronger."
// Chance counts DEVICES; strength is a separate axis. A black soulgem plug is no
// likelier to fire than a common one — only stronger when it does.
constexpr int kChancePlug     = 15;
constexpr int kChancePiercing = 5;

// ⚠ ANTI-SPAM, deliberately blunt. The roll alone is not enough: a mage in a
// fight casts many times a minute, and at 40% that is a device firing every few
// seconds. A hard per-actor floor in REAL seconds bounds it however the dice
// fall. DD's own SpellCastVibrate carries a 60 s cooldown for the same reason.
constexpr double kCastCooldownS = 45.0;

// A shock is a sharper interruption than a vibration, so it is paced harder.
constexpr double kShockCooldownS = 90.0;

// ⛔ REMOVED 1.3.7 (2026-09-15): `kClimaxAV = "Variable03"` - unused here, and the premise under it was wrong.
// It said OAR "cannot read anything else we own"; OAR reads a FACTION rank (FactionRank condition), and a
// faction in DD SN Database.esp IS ours. Variable01-10 are vanilla actor values any mod or the game's own AI
// may write (SeverActions uses 07/08/10, SkyrimNet 09). The user: "make sure it's our own variable". The
// climax pose now marks her with DDSN_ClimaxPoseFaction (DD SN Database.esp 0x801) - see ClimaxClipMode.
// DD SN writes Variable03 nowhere any more (1.2.x-1.3.4 stamped it with the climax count; left untouched).

std::unordered_map<std::uint32_t, double> g_castCd;

int RollPercent()
{
    static std::mt19937 rng{ std::random_device{}() };
    return std::uniform_int_distribution<int>{ 1, 100 }(rng);
}

double NowSeconds()
{
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ── material → strength ──────────────────────────────────────────────────────
// 0 means "this material does not vibrate", which is the user's rule verbatim:
//   "i completely agree with the material ranking, no vibration for metal and stuff."
// Iron, steel, primitive, ebonite, leather, locking, inflatable and tail plugs
// all return 0 and can never fire. Only gem-bearing and crystal devices do.
//
// The value is the strength for a PLUG. A PIERCING of the same material sits one
// tier lower — the user again: "even black soul gem piercing would not vibrate
// as strong as a plug, being less invasive, but still have some effect."
int PlugStrengthOf(const char* name)
{
    if (!name || !name[0]) return 0;
    if (NameHas(name, "Black Soulgem")   || NameHas(name, "Black Soul Gem"))   return 5;
    if (NameHas(name, "Filled Soulgem")  || NameHas(name, "Filled Soul Gem"))  return 5;
    if (NameHas(name, "Grand Soulgem")   || NameHas(name, "Grand Soul Gem"))   return 4;
    // ★ ATRONACH (2026-08-28, the user on #1200-1203): "make them vibrate and do
    // arousal, those are made of Atronach crystal, let's call them really rare."
    // ⚠ DD gives these FOUR records no effect keyword at all - no vibration, no
    // shock, nothing - so this is OUR addition, not a transcription. That is
    // consistent with what this whole lane is: DD ships zad_EffectVibrateOnSpell
    // Cast and never delivers it to an NPC, and we supply the missing half.
    // Rated 5, level with a black soulgem, on the user's "really rare".
    if (NameHas(name, "Atronach"))                                            return 5;
    if (NameHas(name, "Welkynd"))                                             return 3;
    if (NameHas(name, "Greater Soulgem") || NameHas(name, "Greater Soul Gem")) return 3;
    if (NameHas(name, "Unknown Crystal"))                                     return 3;
    if (NameHas(name, "Chaos"))                                               return 3;
    if (NameHas(name, "Soulgem")         || NameHas(name, "Soul Gem"))         return 2;
    return 0;   // iron, steel, primitive, ebonite, leather, locking, tail, ...
}

struct CastScan {
    int chance  = 0;   // total percent
    int best    = 0;   // strongest single device, 1..5
    int count   = 0;   // how many qualified, for the narration
    // ★ 2026-09-06, the user's spec: arousal is PER DEVICE AND ACCUMULATES, by
    // SITE - nipple 1, anal 2, clitoris 3, vaginal 3. All four worn = +9; the
    // two piercings alone = +4.
    // ⛔ This is deliberately NOT `best`. Only the strongest device FIRES (the
    // 2026-08-26 ruling above, about noise and animation), but every worn
    // device still contributes to how aroused she gets - those are different
    // questions and conflating them is why a woman in four devices used to
    // gain exactly as much as one in a single plug.
    int arousal = 0;
};

// ★ ONLY THE STRONGEST FIRES. The user: "a NPC wearing 4 device could technicaly
// fire 4 event at once, we need to make sure only the strongest effect, for
// vibration, noise and animation, is fired. on the LLM side, we can add that all
// four have vibrated, so the roleplay fit."
// So: one VibrateEffect call at `best`, and `count` travels to the LLM separately.
CastScan ScanForCast(RE::Actor* a)
{
    CastScan r;
    if (!a) return r;
    std::vector<std::uint32_t> seen;
    ForEachWorn(a, [&](RE::TESObjectARMO* w) {
        if (!HasKw(w, "zad_Lockable") && !HasKw(w, "zad_DeviousPlug")) return true;
        const std::uint32_t fid = w->GetFormID();
        if (std::find(seen.begin(), seen.end(), fid) != seen.end()) return true;
        seen.push_back(fid);

        char cls[64];
        ClassOf(w, cls, sizeof cls);
        const bool isPlug     = (_strnicmp(cls, "Plug", 4) == 0);
        const bool isPiercing = (_strnicmp(cls, "Piercings", 9) == 0);
        if (!isPlug && !isPiercing) return true;


        std::string nm = NameForDevice(a->GetFormID(), w, cls);
        if (nm.empty()) nm = cls;
        // ⚠ A SHOCKING SOULGEM IS NOT EXCLUDED HERE, and an exclusion added
        // earlier today was REVERTED (user, 2026-08-27: "yes, i want shocking plug
        // to create arousal, it's kinda the goal").
        //
        // ★ Both halves are true at once and they are not in conflict: a Shocking
        // Soulgem plug BUILDS arousal exactly as any other soulgem does, and its
        // discharge is a separate lane with its own 90 s cooldown (ScanShock).
        // Excluding it removed the mechanism that makes the device work at all.
        // (Until 1.2.5 the Black Soulgem plugs were also discharged, on a DD
        // keyword DD itself never implemented - retired, see ScanShock.)
        int st = PlugStrengthOf(nm.c_str());
        if (st <= 0) return true;                        // inert material

        if (isPiercing) st = (std::max)(1, st - 1);      // less invasive, always lower
        r.chance += isPlug ? g_sc.pctCastPlug : g_sc.pctCastPiercing;
        r.best    = (std::max)(r.best, st);
        r.count  += 1;

        // ★ THE PER-SITE AROUSAL SUM (user, 2026-09-06). Keyed on the CLASS,
        // because the class is what names the site. `PlugAnal` and `PlugVaginal`
        // are distinct classes; a bare `Plug` carries no site at all, so it
        // takes the lower plug value rather than guessing the higher one.
        if      (_stricmp(cls, "PiercingsNipple")  == 0) r.arousal += g_sc.arNipple;
        else if (_stricmp(cls, "PiercingsVaginal") == 0) r.arousal += g_sc.arClitoris;
        else if (_stricmp(cls, "PlugAnal")         == 0) r.arousal += g_sc.arAnal;
        else if (_stricmp(cls, "PlugVaginal")      == 0) r.arousal += g_sc.arVaginal;
        else if (isPlug)                                 r.arousal += g_sc.arAnal;
        return true;
    });
    if (r.chance > 100) r.chance = 100;
    return r;
}

// ── location keywords ────────────────────────────────────────────────────────
// All 45 vanilla LocType* keywords live in Skyrim.esm, always mod index 0x00, so
// these FormIDs cannot shift with load order. Eight were re-verified against
// this load order before being written down.
//
// ⚠ NOT LookupByEditorID — SSE/VR only partially populate that map, and a
// nullptr there is a SILENT FEATURE SWITCH rather than an error: the classifier
// would answer "wilderness" forever with nothing in the log (§36 §4, again).
struct KwSet { RE::BGSKeyword* kw[16] = {}; std::size_t n = 0; };
KwSet g_home, g_guild, g_inn, g_town, g_dungeon;
RE::BGSKeyword* g_actorTypeNPC = nullptr;

RE::BGSKeyword* LocKw(std::uint32_t rawId)
{
    auto* dh = RE::TESDataHandler::GetSingleton();
    if (!dh) return nullptr;
    auto* f = dh->LookupForm(rawId, "Skyrim.esm");
    return f ? f->As<RE::BGSKeyword>() : nullptr;
}

void AddKw(KwSet& s, std::uint32_t raw)
{
    if (auto* k = LocKw(raw); k && s.n < 16) s.kw[s.n++] = k;
}

bool AnyKw(RE::BGSLocation* loc, const KwSet& s)
{
    // Walk the array directly. ⚠ Whether BGSLocation::HasKeyword recurses into
    // parentLoc is undocumented and could not be settled; this is correct under
    // either answer, because the CALLER does the walking.
    auto* kwf = loc ? loc->As<RE::BGSKeywordForm>() : nullptr;
    if (!kwf) return false;
    for (std::uint32_t i = 0; i < kwf->GetNumKeywords(); ++i) {
        auto k = kwf->GetKeywordAt(i);
        if (!k.has_value() || !k.value()) continue;
        for (std::size_t j = 0; j < s.n; ++j) if (s.kw[j] == k.value()) return true;
    }
    return false;
}

void ArmLocationKeywords()
{
    AddKw(g_home, 0x0FC1A3); AddKw(g_home, 0x01CB85); AddKw(g_home, 0x0504F9);
    AddKw(g_guild, 0x01CD5A);          // LocTypeGuild - verified live
    AddKw(g_inn,  0x01CB87);
    AddKw(g_town, 0x013168); AddKw(g_town, 0x013166);
    AddKw(g_town, 0x013167); AddKw(g_town, 0x039793);
    for (std::uint32_t id : { 0x0130DBu, 0x0130E2u, 0x0130DFu, 0x0130DEu, 0x0130E4u,
                              0x0130EBu, 0x0130ECu, 0x0130E3u, 0x0130E1u, 0x0130E0u,
                              0x0130E6u, 0x0130EDu, 0x0130EEu, 0x0130E5u, 0x0130EAu })
        AddKw(g_dungeon, id);
    g_actorTypeNPC = LocKw(0x013794);
    // A zero in this line is the tell that the lookups failed. Without it the
    // failure is invisible and every actor silently reads as "wilderness".
    logger::info("[WORN] location keywords armed: home={} guild={} inn={} town={} "
                 "dungeon={} npcKw={}",
                 g_home.n, g_guild.n, g_inn.n, g_town.n, g_dungeon.n,
                 g_actorTypeNPC != nullptr);
}

// ── the audience scan ────────────────────────────────────────────────────────
constexpr float kZGate  = 400.0f;     // two floors: a guard downstairs is not watching
constexpr float kRadius = 2048.0f;    // ~29 m
constexpr float kR2     = kRadius * kRadius;

} // namespace

Context Classify(RE::Actor* a)
{
    if (!a) return Context::Unknown;
    // ★ LEAF-FIRST, MOST-SPECIFIC-WINS. The specific bucket sits on the LEAF
    // location, the town on its PARENT, the hold on the grandparent — so a flat
    // test on the current location alone can say "inn" but never "which town",
    // and a test starting at the root calls a bedroom "a city".
    // Dungeon first so a bandit-held house reads hostile; town LAST because it
    // always lives further up the chain and would otherwise swallow every leaf.
    RE::BGSLocation* loc = a->GetCurrentLocation();
    for (int depth = 0; loc && depth < 16; loc = loc->parentLoc, ++depth) {
        if (AnyKw(loc, g_dungeon)) return Context::Dungeon;
        if (AnyKw(loc, g_inn))     return Context::Inn;
        if (AnyKw(loc, g_home))    return Context::PrivateHome;
        if (AnyKw(loc, g_guild))   return Context::Guild;
        if (AnyKw(loc, g_town))    return Context::Town;
    }
    // The chain ends at TamrielLocation, which carries no keywords at all, so
    // "matched nothing" is a clean wilderness signal rather than a failure.
    auto* cell = a->GetParentCell();
    if (cell && cell->IsInteriorCell()) return Context::Unknown;
    return Context::Wilderness;
}

const char* ContextWord(Context c)
{
    switch (c) {
    case Context::PrivateHome: return "somewhere private";
    case Context::Guild:       return "inside a guild hall";
    case Context::Inn:         return "in an inn";
    case Context::Town:        return "on a populated street";
    case Context::Dungeon:     return "in hostile ground";
    case Context::Wilderness:  return "out on the road";
    default:                   return "indoors";
    }
}

bool StrangerWatching(RE::Actor* a)
{
    if (!a) return false;
    auto* lists = RE::ProcessLists::GetSingleton();
    if (!lists) return false;
    auto* player = RE::PlayerCharacter::GetSingleton();
    const RE::NiPoint3 origin = a->GetPosition();

    // ⚠ High process is centred on the PLAYER, not on the subject. A follower
    // sent away can have people around HER who are only middle-high, so a
    // high-only scan would wrongly report "she is alone" — and for a privacy
    // gate a false "nobody is here" is the expensive error.
    const RE::BSTArray<RE::ActorHandle>* arrays[2] = {
        &lists->highActorHandles, &lists->middleHighActorHandles
    };

    // Raycasts are the only real cost, so they are budgeted and everything cheap
    // runs first: flag reads, then the Z gate, then squared distance.
    int losBudget = 8;

    for (const auto* arr : arrays) {
        for (const auto& handle : *arr) {
            auto       ptr = handle.get();    // keeps it alive; never one-line this
            RE::Actor* o   = ptr.get();
            if (!o || o == a || o == player)       continue;
            if (o->IsPlayerTeammate())             continue;   // free bit read
            if (o->IsCommandedActor())             continue;   // summons, thralls
            if (o->IsDead() || o->IsDisabled())    continue;
            if (o->IsGhost() || !o->IsAIEnabled()) continue;
            if (o->IsInBleedout())                 continue;
            if (IsMannequin(o))                    continue;   // never an audience

            RE::NiPoint3 w = o->GetPosition() - origin;
            if (std::fabs(w.z) > kZGate) continue;
            w.z = 0.0f;
            if (w.x * w.x + w.y * w.y > kR2) continue;

            if (g_actorTypeNPC && !o->HasKeyword(g_actorTypeNPC)) continue;  // not a chicken

            // Out of budget: assume seen. Fail toward discretion, never toward
            // performing in front of a crowd we did not finish counting.
            if (losBudget-- <= 0) return true;
            bool unused = false;
            if (o->HasLineOfSight(a, unused)) return true;
        }
    }
    return false;
}

namespace {

// ⚠ INTS ONLY across the VM boundary. Passing an object through
// DispatchStaticCall packs it as its most-derived attached script and the
// external type check refuses the upcast to Form — the trap that cost a session
// in report 23 §18. Papyrus resolves the FormID on the far side.
// ⛔ THE ARGUMENT COUNT MUST MATCH THE PAPYRUS SIGNATURE EXACTLY (1.3.2, 2026-09-14). Nothing
// checks it at build time: the VM refuses a mismatched call at RUN time with one Papyrus.0.log
// line ("Incorrect number of arguments passed to function ... Expected 2, got 3") and the function
// simply never runs. Every call here used to push three Ints, so ClimaxPose(Int, Int) and
// RefreshBoundPose(Int) had never once executed since 1.2.6. One overload per arity; pick the one
// that matches the .psc.
void DispatchPapyrus(const char* fn, std::int32_t a1, std::int32_t a2, std::int32_t a3)
{
    auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
    if (!vm) return;
    RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb;
    auto* args = RE::MakeFunctionArguments(std::move(a1), std::move(a2), std::move(a3));
    vm->DispatchStaticCall("VRTE_DDZaZ_Equip", fn, args, cb);
    delete args;
}

void DispatchPapyrus(const char* fn, std::int32_t a1, std::int32_t a2)
{
    auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
    if (!vm) return;
    RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb;
    auto* args = RE::MakeFunctionArguments(std::move(a1), std::move(a2));
    vm->DispatchStaticCall("VRTE_DDZaZ_Equip", fn, args, cb);
    delete args;
}

void DispatchPapyrus(const char* fn, std::int32_t a1)
{
    auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
    if (!vm) return;
    RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb;
    auto* args = RE::MakeFunctionArguments(std::move(a1));
    vm->DispatchStaticCall("VRTE_DDZaZ_Equip", fn, args, cb);
    delete args;
}

void SendCastEffect(RE::Actor* a, int strength, int count, const char* mode, Context ctx)
{
    auto* src = SKSE::GetModCallbackEventSource();
    if (!src) return;
    if (DeviceEquip::SceneOnNow()) {                       // D5: the scene owns the moment
        logger::info("[WORN] cast on 0x{:08X} - scene running, not narrated", a ? a->GetFormID() : 0u);
        return;
    }
    // "discreet" is the only mode a bystander could not notice — a stagger is
    // as visible as the full display, just differently shaped.
    const bool visible = (std::strcmp(mode, "discreet") != 0);
    char buf[128];
    // cast | mode | visible | strength | deviceCount | place
    std::snprintf(buf, sizeof buf, "cast|%s|%d|%d|%d|%s",
                  mode, visible ? 1 : 0, strength, count, ContextWord(ctx));
    SKSE::ModCallbackEvent ev{};
    ev.eventName = "VRTE_DDZaZ_DeviceEffect";
    ev.strArg    = buf;
    ev.numArg    = static_cast<float>(strength);
    ev.sender    = a;
    src->SendEvent(&ev);
}

class CastSink : public RE::BSTEventSink<RE::TESSpellCastEvent>
{
public:
    RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* e,
                                          RE::BSTEventSource<RE::TESSpellCastEvent>*) override
    {
        if (!e || !e->object) return RE::BSEventNotifyControl::kContinue;
        auto* a = e->object->As<RE::Actor>();
        if (!a || a->IsDead()) return RE::BSEventNotifyControl::kContinue;

        // The player already has DD's own implementation of exactly this. Doing
        // it again here would double every cast.
        if (a == RE::PlayerCharacter::GetSingleton())
            return RE::BSEventNotifyControl::kContinue;

        const std::uint32_t fid = a->GetFormID();
        {   // cheapest possible reject: not wearing anything we track
            std::scoped_lock lk(g_mtx);
            if (g_on.find(fid) == g_on.end()) return RE::BSEventNotifyControl::kContinue;
        }
        // 1.3.5: nothing starts on her while the climax clip plays - and no cooldown is spent on it.
        if (PoseBusy(fid)) return RE::BSEventNotifyControl::kContinue;

        const double now = NowSeconds();
        auto it = g_castCd.find(fid);
        if (it != g_castCd.end() && now < it->second) return RE::BSEventNotifyControl::kContinue;

        const CastScan sc = ScanForCast(a);
        if (sc.chance <= 0 || sc.best <= 0) return RE::BSEventNotifyControl::kContinue;
        if (RollPercent() > sc.chance)      return RE::BSEventNotifyControl::kContinue;

        g_castCd[fid] = now + g_sc.cdCast;

        // ── THE DISCRETION POLICY ────────────────────────────────────────────
        // Measured facts about DD's own idles that drive every line of this:
        //
        //  ⛔ DDZazHornyE (climax) puts the actor FLAT ON THE FLOOR, knees up and
        //     legs apart, with no transition in or out. It fails the user's bar
        //     outright and must never fire in front of anyone.
        //  ⛔ DDZazHornyD (edged) becomes a full ~15 s KNEEL on a belted actor.
        //  ⚠  StartThirdPersonAnimation SHEATHES her weapon and waits up to 3.5 s
        //     before any idle — mid-fight that disarms her.
        //  ⚠  VibrateEffect plays the idle INTERNALLY, so DD cannot be asked for
        //     "sound but no animation". The only way to withhold the body is not
        //     to call it — hence a quieter path of our own.
        //
        // So: full expression ONLY where nobody can see her and nothing is
        // trying to kill her. Anywhere else she keeps it to herself, which is
        // better roleplay than the alternative anyway.
        const Context ctx      = Classify(a);
        const bool    inCombat = a->IsInCombat();
        const bool    watched  = StrangerWatching(a);
        const bool    priv     = (ctx == Context::PrivateHome ||
                                  ctx == Context::Guild       ||
                                  ctx == Context::Wilderness  ||
                                  ctx == Context::Unknown);

        // ── CLIMAX IS NARROWER THAN "OPEN" ───────────────────────────────────
        // The user, 2026-08-26: "i want the kneel animation for climax, but not
        // in combat, only private place. guilds is fine."
        //
        // OPEN mode also covers the open ROAD, which is unobserved but is not a
        // private PLACE - and the climax idle is a ~15 s kneel that would look
        // absurd in the middle of a highway. So climax needs four walls:
        // a home, a guild hall, or an unnamed interior. On the road she still
        // gets the full vibration, just never the finish.
        const bool climaxOk = (ctx == Context::PrivateHome ||
                               ctx == Context::Guild       ||
                               ctx == Context::Unknown);

        // ── THREE MODES, not two (user, 2026-08-26) ──────────────────────────
        //   "3.5sec stunt in combat is good actualy, but not with those 'feet
        //    up on back' animation of course."
        // COMBAT gets its own lane: a vanilla STAGGER plus the moan. The
        // stagger is combat-native — it never sheathes her weapon, never plays
        // a DD idle, and ends in about a second — so the fight reads a stumble
        // rather than a performance. DD's machinery is not involved at all.
        // ★ AROUSAL FIRST, AND IN EVERY MODE (user, 2026-09-06: "apply to all
        // three modes"). Before this, arousal only moved in `open`, because it
        // was DD's VibrateEffect doing it as a side effect - so a device that
        // fired in combat or in public raised her arousal by exactly nothing.
        // The device did something to her body; where she happens to be
        // standing cannot decide whether it did.
        // ⚠ In `open` DD's own VibrateEffect ALSO raises arousal. That is
        // intended and was chosen deliberately: open is the full-expression
        // mode and should be the largest gain.
        if (sc.arousal > 0) {
            DispatchPapyrus("DoArousal", static_cast<std::int32_t>(fid),
                            sc.arousal, 0);
            logger::info("[WORN] CAST arousal +{} on 0x{:08X} ({} device(s))",
                         sc.arousal, fid, sc.count);
        }

        const char* mode;
        if (inCombat) {
            // ⛔ THE COMBAT STAGGER WAS REMOVED 2026-09-06 (user: "remove the
            // stagger in combat, it make no sense"). A device firing mid-fight
            // now does what it does in any other public place: she makes a
            // sound and keeps fighting. Staggering her handed the player a free
            // opening every time her own gear went off, which read as a
            // punishment mechanic nobody asked for.
            // `DoCombatStun` is left in the Papyrus for now; nothing calls it.
            mode = "combat";
            DispatchPapyrus("DoMoan", static_cast<std::int32_t>(fid), 0, 0);
        } else if (watched || !priv) {
            mode = "discreet";
            // Just the sound, from us. Gag-aware inside DD (Moan plays the
            // gagged clip instead when her mouth is stopped).
            DispatchPapyrus("DoMoan", static_cast<std::int32_t>(fid), 0, 0);
        } else {
            mode = "open";
            // duration 0 -> DD picks 5..20 s itself.
            // ⚠ teaseOnly = 1 ON PURPOSE: it makes DD EDGE her instead of
            // running ActorOrgasm, which is the only way the supine floor pose
            // (DDZazHornyE) can be reached from this trigger. The edge path
            // plays DDZazHornyD — or, on a belted actor, OAR's kneel variant —
            // which the user has accepted for private settings.
            // teaseOnly = 0 lets DD finish; = 1 edges her instead. DD still
            // requires arousal >= 99 and strength >= 3 for the finish to be
            // reachable at all, so this is a permission, not a guarantee.
            // ★ The third argument is the number this climax WOULD be, so the
            // Papyrus side can stamp the actor value BEFORE DD plays anything.
            // OAR reads that value to choose standing or kneeling, and OAR
            // reads it at play time - so stamping after the fact would always
            // be one climax late.
            int nextClimax = 0;
            {   // ⛔ g_fx is written from the VM thread too (NoteClimax); an
                // unguarded operator[] here can rehash under that thread's feet.
                std::scoped_lock lk(g_mtx);
                auto& st = g_fx[fid];
                nextClimax = st.climaxes + 1;
                // Claim the vibration edge this is about to cause, so the state
                // observer does not narrate the same thing a second time.
                // ★ D12: stamped, so a claim DD refused expires instead of swallowing
                // the next genuine start (ReEvaluate honours it for 30 s).
                st.weStartedVib   = true;
                st.weStartedVibAt = NowSeconds();
            }
            DispatchPapyrus("DoVibrate", static_cast<std::int32_t>(fid),
                            sc.best, climaxOk ? nextClimax : 0);
        }
        SendCastEffect(a, sc.best, sc.count, mode, ctx);

        logger::info("[WORN] CAST 0x{:08X} chance={}% best={} devices={} combat={} "
                     "watched={} ctx={} -> {}",
                     fid, sc.chance, sc.best, sc.count, inCombat, watched,
                     static_cast<int>(ctx), mode);
        if (std::strcmp(mode, "open") == 0)
            logger::info("[WORN]   climax {} for this fire",
                         climaxOk ? "PERMITTED (four walls)" : "withheld (not an interior)");
        return RE::BSEventNotifyControl::kContinue;
    }
};

CastSink g_castSink;

} // namespace

// ★ 1.3.3 - HOW A DEVICE CAN LEAVE HER IN A STRUGGLE (the user, 2026-09-14: "Say she unlocked it").
// Declared HERE, in the WornDevices namespace and outside the anonymous block below, because the
// struggle code lives inside that block while the key store (g_keys) is defined far below it in the
// named namespace - a declaration inside the anonymous block would name a second, undefined function
// (the LNK2019 trap in KNOWLEDGEBASE). Defined next to NoteDeviceKey.
enum class FreedHow : std::uint8_t {
    Forced   = 0,   // struggled, picked, cut - or we cannot prove a key route
    Unlocked = 1,   // DD wants a key and she held enough of them
    TakenOff = 2,   // DD wants no key for it (a keyless plug, a tail plug)
};
bool     HandsBlockedForKeys(RE::Actor* a);                                   // no lock (worn keywords only)
FreedHow EscapeRouteOf(RE::Actor* a, RE::TESObjectARMO* w, bool handsBlocked); // ⛔ takes g_mtx - never call locked

// ─────────────────────────────────────────────────────────────────────────────
namespace {


// ═════════════════════════════════════════════════════════════════════════════
// ★★ THE STRAIN AFTERMATH — what a device leaves behind when it comes off.
//
// The model built strain that ACCUMULATES while a device is worn and then said
// nothing at all when it was removed: nine hours in an armbinder ended and the
// shoulders were reported as fine. Two separate problems had to be solved.
//
// ⛔ 1. THE DATA WAS DESTROYED BEFORE IT COULD BE USED. NoteOff erased the wear
//    record, and it is called from the TESEquipEvent sink BEFORE the narration
//    branch below it. At the moment a removal could be reported, the duration
//    and the phase were already gone. NoteOff now RETURNS the record, from
//    inside the lock it already holds - no second lookup, no TOCTOU.
//
// ⛔ 2. THE MODEL HAD NO RECOVERY TERM. PhaseOf is a pure function of CURRENT
//    continuous wear time. Nothing decayed, and a device put straight back on
//    resumed from zero as though the body had reset with it.
//
// ★★ THE NON-MONOTONE PROBLEM, AND THE ANSWER. The wear curve FALLS at phase 5
// because phase 5 is nerve conduction block - the REPORTING channel fails while
// the harm is greatest. Removal takes the suppressor away. So:
//        wear      is monotone in REPORTABILITY
//        aftermath is monotone in HARM
// which is why phases 2->3->4 escalate in COMPLAINT and 4->5 escalates in
// DEFICIT. A limb that will not answer reads worse than one that aches, without
// shouting louder than it. Getting this backwards would have made the most
// damaged state the quietest one twice over.
//
// ★ NO PEAK-TRACKING STATE IS NEEDED. The phase at removal IS the peak reached:
// PhaseOf is monotone in hours, and NoteOn refuses to restart a clock that is
// already running. Storing a running maximum would have been redundant state
// that could drift out of step with the thing it mirrors.
//
// ⚠ WHAT IS ASSUMED, SAID PLAINLY. Only the phase-5 reperfusion window has a
// real anchor (a released ischemic block returns over minutes - the clamp
// tier's own documented event). The other durations are an argued SHAPE:
// ordered, roughly doubling, defensible - not measured. They live in
// VRTE_DDZaZ_Scales.ini [aftermath] so anyone can disagree with them without a
// rebuild, and the file says which one is anchored.
// ═════════════════════════════════════════════════════════════════════════════

// One residual per (actor, TIER). Per tier and not per device is the user's
// ruling, and it is also the honest shape: two cuffs coming off one after the
// other are one set of pressed lines on one body, not two independent recoveries.
struct Residual {
    int   phase   = 0;      // peak phase reached, 2..5
    float endDay  = 0.0f;   // GameDaysPassed when the residual is over
    float segADay = 0.0f;   // phase 5 only: when reperfusion gives way to deficit
    bool  clamp   = false;  // the clamp family: removal IS the event
    // ★ 2026-09-10: the after-state is a PROMPT BLOCK now, counted in prompts, not an event.
    int    left    = 3;     // prompts still to show: 3 = the full line, 2 = easing, 1 = fading
    double shownAt = 0.0;   // real seconds when the countdown last moved; 0 = not seen yet
    float  startDay = 0.0f; // 1.3.0: when it began - the state file grades by elapsed life, not prompts
};
// (actor -> tier -> residual)
std::unordered_map<std::uint32_t, std::unordered_map<int, Residual>> g_after;

// ★ THE DECAY / RE-EQUIP CREDIT (the user: "if someone get released, but put
// back in right after, there is still residual pain... the scale gradually
// deplete over a 1h game span"). On removal we remember how long the tier had
// been worn; a device of the SAME tier going back on inside the decay window
// resumes partway up its curve instead of from zero.
// ⚠ Two clocks, deliberately: this one winds the WEAR value down over ~1 game
// hour, while the aftermath NARRATION runs its own 1/2/4/6-hour course. They
// answer different questions - "how strained is the body now" and "what is it
// still reporting" - and collapsing them would make a long recovery also mean a
// long immunity to re-strain.
struct Credit { float hours = 0.0f; float offDay = 0.0f; };
std::unordered_map<std::uint32_t, std::unordered_map<int, Credit>> g_credit;

const char* AftermathLine(StrainTier t, int phase, bool segB)
{
    switch (t) {
    case kStrainArms:
        switch (phase) {
        case 2: return "the shoulders and neck are still aching where the position held them, and the ache lets go only slowly";
        case 3: return "the arms are still heavy and congested and the fingers are still tingling, and the shoulders ache through every movement now that movement is possible again";
        case 4: return "the shoulder joints are still grinding and sore, the arms are weak, and the reach has not come back through its full range";
        case 5: return segB
                    ? "the arms answer slowly and the grip keeps failing, and there are patches on the hands and forearms where nothing is felt at all"
                    : "sensation is flooding back into the arms as burning and pins, the hands are shaking with it, and nothing they close on can be held";
        }
        break;
    case kStrainYoke:   // ceiling 4 - never reaches a segment
        switch (phase) {
        case 2: return "the burn across the shoulders is still there now that the arms have come down, and it fades slowly";
        case 3: return "the shoulders and upper back are still burning and the arms are heavy and slow to lift";
        case 4: return "the shoulder joints ache deeply and the arms are still shaking, and they will not lift back to the height they were held at";
        }
        break;
    case kStrainMitt:
        switch (phase) {
        case 2: return "the fingers are still stiff and sore and are slow to open all the way";
        case 3: return "the hands throb and the knuckles are stiff where they were folded, and the fingers straighten only part way";
        case 4: return "every finger joint is sore and swollen, and the hands are clumsy on anything small";
        case 5: return segB
                    ? "the hands are weak and clumsy and there is still no feeling in the fingertips"
                    : "feeling is coming back into the hands as burning and pins, and the fingers will not close on anything with any force";
        }
        break;
    case kStrainLimb:
        switch (phase) {
        case 2: return "the legs are still aching where the weight was carried, and the ache eases only as they are used";
        case 3: return "the legs are still heavy and the feet are still tingling, and weight goes through them unevenly";
        case 4: return "the joints of the legs and feet are still sore and stiff, and the full range has not come back into them";
        case 5: return segB
                    ? "the legs are weak under load and the footing is unreliable, with patches on the feet where nothing is felt"
                    : "feeling is flooding back into the legs and feet as burning and pins, and they will not take weight steadily";
        }
        break;
    case kStrainRope:   // widest tier in the model: name only the rope and what is under it
        switch (phase) {
        case 2: return "the pattern of the turns is still pressed into the skin, and the muscle under it is slow to loosen";
        case 3: return "the muscle that was held is still set hard and lets go only a little at a time, and the marks of each wrap are still on the skin";
        case 4: return "the joints that were wrapped move stiffly and only part way, and movement is coming back slowly and awkwardly";
        }
        break;
    case kStrainStrap:  // ceiling 2 - exactly one line, and it claims nothing but a mark
        if (phase >= 2) return "the skin still carries a pressed line where the edges sat, and it is tender under it";
        break;
    case kStrainGagOpen:
        switch (phase) {
        case 2: return "the jaw is sore to close all the way, and the mouth is still dry";
        case 3: return "the jaw aches opening and closing, the mouth is still dry and tacky, and saliva is only beginning to come back";
        case 4: return "the jaw is slow and sore through its whole range and will not open wide, and the mouth and lips are still dry";
        }
        break;
    case kStrainGagFill:
        switch (phase) {
        case 2: return "the jaw is sore where it was held apart, and saliva can be swallowed again";
        case 3: return "the jaw aches through its whole range, and the chin and throat are still wet";
        case 4: return "the jaw is stiff and slow to close and sore to open, and the tongue and the roof of the mouth are still pressed flat where it sat";
        }
        break;
    case kStrainClamp:  // removal IS the event: reperfusion
        switch (phase) {
        case 3: return "the blood has come back into what was held, and it is throbbing hard where the grip was";
        case 4: return "the blood has come back into what was held all at once, and it is burning and beating hard where the grip was";
        case 5: return segB
                    ? "what was held is still hot and throbbing and is sore to touch"
                    : "sensation has returned to what was held all at once as a hard burning throb, and the flesh is hot and sore to any touch";
        }
        break;
    case kStrainSuit:   // ⚠ also reached by the 134 zad_DeviousSuit devices whose
        switch (phase) {  // class resolved elsewhere - so never name a garment shape
        case 3: return "the skin is wet everywhere it was sealed and is cooling fast now that the air reaches it";
        case 4: return "the skin is wet through where it was sealed, and is cooling as it dries";
        case 5: return "the skin is soft and pale where it was held wet, and still carries the mark of every seam";
        }
        break;
    default: break;
    }
    return nullptr;
}

// ★ THE OBSERVER LINE IS A DIFFERENT TABLE, NOT A TRUNCATION. Numbness, ache
// and dryness are interoceptive - nobody can see them. What a bystander can see
// is a limb being shaken out, a grip failing, a pressed line, wet skin. Where
// there is no outward sign this returns nullptr and the witness is told nothing,
// which is the honest answer.
const char* AftermathWitness(StrainTier t, int phase)
{
    switch (t) {
    case kStrainArms:
        if (phase == 4) return "the arms hang oddly and are being worked and shaken out";
        if (phase >= 5) return "the arms are shaking and the hands keep failing to close on what they reach for";
        break;
    case kStrainMitt:
        if (phase >= 4) return "the hands are being flexed and shaken out and keep fumbling what they reach for";
        break;
    case kStrainLimb:
        if (phase >= 4) return "the footing is unsteady and the weight keeps shifting from one leg to the other";
        break;
    case kStrainRope:
        if (phase >= 3) return "the pattern of the rope is still pressed across the skin";
        break;
    case kStrainStrap:
        if (phase >= 2) return "there is a pressed line on the skin where something was fastened";
        break;
    case kStrainSuit:
        if (phase >= 4) return "the skin is wet and marked where something was sealed against it";
        break;
    default: break;
    }
    return nullptr;
}


// ⛔ CALLED FROM THE UNCONDITIONAL CHOKEPOINT, AND THAT IS THE WHOLE POINT.
// EmitDeviceChange, thirty lines below the NoteOff call, is gated on
// `isPlug || !ours` - so a non-plug device pulled off BY HAND, which is the
// primary VR route, never reaches it. Hanging the aftermath off that payload
// would have meant an armbinder ripped off by the player leaves nothing behind:
// exactly the case the feature exists for. NoteOff has ONE call site and it runs
// for every removal on every route - hands, menu, container, key, script.
// ★ 1.2.8: RETURNS THE PHASE REACHED AT REMOVAL (0 = no strain tier / no wear) and hands back the
// tier, so the tired kneel (DrainPendingOff) reads the very same answer the after-state used - one
// tier question, never two that could disagree. The phase is returned even where no after-state
// line is written (a phase-1 device, a strap under its threshold).
int NoteAftermath(RE::Actor* a, RE::TESObjectARMO* w, const Wear& worn, StrainTier* outTier)
{
    if (outTier) *outTier = kStrainNone;
    if (!a || !w) return 0;
    if (!g_scLoaded) LoadScalesIni();

    const float now   = GameDaysNow();
    const float hours = (now - worn.when) * 24.0f;
    if (hours <= 0.0f) return 0;

    char cls[64] = {};
    ClassOf(w, cls, sizeof cls);
    std::string nm = NameForDevice(a->GetFormID(), w, cls);
    const char* label = nm.c_str();
    // ★ 2026-09-13 (#299, the user: "change it"): the same name-keyed reclass Report applies, so the
    // wear clock, the after-state and the re-equip credit follow the class the LLM is told.
    ReclassByName(cls, sizeof cls, label);

    // ⚠ the rope flag comes from the STORED record, not re-derived: IsRopeDevice
    // falls back to the NAME, and the name map is session-only, so a reload
    // between equip and removal would otherwise silently drop a rope device onto
    // the wrong curve. The co-save carries `rope` for exactly this reason.
    const StrainTier t = TierOfDevice(w, cls, worn.rope, IsClampDevice(cls, label),
                                     IsSoftFootwear(cls, label));
    if (t == kStrainNone) return 0;
    if (outTier) *outTier = t;

    // ★ PHASE AT REMOVAL *IS* PEAK PHASE REACHED - no peak-tracking state needed.
    // PhaseOf is monotone in hours and NoteOn refuses to restart a live clock,
    // so the value here cannot be lower than anything reported earlier.
    const int phase = PhaseOf(t, hours, CeilingFor(cls, t, label));
    if (phase < 2) return phase;                  // nothing accumulated

    // ★ THE CONSERVATIVE CUFF RULE, AS A THRESHOLD RATHER THAN A SILENCE.
    // The user: build it, "but only at long wear". strap trips phase 2 at 6 game
    // hours, which nearly every worn collar reaches - a pressed mark reported on
    // every NPC in a collar for an evening is precisely the spam this model is
    // warned about. A full day in shackles marks; an evening in a collar does
    // not. ⚠ 24 h is CHOSEN, not derived, which is why it is an INI key.
    if (t == kStrainStrap && hours < g_sc.afterStrapMin) return phase;

    const char* wearer = AftermathLine(t, phase, false);
    if (!wearer) return phase;

    float dur  = g_sc.afterMarks;                 // phase 2
    float segA = 0.0f;
    if      (phase == 3) dur = g_sc.afterAche;
    else if (phase == 4) dur = g_sc.afterDeep;
    else if (phase >= 5) { segA = g_sc.afterReperf; dur = segA + g_sc.afterDeficit; }

    {
        std::scoped_lock lk(g_mtx);
        Residual& r = g_after[a->GetFormID()][static_cast<int>(t)];
        // ⚠ KEEP THE WORSE OF AN OVERLAPPING PAIR. Two devices of one tier coming
        // off together are one recovery on one body, and the deeper of the two is
        // the state that body is actually in. Letting the second overwrite the
        // first would let taking off a bracelet cancel an armbinder's aftermath.
        if (phase >= r.phase) {
            r.phase   = phase;
            r.endDay  = now + dur / 24.0f;
            r.segADay = segA > 0.0f ? now + segA / 24.0f : 0.0f;
            r.clamp   = (t == kStrainClamp);
            r.left    = 3;                        // a fresh (or deeper) release starts over
            r.shownAt = 0.0;
            r.startDay = now;
        }
        // what this tier had accumulated, for a re-equip inside the decay window
        g_credit[a->GetFormID()][static_cast<int>(t)] = Credit{ hours, now };
    }

    // ★ NO EVENT (2026-09-10). The Database is awareness only: the removal itself is the AddOn's
    // to narrate, and what the device did to the body is read from here by the 0790 prompt
    // (AftermathState) and by an onlooker's 0950 block (AftermathVisible).
    logger::info("[WORN] after-state 0x{:08X} tier {} phase {} - a 3-prompt block, at most {:.1f} game h",
                 a->GetFormID(), static_cast<int>(t), phase, dur);
    return phase;
}

// ★ THE RE-EQUIP CREDIT (the user: "if someone get released, but put back in
// right after, there is still residual pain... the scale gradually deplete over
// a 1h game span"). The wear value winds down linearly over `afterDecay` game
// hours; a device of the SAME TIER going back on inside that window resumes
// partway up its curve instead of from zero.
//
// ⚠ PER TIER, NOT PER DEVICE - the user's ruling, and the honest shape: swapping
// one collar for another is the same strap on the same skin, and crediting per
// device would let a swap launder the strain away.
// ⚠ TWO CLOCKS, DELIBERATELY. This winds the WEAR value down over ~1 game hour
// while the aftermath NARRATION runs its own 1/2/4/6-hour course. They answer
// different questions - how strained the body is now, versus what it is still
// reporting - and collapsing them would make a long recovery also confer a long
// immunity to being strained again.
float WearCredit(RE::Actor* a, RE::TESObjectARMO* w)
{
    if (!a || !w) return 0.0f;
    if (!g_scLoaded) LoadScalesIni();
    if (g_sc.afterDecay <= 0.0f) return 0.0f;

    char cls[64] = {};
    ClassOf(w, cls, sizeof cls);
    std::string nm = NameForDevice(a->GetFormID(), w, cls);
    const char* label = nm.c_str();
    // ★ 2026-09-13 (#299, the user: "change it"): the same name-keyed reclass Report applies, so the
    // wear clock, the after-state and the re-equip credit follow the class the LLM is told.
    ReclassByName(cls, sizeof cls, label);

    const StrainTier t = TierOfDevice(w, cls, IsRopeDevice(w, label),
                                     IsClampDevice(cls, label),
                                     IsSoftFootwear(cls, label));
    if (t == kStrainNone) return 0.0f;

    const float now = GameDaysNow();
    std::scoped_lock lk(g_mtx);
    auto ac = g_credit.find(a->GetFormID());
    if (ac == g_credit.end()) return 0.0f;
    auto c = ac->second.find(static_cast<int>(t));
    if (c == ac->second.end()) return 0.0f;

    const float sinceH = (now - c->second.offDay) * 24.0f;
    if (sinceH < 0.0f || sinceH >= g_sc.afterDecay) {
        ac->second.erase(c);                      // fully recovered; forget it
        return 0.0f;
    }
    const float frac = 1.0f - (sinceH / g_sc.afterDecay);
    return c->second.hours * frac;
}

// ★ Returns false when the device was ALREADY on her (2026-09-13). Devious Devices re-asserts a
// worn device with a bare equip - no unequip first - on every fixup pass, and that is not a change:
// the sink told the LLM "is now wearing the Copper Wrist Cuffs Front" for cuffs already on.
bool NoteOn(std::uint32_t afid, std::uint32_t dfid, float creditHours)
{
    const float now = GameDaysNow();
    {
        std::scoped_lock lk(g_mtx);
        auto& m = g_on[afid];
        if (m.find(dfid) != m.end()) return false; // already ticking; do not restart
        // ★ BACKDATED BY THE CREDIT, so a device going straight back on
        // resumes partway up its curve instead of from zero. 0 for a first
        // equip, which is the ordinary case and behaves exactly as before.
        m[dfid] = Wear{ now - creditHours / 24.0f, false };
    }
    logger::info("[WORN] on  0x{:08X} <- device 0x{:08X} at day {:.4f}", afid, dfid, now);
    return true;
}

// ⛔ RETURNS THE RECORD, and that is the fix for the first half of the
// aftermath problem: this used to erase the wear entry and tell the caller
// nothing, so by the time a removal could be narrated the duration and the phase
// were already gone. Handing it back from INSIDE the lock it already holds means
// there is no second lookup and no window for another thread to erase it first -
// the TOCTOU shape this file's own comments warn about two functions below.
bool NoteOff(std::uint32_t afid, std::uint32_t dfid, Wear* out)
{
    bool had = false;
    {
        std::scoped_lock lk(g_mtx);
        auto it = g_on.find(afid);
        if (it != g_on.end()) {
            auto d = it->second.find(dfid);
            if (d != it->second.end()) {
                if (out) *out = d->second;
                it->second.erase(d);
                had = true;
            }
            if (it->second.empty()) g_on.erase(it);
        }
        // (2026-09-13, second pass: the exact-FormID name is NOT erased here - the same record is
        // the same device, and erasing it left every name blank for up to 30 s on each of DD's
        // off->on re-asserts. Stale CLASS names are dropped on the next equip - ForgetStaleClassName.)
    }
    if (had) logger::info("[WORN] off 0x{:08X} -> device 0x{:08X}", afid, dfid);
    // (the aftermath is raised by the CALLER, which still has the armor form)
    // ★★ 1.2.8 (the user, 2026-09-14: "removing a device should not reset arousal, it should leave it
    // at what it is"): her LAST device coming off no longer ERASES g_fx. The erase dated from the
    // inferred-climax era (an actor stripped at arousal 99 and re-equipped compared the old 99 and
    // reported a climax that never happened) - that inference was deleted 08-27; climaxes come from
    // DD's own event. What it still destroyed was hers to keep: the climax count (the kneel from the
    // 4th), the shock and 100-lane cooldowns, the last arousal reading. Only the EDGE flags - "a
    // vibration / shock is running now", "we started it" - clear, because a device that is gone
    // cannot still be running and a stale flag would report a stop edge later.
    {   // ⛔ One lock scope for the test AND the write: g_fx is shared with the VM thread (NoteClimax).
        std::scoped_lock lk(g_mtx);
        if (g_on.find(afid) == g_on.end()) {
            if (auto f = g_fx.find(afid); f != g_fx.end()) {
                auto& st = f->second;
                st.vibrating      = false;
                st.shocked        = false;
                st.weStartedVib   = false;
                st.weStartedVibAt = 0.0;
                st.castVib        = false;
                st.vibStartMuted  = false;
                // 1.3.10: and the 1.3.9 chain hold. Off tracking, ReEvaluate never runs for her, so a stop held
                // here would survive until her NEXT equip and be told then ("gone still again" beside an
                // unrelated device, possibly hours later). The device is gone; its stop is not news.
                st.stopHeld       = false;
                st.stopHeldAt     = 0.0;
            }
        }
    }
    return had;
}

// ★ D9 (2026-09-13, second pass): drop a STALE class-keyed name when a NEW device of that class
// goes on. g_names[actor][class] was written on every equip and erased only on load, so the name of
// a device she wore EARLIER became the label of the next same-class device fitted (a Scold's bridle,
// then a plain ball gag labelled "Scold's..." -> Mid face|head -> refused by a helmet). The first
// pass erased it on REMOVAL, which blanked the name for up to 30 s on every one of DD's off->on
// re-asserts. Now: on a genuinely NEW equip, a class entry is stale unless the incoming rendered
// record already has an exact-FormID name (i.e. it IS the device that was named). The DDI edge
// pushes the new device's name ~4 s later, under both keys.
void ForgetStaleClassName(RE::Actor* act, RE::TESObjectARMO* armo)
{
    if (!act || !armo) return;
    auto* kwf = armo->As<RE::BGSKeywordForm>();
    if (!kwf) return;
    const std::uint32_t afid = act->GetFormID();
    std::scoped_lock lk(g_mtx);
    if (auto d = g_devNames.find(afid); d != g_devNames.end() && d->second.count(armo->GetFormID()))
        return;                                              // known by record - the class name is its own
    auto a = g_names.find(afid);
    if (a == g_names.end()) return;
    for (std::uint32_t i = 0; i < kwf->GetNumKeywords(); ++i) {
        auto kw = kwf->GetKeywordAt(i);
        if (!kw.has_value() || !kw.value()) continue;
        const char* id = kw.value()->GetFormEditorID();
        if (!id || _strnicmp(id, "zad_Devious", 11) != 0 || !id[11]) continue;
        if (_stricmp(id + 11, "Device") == 0) continue;
        if (a->second.erase(id + 11))
            logger::info("[WORN] name 0x{:08X} {} was an earlier device's - forgotten for the new one", afid, id + 11);
    }
}

// ★ D5 (2026-09-13): the scene owns the moment for EFFECT lines too. A vibration, shock, trip,
// cast or climax on a scene actor used to narrate straight into the scene (D1 was the climax
// instance). The effects themselves still run - the user left that as it is - only the line is
// held. The scene edges live in DeviceEquip, with the plug narration they were moved for.
bool SceneMutes(RE::Actor* a, const char* what)
{
    if (!DeviceEquip::SceneOnNow()) return false;
    logger::info("[WORN] {} on 0x{:08X} - scene running, not narrated", what, a ? a->GetFormID() : 0u);
    return true;
}

// Emit one effect edge. VRTE decides tier and audience; we only report that it
// happened and whether a bystander could tell (the ownership rule, report 25 §1).
void SendEffect(RE::Actor* a, const char* what, const char* phase, bool visible)
{
    auto* src = SKSE::GetModCallbackEventSource();
    if (!src || !a) return;
    if (SceneMutes(a, what)) return;
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s|%s|%d", what, phase, visible ? 1 : 0);
    SKSE::ModCallbackEvent ev{};
    ev.eventName = "VRTE_DDZaZ_DeviceEffect";
    ev.strArg    = buf;
    ev.numArg    = 0.0f;
    ev.sender    = a;
    src->SendEvent(&ev);
    logger::info("[WORN] fx 0x{:08X} {}", a->GetFormID(), buf);
}

// One shock edge with its count and its sites, for BOTH lanes. ★ D14 (2026-09-13): the observer
// edge used to send the bare 3-field form, which the Controller defaulted to "one plug" - so a
// piercing-only jolt DD itself fired was told as a knockdown with the 120 s "put on the floor"
// aftermath. Now it scans the worn set exactly as the arousal lane does.
void SendShock(RE::Actor* a, int count, int sites)
{
    auto* src = SKSE::GetModCallbackEventSource();
    if (!src || !a) return;
    if (SceneMutes(a, "shock")) return;
    if (count < 1) count = 1;
    char buf[128];
    std::snprintf(buf, sizeof buf, "shock|start|1|%d|%d|%s", count, sites, ContextWord(Classify(a)));
    SKSE::ModCallbackEvent ev{};
    ev.eventName = "VRTE_DDZaZ_DeviceEffect";
    ev.strArg    = buf;
    ev.numArg    = static_cast<float>(count);
    ev.sender    = a;
    src->SendEvent(&ev);
    logger::info("[WORN] fx 0x{:08X} {}", a->GetFormID(), buf);
}

// Re-evaluate the two runtime states DD leaves observable on an actor and emit
// only the EDGES.
//
// ⚠ WHY WE WATCH STATE AND NOT DD'S EVENTS. DD broadcasts "DeviousEvent<Name>"
// for everything it fires — but only from ProcessOneEvent, which is the PLAYER
// loop. The NPC loop calls Eval() directly and sends nothing at all, so for an
// NPC there is no event in existence to subscribe to (Report 26 §1.2). What DD
// does leave behind is state: zadVibratorFaction membership while a vibration
// runs, and a shock magic effect while a jolt lands. Those we can see.
void ReEvaluate(RE::Actor* a)
{
    if (!a) return;
    // ⚠ The cast lane excluded the player and this one did not, so DD's own
    // player-side shock events and ours could both fire on her. And nothing
    // checked liveness at all: a tracked NPC killed at high arousal could take
    // a shock, and a narration about it, as a corpse - death itself applies and
    // removes magic effects, which is what wakes this sink.
    if (a == RE::PlayerCharacter::GetSingleton()) return;
    if (a->IsDead() || a->IsDisabled() || a->IsChild()) return;
    const std::uint32_t fid = a->GetFormID();
    auto* fac = VibratorFaction();

    // ⚠ hoisted above the shock edge (2026-08-30): the edge now has to know
    // whether the arousal lane's cooldown has expired. See below.
    const double now = NowSeconds();
    // ★ D3 (the user, 2026-09-13: "yes"): NOTHING is done to an unconscious actor. VRTouchEvents'
    // KO holds her paralysed at low health with HealRate 0 for game-hours; four shocking devices
    // at 2.5 % each killed her inside it, the bleedout played on a ragdoll, and the line said she
    // "got back up". State is still TRACKED below (so no edge is reported late when she wakes),
    // but no line goes out and the arousal lane does not fire.
    const bool ko  = a->IsUnconscious();
    const bool vib = fac && a->IsInFaction(fac);
    // 02C0C0 zad_effElectroShock - the ShockEffect spell DD RemoteCasts for a real jolt (zadLibs
    // :2516, zadEventPeriodicShocker :26).
    // ⛔ 050C85 zad_effShocker is NOT a jolt (found in VR 2026-09-13): it is the MARKER effect of
    // the plug's own enchantment ("Shock on full arousal, shock after vibrations", e.g.
    // zadx_EnchPlugBlackSoulgems) and sits on the wearer for as long as any shock-capable plug is
    // worn - so every equip of one tripped a "shock edge" 17 ms after the plug went in, and when
    // the arousal lane was in cooldown that phantom jolt was NARRATED.
    const bool shk = HasEffect(a, 0x02C0C0);

    // ⚠ The operator[] (an INSERT) is under the lock; the reference is then
    // used unlocked, and that is reasoned, not sloppy: unordered_map element
    // addresses are STABLE across rehash, and the only erasers of g_fx run on
    // this same main thread (NoteOff via the equip sink; OnRevert at a load
    // boundary, when the sinks are quiet) - so the reference cannot dangle.
    // What the lock prevents is this insert rehashing while the VM thread
    // (NoteClimax) walks a bucket. ⚠ STALE since 1.3.3 (review of 2026-09-15): this runs in an SKSE task on a
    // BSJobs worker, not the main thread, and NoteClimax / OnDDOrgasmSignal / NotePoseEnd write more than
    // `climaxes` - so the pose flag is read through PoseBusy, under the lock. Field-level interleaving with NoteClimax is
    // benign - it touches only `climaxes`, which this function does not.
    EffectState* stp = nullptr;
    {
        std::scoped_lock lk(g_mtx);
        stp = &g_fx[fid];
    }
    auto& st = *stp;
    // ★ THE WAKE (found in VR 2026-09-13): a knocked-out NPC came round with her hands FREE while
    // the Copper Wrist Cuffs Front were still locked on. VRTouchEvents' KO ragdolls her and the
    // get-up resets the animation graph; Devious Devices re-evaluates its bound-arm animation sets
    // only on its own equip events. First pass with the KO cleared: ask DD to re-evaluate.
    if (st.wasKo && !ko) {
        logger::info("[WORN] 0x{:08X} woke up - asking Devious Devices to re-apply the bound-arm pose", fid);
        DispatchPapyrus("RefreshBoundPose", static_cast<std::int32_t>(fid));   // ⛔ one Int - the .psc takes one
    }
    st.wasKo = ko;
    if (vib != st.vibrating) {
        st.vibrating = vib;
        if (vib) {
            // ⚠ If the CAST lane started this vibration it has already narrated it,
            // in more detail than this line can. Reporting the edge as well turned
            // one happening into two events on the wearer and up to eight on
            // bystanders. Consume the flag and stay quiet exactly once.
            // ★ D12: the claim is honoured for 30 s only - DD refuses a dispatch for
            // an actor in an engine scene / without 3D / already vibrating, and a
            // stale claim used to swallow the NEXT genuine start.
            const bool ours = st.weStartedVib && (now - st.weStartedVibAt) < 30.0;
            st.weStartedVib = false;
            st.castVib      = ours;
            if (ours) {
                if (st.stopHeld) {   // 1.3.9: a different happening - the held chain's stop is told first
                    st.stopHeld = false;
                    SendEffect(a, "vibration", "stop", true);
                }
                logger::info("[WORN] vibration on 0x{:08X} started by our own cast - "
                             "edge not reported twice", fid);
            } else if (ko) {
                st.vibStartMuted = true;
                logger::info("[WORN] vibration start on 0x{:08X} - unconscious, not narrated", fid);
            } else if (SceneMutes(a, "vibration start")) {
                st.vibStartMuted = true;
            } else if (st.stopHeld) {
                // 1.3.9: the device went off again straight after the last one ran out - one chain, one start line.
                st.stopHeld      = false;
                st.vibStartMuted = false;
                logger::info("[WORN] vibration on 0x{:08X} going again - same chain, its start is not told twice", fid);
            } else {
                st.vibStartMuted = false;
                // A running device is audible at conversational range and she moves
                // with it, so a bystander can tell. Whether one is NEAR is VRTE's call.
                SendEffect(a, "vibration", "start", true);
            }
        } else {
            // ★ D12: a vibration OUR cast started was told in full by the cast line;
            // its stop edge made a second persistent, a second witness set and a 60 s
            // aftermath that REPLACED the climax's. As quiet as its start.
            if (st.castVib) {
                st.castVib = false;
                logger::info("[WORN] vibration on 0x{:08X} stopped - our own cast, edge not reported", fid);
            } else if (st.vibStartMuted) {
                st.vibStartMuted = false;
                logger::info("[WORN] vibration stop on 0x{:08X} - its start went unsaid, so does this", fid);
            } else if (ko) {
                logger::info("[WORN] vibration stop on 0x{:08X} - unconscious, not narrated", fid);
            } else if (st.arousal >= 99 && !PoseBusy(fid)) {
                // 1.3.9: still at full arousal and no climax pose on her - the 1.3.8 lane may start it again on
                // this very pass. Held; told below if no new start follows.
                st.stopHeld   = true;
                st.stopHeldAt = now;
                logger::info("[WORN] vibration stop on 0x{:08X} held - at full arousal the device may go off again", fid);
            } else {
                SendEffect(a, "vibration", "stop", true);
            }
        }
    }
    // 1.3.9: a held stop that no new start followed is the chain's last stop - told now. The wait spans two of
    // the 3 s polls plus DD's own start-up (3.1 s from "ran out" to the next start, VR log 2026-09-15); a start
    // seen on the same pass is handled above first, so this never tells a stop the chain continued past.
    if (st.stopHeld && !st.vibrating && (now - st.stopHeldAt) > 8.0) {
        st.stopHeld = false;
        if (ko)
            logger::info("[WORN] held vibration stop on 0x{:08X} dropped - unconscious", fid);
        else
            SendEffect(a, "vibration", "stop", true);
    }
    // ⛔ THE OBSERVER EDGE AND THE AROUSAL LANE CAN FIRE IN ONE PASS (found by
    // the 2026-08-30 audit). Below, the arousal lane may decide to shock her and
    // PRE-SETS st.shocked so this edge does not report the jolt twice - but on a
    // pass where DD's OWN NPC shock path has already applied the effect, `shk`
    // is ALREADY true here, this branch reports it, and the arousal lane then
    // fires a second real ShockActor with its own damage. One jolt became two
    // narrations, up to eight witness lines, two shocks and up to 10% max health.
    //
    // ★ The cooldown is the honest arbiter and it already exists: if the arousal
    // lane is within its 90 s window it will not fire, so reporting the edge is
    // right; if it is ABOUT to fire, it owns the narration. Test that here.
    const bool arousalLaneMayFire = (now >= st.shockCd);
    // ★ A DD-caused edge the arousal lane MAY claim this pass. If the lane then does NOT fire (the
    // roll fails, arousal is under the bar, no device counts) the edge is sent after it - it used
    // to be logged "deferred" and then never told at all (found in VR 2026-09-13).
    bool ddEdge = false;
    if (shk != st.shocked) {
        st.shocked = shk;
        if (shk && ko) {
            logger::info("[WORN] shock edge on 0x{:08X} - unconscious, not narrated", fid);
        } else if (shk && !arousalLaneMayFire) {
            // ★ D14: DD's own jolt, told with ITS count and sites, not "one plug".
            const ShockScan os = ScanShock(a);
            SendShock(a, os.count, os.sites);
        } else if (shk) {
            ddEdge = true;
            logger::info("[WORN] shock edge on 0x{:08X} - the arousal lane gets first claim this pass", fid);
        }
    }
    // ★ D3: no shock, no arousal climb, on an unconscious actor.
    if (ko) return;

    // ── AROUSAL-DRIVEN: the shock, and the climax observation ───────────────
    // ⚠ WHY THIS LIVES ON AN EVENT SINK AND NOT A TIMER. The AddOn ships no ESP,
    // so there is no script instance anywhere in the mod and therefore no
    // OnUpdate. TESActiveEffectApplyRemoveEvent fires whenever anything changes
    // on a tracked actor - including everything DD itself does to her - so it
    // is a natural heartbeat for exactly the actors we care about, and silent
    // for everyone else.
    const bool laneFired = [&]() -> bool {
    const int ar = ArousalOf(a);
    if (ar < 0) return false;
    st.arousal = ar;
    if (ar < 99) st.fullArmed = true;   // 1.3.4: below full again - the 100 lane may fire the next time she reaches it

    // THE INFERRED CLIMAX IS GONE. It watched for arousal falling from near
    // maximum, which was wrong three ways: our own ShockActor call produces
    // exactly that signature (so every shock also reported a phantom climax);
    // a SexLab or OStim orgasm produces it too (so any sex scene was narrated
    // as "the devices brought her off"); and it could not run at all, because
    // ArousalOf was reading a dead faction. Climaxes now arrive on DD's own
    // DeviceActorOrgasmEx, which carries the actor as a Form - see NoteClimax.

    // ★ THE SHOCK.
    //
    // ⚠ DD's own PeriodicShocker is PLAYER-ONLY, so for the PERIODIC kind this lane is the only
    // discharge an NPC gets; the cooldown is what keeps it to one jolt.
    // ⛔ CORRECTED 1.3.3 (review of 2026-09-14): this comment used to say no DD script reads
    // zad_EffectShockOnFullArousal. FALSE - DD 5.2's zadEventVibrate.Execute (not overridden by DD NG)
    // shocks at arousal >= 99 and sets exposure to 1, and NG's zadNPCQuestScript runs that event for
    // the NPCs DD has SLOTTED (up to 15, every 1.5 game hours). The on-full kind came back in 1.2.9
    // (ScanShock.onFull below). ⚠ A DD-slotted NPC can therefore be shocked by DD AND by this lane;
    // the two are not coordinated (DD's own drops her arousal, which usually closes our >= 99 test).
    const ShockScan sh = ScanShock(a);
    if (sh.count == 0) return false;
    if (now < st.shockCd) return false;
    if (PoseBusy(fid)) return false;   // 1.3.5: not while the climax clip plays (no cooldown spent); 1.3.6: read under the lock
    // ⚠ Chance is now PER SET, not per device, and the per-device rate is low:
    // 6% x count, so one device is 6% and a full set of four is 24% - down from
    // the flat 25% it used to roll at any count. The roll is per EVENT and
    // active-effect events arrive in bursts, so the 90 s cooldown is still what
    // does the real pacing.
    const bool periodic = (sh.kind >= 2 && ar >= 60 &&
                           RollPercent() <= g_sc.pctShockPerDevice * sh.count);
    // ★ 1.2.9: a shock-on-full-arousal plug discharges AT full arousal, no roll - that is what the
    // device is ("treat it as a shock plug"). Every shock device she wears goes with it (the user's
    // 08-26 rule: all fire together, the count is the intensity).
    const bool onFull = (sh.onFull > 0 && ar >= 99);
    if (!periodic && !onFull) return false;

    st.shockCd = now + g_sc.cdShock;
    // ⚠ PRE-SET so the observer does not report this a second time when the
    // magic effect actually lands a frame later. Without this one line a single
    // jolt emitted TWO shock events - and a third, a phantom climax, because
    // ShockActor also slams arousal down to 10-20.
    st.shocked = true;

    // count | hasPlug -> Papyrus scales the damage and picks the reaction.
    DispatchPapyrus("DoShock", static_cast<std::int32_t>(a->GetFormID()),
                    sh.count, sh.sites);

    // The payload carries the count and where it lands, so the narration can
    // say how many sites went at once rather than inventing one.
    SendShock(a, sh.count, sh.sites);
    logger::info("[WORN] SHOCK on 0x{:08X} kind={} devices={} sites={} arousal={}{}",
                 a->GetFormID(), sh.kind, sh.count, sh.sites, ar,
                 onFull ? " - the shock plug discharged at full arousal" : "");
    return true;
    }();
    // The DD-caused edge the lane was offered first and declined: hers to tell after all.
    if (ddEdge && !laneFired) {
        const ShockScan os = ScanShock(a);
        SendShock(a, os.count, os.sites);
    }

    // ★★ THE 100 LANE (the user, 2026-09-13): "for plug, it's 100, where the vibration goes off and
    // the NPC just holds it in place and shakes, then on the 4th one it's on her knee from now on."
    // A vibrating device at full arousal goes off BY ITSELF. Devious Devices' own NPC loop would do
    // this once per 1.5 game hours, and only for the NPCs it monitors - never, in practice. Same
    // discretion as the cast lane: four walls = the finish is permitted, anywhere else DD edges her;
    // never in combat. The vibration start edge narrates itself (no cast line here), the climax
    // narrates through NoteClimax and ClimaxPose sets arousal back to 50.
    // ★ 1.3.4 ONCE PER CLIMB, NO TIMER (the user, 2026-09-14: "5 min guard? Where? Why? I never ask that?
    // Why are you implementing feature i didn't ask for?"). The 1.2.6 build added a 300 s cooldown here
    // on its own, to stop a PUBLIC edge (DD's EdgeActor leaves arousal untouched) re-firing on every 3 s
    // pass. That is now fullArmed: the lane fires once when she reaches full and re-arms only when a real
    // reading drops below 99 - in public she is teased once and stays wanting until her arousal falls
    // and climbs back; in private she climaxes, relief sets 50, and she can climb again straight away.
    // ★ 1.2.9: never on the pass the shock lane fired - a shock plug's discharge at 100 is the shock,
    // not a vibration finish (and ShockActor drops her arousal to 10-20 straight after).
    if (!laneFired && st.arousal >= 99 && !st.vibrating && st.fullArmed && !PoseBusy(fid) && !a->IsInCombat()) {
        const CastScan sc = ScanForCast(a);
        if (sc.best > 0 && sc.count > 0) {
            const Context ctx      = Classify(a);
            const bool    climaxOk = (ctx == Context::PrivateHome || ctx == Context::Guild ||
                                      ctx == Context::Unknown);
            st.fullArmed = false;
            const int nextClimax = st.climaxes + 1;
            DispatchPapyrus("DoVibrate", static_cast<std::int32_t>(fid), sc.best,
                            climaxOk ? nextClimax : 0);
            logger::info("[WORN] FULL AROUSAL on 0x{:08X} - {} vibrating device(s), best={} -> vibration, {}",
                         fid, sc.count, sc.best,
                         climaxOk ? "finish permitted (four walls)" : "edge only (not an interior)");
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// THE BLIND MODULE (2026-08-29) - the user's design, both halves.
//
//   "can we make someone blind attack ANYONE... And NO RANGE WEAPON, only
//    melee" / "can we do no range spell? Only self targeted?" / "attach DD's
//    fall animation to the blindfold... 5% chance... if the NPC is walking,
//    25% if they are running, and 50% if an elevation change is detected."
//
// ⛔ THE BASELINE THIS FILLS: nobody delivers blindness to an NPC anywhere -
// DD's blindfold effect is player-only in every branch, ZaZ's says "(only
// works on player)" in its own comment, and the engine Blindness AV is used
// by zero of 2,092 mods. FIFTH instance of declared-but-undelivered.
//
// WHAT RUNS HERE (main thread, from PollStates' 3 s task):
//   1. MELEE ONLY - a blind actor's bow/crossbow and any equipped spell that
//      is not SELF-delivery are unequipped, via Papyrus (DoBlindStrip). The
//      AI may re-equip; the next tick strips again - self-healing both ways,
//      and it stops on its own the moment the blindfold comes off.
//   2. THE TRIP - a blind body moving over ground it cannot see. Rolled per
//      tick while moving: walking 5% / running 25% / an elevation change 50%.
//      Movement is measured from POSITION DELTA, not the actorState flags -
//      the flags are animation-driven and unreliable for NPCs, the position
//      is ground truth. Delivery is DD's OWN zadLibs.Trip() (public, generic,
//      VR-aware, OAR-replaced, gag-aware moan) - its only shipped caller is
//      player-only, so this is the missing NPC half, not a duplicate.
//      ⚠ NOT a real ragdoll on purpose: PushActorAway physics on a driven
//      actor risks the HMD/PLANCK interactions; the animation cannot.
//
// ⚠ SCOPE LIMIT, honest: this tick walks TrackedActors (g_on), whose gate is
// zad_Lockable || zad_DeviousPlug - so a ZaZ-only blindfold does not reach it
// until the ZaZ incorporation widens the tracker. DD blindfolds all qualify.
// ⚠ The mis-target half (attack anyone) is designed, NOT built - report 29
// §0.6g; it needs its own careful pass over crime/essential rules.
// ═══════════════════════════════════════════════════════════════════════════
struct BlindState {
    RE::NiPoint3 last{};
    bool         hasLast     = false;
    double       tripCdUntil = 0.0;
    double       misCdUntil  = 0.0;    // the mis-target roll's pacing
    int          rolls       = 0;      // 1.3.8: moving ticks rolled (receipt every 20th)
    int          trips       = 0;
};
std::unordered_map<std::uint32_t, BlindState> g_blindTick;   // main thread only

void BlindTick(RE::Actor* a)
{
    if (!a) return;
    const std::uint32_t fid = a->GetFormID();
    if (fid == 0x14) return;                       // the player has DD's own systems
    // ⛔ LIVENESS FIRST (2026-08-30, audit finding). Neither half had a guard:
    // the strip half ran on corpses and unloaded actors, and the trip half
    // could emit its narration for an actor whose 3D is gone - DoTrip then
    // refuses to animate (it tests Is3DLoaded), so the LLM was told she fell
    // over while nothing happened. One test, before either half.
    if (a->IsDead() || a->IsDisabled() || !a->Is3DLoaded()) {
        g_blindTick.erase(fid);
        return;
    }
    if (!DeviceEquip::IsRestrained(a, 1)) {        // cat 1 = blind
        if (g_blindTick.erase(fid))
            logger::info("[WORN] BLIND watch off 0x{:08X} - she can see again", fid);
        return;
    }
    // ★ 1.3.8 A RECEIPT WHEN THE WATCH OPENS (the user, 2026-09-15: "blinded NPC don't trip"): the first VR
    // session of 1.3.7 had no line at all between "blindfold on" and a trip, so nothing could say whether
    // the gate had opened. Now it says so once, and every 20 moving ticks it reports the rolls.
    const bool fresh = g_blindTick.find(fid) == g_blindTick.end();
    auto& bs = g_blindTick[fid];
    if (fresh)
        logger::info("[WORN] BLIND watch on 0x{:08X} - trip rolls while she moves: walk {}% / run {}% / stairs {}% per 3 s",
                     fid, g_sc.pctTripWalk, g_sc.pctTripRun, g_sc.pctTripStairs);

    // ── 1. melee only: strip the bow and every aimed spell ──────────────────
    for (const bool left : { true, false }) {
        auto* obj = a->GetEquippedObject(left);
        if (!obj) continue;
        if (auto* weap = obj->As<RE::TESObjectWEAP>()) {
            // ⛔ IsStaff ADDED 2026-09-06. The test was bow-or-crossbow only, and
            // a STAFF is neither - so a blindfolded NPC went on firing a staff of
            // firebolt across a room while her hand spells were being stripped.
            // The hand-spell branch below was doing its job; the weapon branch
            // had a hole exactly the shape of the thing it was meant to stop.
            if (weap->IsBow() || weap->IsCrossbow() || weap->IsStaff()) {
                DispatchPapyrus("DoBlindStrip", static_cast<std::int32_t>(fid),
                                static_cast<std::int32_t>(weap->GetFormID()), -1);
                logger::info("[WORN] BLIND 0x{:08X}: ranged weapon stripped ({})",
                             fid, weap->IsStaff() ? "staff" : "bow/crossbow");
            }
        } else if (auto* sp = obj->As<RE::SpellItem>()) {
            if (sp->GetDelivery() != RE::MagicSystem::Delivery::kSelf) {
                DispatchPapyrus("DoBlindStrip", static_cast<std::int32_t>(fid),
                                static_cast<std::int32_t>(sp->GetFormID()),
                                left ? 0 : 1);
                logger::info("[WORN] BLIND 0x{:08X}: aimed spell stripped ({})",
                             fid, left ? "L" : "R");
            }
        }
    }

    // ── 1b. THE MIS-TARGET (2026-08-30, the user: "build it"). "attack ANYONE
    // if they are blind. Cause they can't see" - mis-identification, not rage.
    // ⚠ ONLY while ALREADY in combat: a blind NPC cannot find a target and
    // never initiates; the confusion begins when a fight exists and a body
    // comes within reach. 25% per 3 s tick, 12 s cooldown after a switch, and
    // the nearest LIVING adult within ~8.5 m is the new target - whoever that
    // is, allies included; that is the ruling. The chaos that follows (crime,
    // guards) was stated and accepted when this was designed (report 30 §0.6g).
    if (a->IsInCombat()) {
        const double nowM = NowSeconds();
        if (nowM >= bs.misCdUntil && RollPercent() <= g_sc.pctMisTarget) {
            RE::Actor* best = nullptr;
            float bestD2 = 600.0f * 600.0f;
            if (auto* lists = RE::ProcessLists::GetSingleton()) {
                const RE::NiPoint3 origin = a->GetPosition();
                for (const auto& h : lists->highActorHandles) {
                    auto ptr = h.get();
                    RE::Actor* o = ptr.get();
                    if (!o || o == a || o->IsDead() || o->IsDisabled() ||
                        o->IsChild() || !o->Is3DLoaded() || IsMannequin(o))
                        continue;
                    const RE::NiPoint3 d = o->GetPosition() - origin;
                    const float d2 = d.x * d.x + d.y * d.y + d.z * d.z;
                    if (d2 < bestD2) { bestD2 = d2; best = o; }
                }
            }
            if (best) {
                auto cur = a->GetActorRuntimeData().currentCombatTarget.get();
                if (cur.get() != best) {
                    bs.misCdUntil = nowM + g_sc.cdMisTarget;
                    DispatchPapyrus("DoBlindMisTarget",
                                    static_cast<std::int32_t>(fid),
                                    static_cast<std::int32_t>(best->GetFormID()), 0);
                    logger::info("[WORN] BLIND MIS-TARGET 0x{:08X} -> 0x{:08X} "
                                 "(nearest body, {:.0f}u)",
                                 fid, best->GetFormID(), std::sqrt(bestD2));
                }
            }
        }
    }

    // ── 2. the trip roll ────────────────────────────────────────────────────
    const RE::NiPoint3 p = a->GetPosition();
    if (!bs.hasLast) { bs.last = p; bs.hasLast = true; return; }
    const float dx = p.x - bs.last.x, dy = p.y - bs.last.y, dz = p.z - bs.last.z;
    bs.last = p;
    if (a->IsDead() || a->IsInKillMove() || a->AsActorState()->IsSwimming() ||
        a->IsOnMount())
        return;
    if (a->AsActorState()->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal)
        return;

    const float dist2 = dx * dx + dy * dy;
    // A cell change or teleport reads as a huge jump - update and stand down.
    if (dist2 > 4000.0f * 4000.0f) return;
    // ★ 1.3.8 "MOVING" IS THE GAME'S OWN FLAG, NOT A DISTANCE (the user, 2026-09-15: "blinded NPC don't trip,
    // so that's not good. They are supposed to get a chance of it"). This used to require 150 units of travel
    // per 3 s tick before it rolled at all. A follower shuffling round a room never covers that, and an NPC in
    // leg irons never can - Carmella's Copper Ankle Cuffs slow her ("no speed is possible"), so a blindfold
    // worn with ankle cuffs could not trip by construction. The actor's movement state says whether she is
    // walking or running at whatever speed her restraints leave her; the position only tells stairs from flat.
    const auto* as = a->AsActorState();
    if (!as || !a->IsMoving()) return;
    const bool climbed = std::fabs(dz) > 40.0f;    // stairs or a ledge this tick
    const bool running = as->IsRunning() || as->IsSprinting();
    const int  chance  = climbed ? g_sc.pctTripStairs
                             : (running ? g_sc.pctTripRun : g_sc.pctTripWalk);

    const double now = NowSeconds();
    if (now < bs.tripCdUntil) return;
    if (PoseBusy(fid)) return;                            // 1.3.5: not during the climax clip
    if (++bs.rolls % 20 == 0)
        logger::info("[WORN] BLIND 0x{:08X}: {} moving ticks rolled so far, {} trip(s)", fid, bs.rolls, bs.trips);
    if (RollPercent() > chance) return;
    ++bs.trips;
    bs.tripCdUntil = now + g_sc.cdTrip;                   // the fall + getting up

    DispatchPapyrus("DoTrip", static_cast<std::int32_t>(fid), 0, 0);
    SendEffect(a, "trip", climbed ? "stairs" : (running ? "running" : "walking"),
               true);
    logger::info("[WORN] BLIND TRIP 0x{:08X} cause={} rolled under {}%",
                 fid, climbed ? "elevation" : (running ? "running" : "walking"),
                 chance);
}

// ═══════════════════════════════════════════════════════════════════════════
// THE GAG MODULE (2026-09-06) - the user: "Do gag=nocasting, cause that make
// sense, like for blindfold and long range distance weapon."
//
// The parallel is exact. A blindfold takes away the thing sight is FOR, so the
// blind module strips what needs aiming. A gag takes away the thing speech is
// for, so this strips what needs speaking: every equipped spell, at any
// delivery. She keeps her sword.
//
// ⛔ WHY THIS IS ENFORCED AND THE REST OF THE GAG IS NOT. Being unable to TALK
// is a roleplay instruction the LLM honours (it can decide she mumbles, writes,
// gestures). Being unable to CAST is a rule the engine has to hold, because
// nothing about an LLM's dialogue choice stops an AI package from throwing a
// firebolt. Same split as the blind module, and the same reason.
//
// ⚠ STAVES ARE DELIBERATELY LEFT ALONE. A staff channels through the staff and
// needs no words - it is the canonical way a silenced caster keeps casting, and
// removing it would be enforcing something a gag does not physically do. (The
// BLIND module does strip staves, because that is about aiming, not speech.)
// Say the word if you want gags to take staves too.
//
// ⚠ NOT NEW BEHAVIOUR FOR ZaZ's `zbfEffectNoMagic` GEAR: ZaZ already unequips
// spells for those items through its own ApplySilenceEffect, and we already
// describe it. This generalises the rule to EVERY gag, DD ones included, which
// previously blocked no casting at all.
//
// Self-healing exactly like the blind strip: the AI may re-equip, the next
// 3 s tick strips again, and it stops the moment the gag comes off.
// ═══════════════════════════════════════════════════════════════════════════
void GagTick(RE::Actor* a)
{
    if (!a) return;
    const std::uint32_t fid = a->GetFormID();
    if (fid == 0x14) return;                       // the player has DD's own systems
    if (a->IsDead() || a->IsDisabled() || !a->Is3DLoaded()) return;
    if (!DeviceEquip::IsRestrained(a, 0)) return;  // cat 0 = gag

    for (const bool left : { true, false }) {
        auto* obj = a->GetEquippedObject(left);
        if (!obj) continue;
        auto* sp = obj->As<RE::SpellItem>();
        if (!sp) continue;                         // a weapon is not our business
        DispatchPapyrus("DoBlindStrip", static_cast<std::int32_t>(fid),
                        static_cast<std::int32_t>(sp->GetFormID()),
                        left ? 0 : 1);
        logger::info("[WORN] GAG 0x{:08X}: spell stripped ({}) - no words, no spell",
                     fid, left ? "L" : "R");
    }
}

// ── the two engine sinks ─────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════
// DEVICE STATE CHANGES -> SkyrimNet.  The equip sink below is the SINGLE
// emitter for "a device went on / came off", whatever caused it.
//
// ★ WHY THE ENGINE SINK AND NOT DD'S OWN EVENTS (user, 2026-08-26: "if the
// equipment/removal of DD gears is done thru a menu, we need to send an event
// to SkyrimNet too").  DD does raise DDI_DeviceEquipped / DDI_DeviceRemoved,
// and they carry the INVENTORY half so they name the device properly - but
// they are Papyrus, and DD's own source doubts they fire for NPCs at all:
// zadEquipScript.psc:415 carries the standing note
//     ; OnEquipped(npc) ; Not sure why this isn't being called for npc's.
// TESEquipEvent has no such doubt.  It is the engine's own truth, it fires for
// every actor on every runtime, and it cannot be bypassed by a script that
// takes an unusual route - menu, container, key unlock, console, a third-party
// mod, or our own hands.  One sink covers every path there is.
//
// ⚠ AND IT MUST SEE ONLY THE WORN HALF.  A DD device is a PAIR and both halves
// can carry the plug keywords, so an unfiltered sink fires twice for one
// equip.  The rendered half is the one with biped slots; the inventory half
// has none.  That single test is what makes this one-event-per-device.

// The claim ledger. Our own hand gestures already narrate through their own
// events (DeviceEquipped / UndressEnd), so when a gesture causes the equip the
// sink must stay quiet or one physical act narrates twice.
//
// ⚠ KEYED ON THE ACTOR, NOT ON THE DEVICE - and that is forced, not chosen.
// The removal side knows the rendered FormID, but the EQUIP side never does:
// DoEquip is handed the INVENTORY half and calls EquipItemEx on it, and it is
// DD's own OnEquipped chain that then equips the rendered half. A device-keyed
// claim could therefore never match an equip.
//
// ⛔ IT IS LOCKED, AND THAT IS NOT OPTIONAL. ClaimGesture runs on the RENDER
// thread (DoEquip and RipPiece are reached from OnFrame, installed as
// AddPostVrikPostHiggsCallback), while GestureClaimLive runs inside the
// TESEquipEvent sink on the main or VM thread. Both MUTATE this map - the
// reader erases expired entries - and the claim is written milliseconds before
// the equip it claims, so the race window is the NORMAL path, not an edge case.
// An unguarded insert that rehashes while the other thread walks a bucket is a
// hard crash with nothing in any log pointing here. g_mtx already exists in this
// file for exactly this class of state.
//
// ⚠ THE WINDOW IS SHORT ON PURPOSE, and an earlier 8 s value was wrong for a
// documented reason worth preserving: it was justified by a comment claiming
// DeviceEquip.cpp's measured note "records DD's chain OUTRUNNING a 1.2 s
// verify". The note says the OPPOSITE - "its length is not ours to predict...
// our check at +1.2s still did not see the rendered half worn" - i.e. DD is
// SLOWER, and unbounded. No wall-clock number can be correct against an
// unbounded chain, so the question becomes which way to fail:
//   too long  -> a slow-but-successful equip is suppressed here AND refused by
//                PendingVerify, so NOTHING narrates it. Silent, undiagnosable.
//   too short -> the gesture narrates twice. Redundant, and visible in the log.
// Double narration is the safe direction, so this matches PendingVerify's own
// ~2.5 s budget rather than trying to outlast Papyrus.
std::unordered_map<std::uint32_t, double> g_gestureClaim;
constexpr double kClaimWindowS = 3.0;

bool GestureClaimLive(std::uint32_t actorFid)
{
    std::scoped_lock lk(g_mtx);
    auto it = g_gestureClaim.find(actorFid);
    if (it == g_gestureClaim.end()) return false;
    if (NowSeconds() < it->second) return true;
    g_gestureClaim.erase(it);          // expired: tidy up on the way past
    return false;
}

// Does PPB see this plug AS a plug? PPB's FALLBACK class rule (DeviceGesture.cpp
// DdDescribe): the FIRST keyword containing "Devious" whose suffix is not "Device" names
// the class. VRTE skips a plug only when that class starts "Plug" - so when this answers
// false, VRTE narrates the hand gesture as ordinary gear.
// ⚠ Keyword storage order, not our ClassRank: two resolvers that can disagree on a
// multi-class record is exactly the case this has to follow PPB on, not us.
// ⚠ Not an exact mirror (D17, 2026-09-13): PPB's wire class is DD's zad_DeviousDevice
// PROPERTY first (TickVerify) and DdDescribe only when that is unset. The two agree on
// 1,221 of 1,221 catalogued records today, which is why the keyword walk is enough here.
bool PpbSeesPlug(RE::TESObjectARMO* w)
{
    auto* kwf = w ? w->As<RE::BGSKeywordForm>() : nullptr;
    if (!kwf) return false;
    for (std::uint32_t i = 0; i < kwf->GetNumKeywords(); ++i) {
        auto kw = kwf->GetKeywordAt(i);
        if (!kw.has_value() || !kw.value()) continue;
        const char* id = kw.value()->GetFormEditorID();
        const char* p  = id ? std::strstr(id, "Devious") : nullptr;
        if (!p) continue;
        const char* k = p + 7;
        if (!k[0] || _stricmp(k, "Device") == 0) continue;
        return _strnicmp(k, "Plug", 4) == 0;     // first real class decides
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// THE MENU CLAIM - the flatrim player's hands (2026-08-27).
//
// ★ THE USER'S CALL: "for NPC to NPC, SkyrimNet could be the trigger as action
// can be chosen by the LLM, but we will do the menu one first."
//
// A player equipping a device on an NPC through the trade/container menu is
// the flatrim equivalent of the VR hand gesture - the same person doing the
// same act through a different input. And the engine says exactly WHO that
// menu is open on: ContainerMenu::GetTargetRefHandle() is the follower-trade /
// container target. When a device change lands on the same actor the menu is
// (or just was) open on, the player is the agent - OBSERVED, not guessed.
//
// ⚠ TWO TESTS, because DD's equip chain is Papyrus and unbounded (the measured
// note elsewhere in this file: a check at +1.2 s still did not see the
// rendered half worn - and the rendered half is the one this sink fires on):
//   1. The menu OPEN RIGHT NOW on this actor. The common case; zero state.
//   2. A WINDOW after it closes, because the player can click-equip and close
//      the menu before DD's chain equips the rendered half. 10 s: generous for
//      Papyrus lag under load, short enough that a coincidental script equip
//      landing inside it is unlikely.
// Fail direction: too SHORT degrades to the agentless wording (honest, just
// thinner); too LONG misattributes an act to the player - the exact error the
// `ours` flag exists to prevent. Hence a bounded window, not a toggle.
//
// ⚠ ATTRIBUTION ONLY, NEVER SUPPRESSION. The gesture claim keeps the sink
// QUIET (the gesture's own event narrates instead); the menu claim only
// decorates the sink's own event with "the player did this". Folding the two
// together would silence every menu equip - exactly backwards.
std::unordered_map<std::uint32_t, double> g_menuClaim;
std::uint32_t g_menuOpenOn = 0;        // actor the ContainerMenu is open on now
constexpr double kMenuClaimS = 10.0;

// The actor the container menu is targeting, or 0 for a chest / the player /
// no menu. ⚠ The PLAYER is excluded on purpose: their own inventory work is
// not an act on an NPC, and the equip sink already skips 0x14 anyway.
std::uint32_t ContainerMenuTarget()
{
    const auto handle = RE::ContainerMenu::GetTargetRefHandle();
    RE::NiPointer<RE::TESObjectREFR> ref;
    if (!RE::TESObjectREFR::LookupByHandle(handle, ref) || !ref) return 0;
    auto* act = ref->As<RE::Actor>();
    if (!act || act->GetFormID() == 0x14) return 0;
    return act->GetFormID();
}

bool MenuClaimLive(std::uint32_t actorFid)
{
    // The live menu first: no state, no window, cannot go stale.
    if (auto* ui = RE::UI::GetSingleton();
        ui && ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME) &&
        ContainerMenuTarget() == actorFid)
        return true;

    std::scoped_lock lk(g_mtx);
    auto it = g_menuClaim.find(actorFid);
    if (it == g_menuClaim.end()) return false;
    if (NowSeconds() < it->second) return true;
    g_menuClaim.erase(it);             // expired: tidy up on the way past
    return false;
}

// The UI edges, for the after-close window. ⚠ The target is captured at OPEN -
// it is set before the open event dispatches - because at close the menu is
// already tearing down and the static handle may have been cleared.
class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
public:
    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* e,
                                          RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
    {
        if (!e || !e->menuName.c_str() ||
            _stricmp(e->menuName.c_str(), RE::ContainerMenu::MENU_NAME.data()) != 0)
            return RE::BSEventNotifyControl::kContinue;
        if (e->opening) {
            const std::uint32_t fid = ContainerMenuTarget();
            std::scoped_lock lk(g_mtx);
            g_menuOpenOn = fid;
        } else {
            std::scoped_lock lk(g_mtx);
            if (g_menuOpenOn) {
                g_menuClaim[g_menuOpenOn] = NowSeconds() + g_sc.cdMenuClaim;
                logger::info("[WORN] container menu on 0x{:08X} closed - "
                             "player-agent window open {}s", g_menuOpenOn, g_sc.cdMenuClaim);
                g_menuOpenOn = 0;
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};
MenuSink g_menuSink;

// Which orifice(s) a plug occupies, as a 1=vaginal / 2=anal bitmask, or 0 for
// "not a plug at all" / "cannot tell".
//
// ⛔ IT MUST FIRST ESTABLISH THAT THE DEVICE IS A PLUG. Chastity belts and some
// harnesses carry a plug SUB-keyword because they hold a plug as part of the
// assembly; without the class gate below, a belt going on was promoted to the
// INTERRUPT tier and narrated as a plug being pushed into a body.
//
// ⚠ ORDER MATTERS, and the first version had it backwards. The bare
// zad_DeviousPlug keyword used to short-circuit to "both orifices", which made
// the biped-slot test below UNREACHABLE for exactly the devices that needed it -
// so a single anal plug carrying only the generic keyword was narrated as
// filling both. The site-specific keywords are checked first, then the SLOTS
// (the user's own rule for this AddOn: "look at which slot they equip on instead
// of just their name"), and the bare keyword now yields 0 rather than a guess.
//
// ★ 0 IS AN HONEST ANSWER. Guessing "both" fabricates a physical fact in the one
// payload field the narration cannot soften; declining lets VRTE say "into her
// body" instead of naming two orifices it does not know are filled.
int PlugSitesOf(RE::TESObjectARMO* w)
{
    if (!w) return 0;

    // Is it a plug at all? ClassOf yields DD's own class suffix; the prefix test
    // is the same one the cast scanner uses. Butterfly is deliberately excluded
    // by the prefix - the AddOn's dictionary calls it "strapped against the
    // intimate flesh", i.e. external.
    char cls[64];
    ClassOf(w, cls, sizeof cls);
    const bool classSaysPlug = (_strnicmp(cls, "Plug", 4) == 0);

    const auto m = static_cast<std::uint32_t>(w->GetSlotMask().underlying());
    const bool slotSaysPlug = (m & 0x08000000u) || (m & 0x00040000u);   // 57 / 48

    // A device with no plug class AND no plug slot is not a plug. A device with
    // a plug slot but another class (a belt that also fills an orifice) is not
    // narrated as an insertion either - its own class event covers it.
    if (!classSaysPlug) return 0;

    int sites = 0;
    if (HasKw(w, "zad_DeviousPlugVaginal")) sites |= 1;
    if (HasKw(w, "zad_DeviousPlugAnal"))    sites |= 2;
    if (!sites && slotSaysPlug) {
        if (m & 0x08000000u) sites |= 1;    // slot 57 - vaginal
        if (m & 0x00040000u) sites |= 2;    // slot 48 - anal
    }
    // Bare zad_DeviousPlug with no site keyword and no plug slot: we know it is
    // a plug and we do NOT know where. Say so, rather than inventing a site.
    return sites;
}

// ⛔ "IS IT A PLUG" AND "WHICH ORIFICE" ARE TWO QUESTIONS, and conflating them
// killed the honest-unknown path this file documents (2026-08-27, adversarial
// review). PlugSitesOf answers the SECOND and returns 0 for "it is a plug and I
// cannot tell where" - exactly the case SiteWord("unknown") exists to serve. But
// both callers tested `PlugSitesOf(w) != 0` as though it answered the FIRST, so a
// plug carrying only the bare zad_DeviousPlug keyword and neither plug slot:
//   * lost its claim exemption in EquipSink, so a hand-placed one was SUPPRESSED
//     there - and VRTE's OnDDZDeviceEquipped early-returns on plug classes too, so
//     it would have narrated NOWHERE;
//   * could never reach the plug branch of EmitDeviceChange, leaving SiteWord's
//     "unknown" row unreachable.
// ⚠ ZERO of the 1,221 DD devices in this load order hit it (verified against the
// records: every plug-class device carries a site keyword or a plug slot), so this
// changes no existing behaviour. It is fixed because a content mod can ship one
// and because an unreachable row that reads as live is how the last two
// regressions in this file started.
// ⚠ Uses the CLASS, exactly as PlugSitesOf's own gate does - belts and harnesses
// carry plug sub-keywords for the plug they hold and must NOT take this branch.
bool IsPlugClass(RE::TESObjectARMO* w)
{
    if (!w) return false;
    char cls[64];
    ClassOf(w, cls, sizeof cls);
    return _strnicmp(cls, "Plug", 4) == 0;
}

const char* SiteWord(int sites)
{
    if (sites == 3) return "both";
    if (sites == 1) return "vaginal";
    if (sites == 2) return "anal";
    return "unknown";      // a real plug whose orifice we could not establish
}

void SendDeviceChange(RE::Actor* a, const char* eventName, const char* payload)
{
    auto* src = SKSE::GetModCallbackEventSource();
    if (!src || !a) return;
    SKSE::ModCallbackEvent ev{};
    ev.eventName = eventName;
    ev.strArg    = payload;
    ev.numArg    = 0.0f;
    ev.sender    = a;
    src->SendEvent(&ev);
    logger::info("[WORN] {} 0x{:08X} {}", eventName, a->GetFormID(), payload);
}

// One equip or unequip that we did NOT cause, turned into the right event.
//
// Four events, because the user asked for two different tiers:
//   VRTE_DDZaZ_PlugInserted  / _PlugRemoved   - INTERRUPT.  A plug going into
//       or coming out of a body is a discrete physical act at the intimate
//       ladder's own level, and the user's words are "plug insertion will be
//       their own interrupt".
//   VRTE_DDZaZ_DeviceMenuOn  / _DeviceMenuOff - the ordinary device tier,
//       matching what a hand-placed device already gets.
// ⛔ `ours` IS LOAD-BEARING, and leaving it out was a real defect. When the plug
// emit lived in the finger-extraction path the player WAS always the agent, so
// VRTE's "<Player> drew the plug out of her" was true. This sink fires for every
// route there is - a menu unequip, a key unlock, DD's own RemoveDevice, an NPC's
// AI, a third-party script - and plugs deliberately bypass the claim, so without
// this flag every one of those narrated as the player doing it. That is the mod
// telling an LLM that the player did something the player did not do.
// ⛔ THE CHURN ECHO IS RETIRED (1.2.7, 2026-09-14). It was a 5 s per-(actor, device) window that
// dropped an equip or unequip seen shortly after the device's last event. Once removals were HELD
// and cancelled by a re-equip (1.2.5, DEFERRED REMOVALS below) it could no longer stop a false line
// - only swallow a true one: a real pull inside 5 s of any re-fit flicker on that device was never
// told, and a real re-insertion inside 5 s of a real insertion left the LLM believing the plug was
// out. What replaced it, per edge: OFF -> held, cancelled by the same device coming back on; ON ->
// NoteOn refuses a device that is already worn. `[cooldown] plugecho` is still read and ignored.

// ★ `narrate` (D16, 2026-09-13): false sends the MOD EVENT only. VRTouchEvents' plug handlers are
// pacing-only (they stamp its intimate clock), so a route whose LINE belongs to VRTE still has to
// send the event, or a genital touch in the next seconds narrates on top of VRTE's own line.
void EmitDeviceChange(RE::Actor* a, RE::TESObjectARMO* w, bool on, bool ours, bool narrate = true)
{
    if (!a || !w) return;

    char cls[64];
    ClassOf(w, cls, sizeof cls);

    // The rendered half carries no FULL name (report 23 §30). NameForDevice: the pushed
    // exact-FormID name, the pair map, the class push, then the record's own name. When
    // none answers the class token travels alone and the AddOn resolves it through TypeWord.
    std::string nm = NameForDevice(a->GetFormID(), w, cls);
    CleanDeviceName(nm);

    const int sites = PlugSitesOf(w);
    char buf[192];

    // ⚠ IsPlugClass, not `sites` - see its comment. A plug whose orifice we
    // cannot establish still takes the plug lane and reports site "unknown",
    // which VRTE renders as "their body" rather than naming an orifice.
    if (IsPlugClass(w)) {
        std::snprintf(buf, sizeof buf, "%s|%s|%s|%d", SiteWord(sites),
                      nm.empty() ? "" : nm.c_str(), cls, ours ? 1 : 0);
        SendDeviceChange(a, on ? "VRTE_DDZaZ_PlugInserted"
                               : "VRTE_DDZaZ_PlugRemoved", buf);
        if (narrate)
            DeviceEquip::NarrateDeviceChange(a->GetFormID(), true, on, buf);   // the line is the AddOn's
        return;
    }

    // ⚠ `locked` is NOT in this payload and must not be invented downstream: the
    // rendered half does not carry the lock state, and the gesture path's own
    // DeviceEquipped event is where that fact comes from.
    std::snprintf(buf, sizeof buf, "%s|%s|%s|%d",
                  nm.empty() ? "" : nm.c_str(), cls,
                  ContextWord(Classify(a)), ours ? 1 : 0);
    SendDeviceChange(a, on ? "VRTE_DDZaZ_DeviceMenuOn"
                           : "VRTE_DDZaZ_DeviceMenuOff", buf);
    if (narrate)
        DeviceEquip::NarrateDeviceChange(a->GetFormID(), false, on, buf);  // the line is the AddOn's
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★ DEFERRED REMOVALS (2026-09-13, the first VR session on 1.2.4). Devious Devices re-asserts a
// device as unequip -> equip 20 ms apart on every plug or gag change, outfit re-apply and cell
// load. The echo could only ever drop the SECOND event of that pair; the FIRST - "Telord drew
// The Vaginal Plug out of her vagina" - went out five times that evening for plugs that never
// moved, and the one REAL removal was then swallowed by the echo the false one had stamped.
// A removal is now held for kOffDeferS and CANCELLED by a re-equip of the same device inside the
// window; the line is a second or two late and never false.
// ★★ 1.2.7 (2026-09-14, VRTE's request "PlugRefitEcho" part A): THE WHOLE REMOVAL IS HELD, not just
// its line. 1.2.5 held only the narration - NoteOff still ran on the first edge of DD's flicker, so
// a device that never left her (a) lost its wear clock (below phase 2 no credit is kept, and NoteOn
// restarted it at ZERO - DD re-fits on every armour equip and cell load, so the early phases of the
// strain model could not accumulate in active play), (b) wrote a FALSE after-state ("the arms are
// weak") beside the worn block when it had reached phase 2, and (c) when it was her last device,
// erased her g_fx - the CLIMAX COUNT (so the kneel from the 4th could never come) and the 100 lane's
// cooldown. Now nothing about her changes until the hold ends with the device still off.
// ⚠ Every held removal still drains on PollStates' 3 s tick: a real removal is told 1.5-4.5 s late.
// ═══════════════════════════════════════════════════════════════════════════
// ★★ 1.2.8 (the user, 2026-09-14: "make sure that cell load and gears equipping don't affect our
// state and effect"): A DEVICE THAT IS STILL HERS IS NOT REMOVED, HOWEVER LONG IT IS OFF HER BODY.
// Measured on 09-13: DD's re-fit comes back in 16-34 ms (17 of 17), but an OStim scene held a pair of
// cuffs off for 33.8 s and put them back - past any fixed hold, so their clock, after-state and her
// effect record were spent on a strip. DD's own code draws the line: its UNLOCK takes the rendered
// half OUT of her inventory (zadLibs UnlockDevice :438 RemoveItem(rdevice), RemoveQuestDevice :535),
// while its scene strip only UNEQUIPS it (zadBQ00 :684-809 UnequipItem / EquipItem). So when a hold
// ends with the device still off, DrainPendingOff asks:
//   * a SexLab/OStim scene is running (or in its 15 s grace)  -> hold on; the scene gives it back
//   * a DD device whose rendered half is STILL IN HER INVENTORY -> DD only took it off her body
//     (a strip, an outfit re-apply, a cell-load re-fit): hold on, re-asked every 3 s, for at most
//     kStrippedCapS outside any scene
//   * otherwise (gone from her inventory, or ZaZ/DoM off outside a scene) -> a REAL removal
// `holdFrom` restarts while a scene runs, so the cap counts from the scene's end, not the strip's start.
struct PendingOff {
    std::uint32_t afid = 0, dfid = 0;
    bool   ours     = false;
    bool   emit     = false;
    double at       = 0.0;     // last (re)hold - drained kOffDeferS after it
    double holdFrom = 0.0;     // start of the current strip hold, for the cap
    bool   stripped = false;   // the drain found it off her body but still hers
};
std::vector<PendingOff> g_pendingOff;
constexpr double kOffDeferS    = 1.5;
constexpr double kStrippedCapS = 120.0;
constexpr double kKneelCdS     = 30.0;

// `emit` = the removal takes the event/narration lane when it drains (decided at the edge, where the
// claim that says WHO did it is still live). `ours` = the player (hands or menu) was the agent.
void QueueOff(std::uint32_t afid, std::uint32_t dfid, bool ours, bool emit)
{
    {
        std::scoped_lock lk(g_mtx);
        const double now = NowSeconds();
        bool         found = false;
        for (auto& p : g_pendingOff)
            if (p.afid == afid && p.dfid == dfid) { p.ours = ours; p.emit = emit; p.at = now; found = true; break; }
        if (!found) {
            PendingOff p{};
            p.afid = afid; p.dfid = dfid; p.ours = ours; p.emit = emit; p.at = now; p.holdFrom = now;
            g_pendingOff.push_back(p);
        }
    }
    // ★★ 1.3.11 THE PROMPT SEES THE PULL AT ONCE (the user, 2026-09-15: "during removing a device, the removal
    // trigger an direct naration, but the state is not yet replaced in the prompt"). We are IN the unequip event:
    // the device is already off her body, and `Report` walks her live biped slots, so the state file's text is
    // already right - nothing had rewritten the FILE until the next 3 s poll. Meanwhile a hand pull is narrated by
    // VRTouchEvents the moment our gate answers `done`, which is BEFORE our drain: measured 2.97 s (19:12:36.60 ->
    // 39.58) and 4.06 s (18:55:02.43 -> 06.49) in the 09-15 VR log, and her reply rendered the old block.
    // ⚠ Only the FILE is brought forward. The wear record, the after-state, the effect state and the removal LINE
    // still wait for DrainPendingOff (1.2.7), so a DD strip or re-fit still changes nothing she is told about.
    // Its mirror is in the equip sink's re-assert branch, so a flicker puts the device back just as fast.
    PromptState::RequestRefresh("pull");
}

// OnSave: a device whose removal is still UNDECIDED is probably off her. Saving its wear record would
// resurrect it on load, and the next equip of that device would then read as "already worn". A device
// the drain already found STRIPPED (still hers) keeps its record - it is coming back.
bool RemovalHeld(std::uint32_t afid, std::uint32_t dfid)   // caller holds g_mtx
{
    for (const auto& p : g_pendingOff)
        if (p.afid == afid && p.dfid == dfid) return !p.stripped;
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★ THE STRUGGLE (1.2.9, the user, 2026-09-14: "add the struggling awareness and result to
// SkyrimNet awareness ... a short event for the struggle and direct narration for the result ...
// the tired animation is at the end of the struggle"). Better NPC Support for Devious Devices
// (DD_NPC_Fixup.esp, its "Allow NPCs to struggle" option) lets an NPC try to escape her devices on
// its own timer: PlayStruggleAnimation sets rank 1 in its DDNF_Struggling faction, plays the struggle
// clip, pants and moans, and PerformEscapeAttempt unlocks/picks/struggles out of devices one by one
// (DD's UnlockDevice -> a REAL removal on our drain), then removes her from the faction. Nothing of
// that reached SkyrimNet: the removals narrated agentless, the struggle not at all.
//   * the faction's rising edge (PollStates, 3 s)  -> VRTE_DDZaZ_Struggle "start|<names>"
//     (the Controller files a short-lived event)
//   * every REAL removal while it runs, and for kStruggleSettleS after -> folded in as `freed`,
//     its own line suppressed (the mod event still goes out for VRTouchEvents' pacing)
//   * the tired kneel is DEFERRED to the end
//   * the falling edge + settle (no removal still held) ->
//     "end|<forced>|<unlocked>|<takenOff>|<remaining>|<kneelPhase>" (1.3.3; was end|freed|remaining|kneel)
//     (the Controller speaks the result) then TiredKneel if one was earned.
// Soft dependency: without DD_NPC_Fixup.esp the faction is null and none of this runs.
// ⛔ CORRECTED 1.3.3 (review of 2026-09-14): this banner used to say a 100 %-unlock escape skips the
// faction. FALSE - ddnf_npctracker_npc.psc :2268-2278 hands the faction to PlayStruggleAnimation on the
// unlock branch too (short animation), and :2493-2497 sets it before the short/long split. So a key
// unlock IS a struggle to us, and used to be told as "worked it off by her own effort".
// ★ 1.3.3 THE KEY ROUTE (the user: "Say she unlocked it"). BNSDD picks UNLOCK only when she is not
// hand-blocked (mittens / straitjacket), the device is not a quest device, and she holds DD's key count
// (or DD wants none) - TryToEscapeDevice :2170-2215. At the START we record that route per worn device
// (keys can be consumed by the unlock itself, so it is read BEFORE anything comes off); each removal
// folded in carries it, and the END sends three lists. ⚠ Not modelled: a lock-access difficulty that
// drops unlockChance under the struggle chance (BNSDD then struggles despite the key) - told as
// "unlocked"; and a key gained mid-struggle. Both rare; the log names the route per device.
// ═══════════════════════════════════════════════════════════════════════════
struct Struggle {
    struct Freed { std::string name; FreedHow how = FreedHow::Forced; };
    bool   active   = false;
    double startedAt = 0.0, endedAt = 0.0;
    std::string names;                  // what she wore when it started
    std::vector<Freed> freed;           // what came off while it ran, and how
    std::unordered_map<std::uint32_t, FreedHow> route;   // rendered FormID -> route, read at the START
    int    kneelPhase = 0;              // deepest qualifying after-state phase, played at the end
};
std::unordered_map<std::uint32_t, Struggle> g_struggle;   // under g_mtx
constexpr double kStruggleSettleS = 4.5;   // one drain cycle after the faction clears

RE::TESFaction* DdnfStrugglingFaction()
{
    static RE::TESFaction* fac = nullptr;   // latched only once FOUND - never a nullptr
    if (fac) return fac;
    auto* dh = RE::TESDataHandler::GetSingleton();
    if (!dh) return nullptr;
    fac = dh->LookupForm<RE::TESFaction>(0x0068FA, "DD_NPC_Fixup.esp");
    return fac;
}

// Is a real removal of hers part of a struggle (running, or just ended)?
bool StruggleOwns(std::uint32_t afid, double now)   // caller holds g_mtx
{
    auto it = g_struggle.find(afid);
    if (it == g_struggle.end()) return false;
    return it->second.active || (now - it->second.endedAt) < kStruggleSettleS + 3.0;
}

bool AnyRemovalHeld(std::uint32_t afid)   // caller holds g_mtx
{
    for (const auto& p : g_pendingOff)
        if (p.afid == afid && !p.stripped) return true;
    return false;
}

// A plain noun for a class token, for a device that has no pushed name yet (a DD rendered half is
// nameless). The AddOn's Papyrus TypeWord does the same job on its side; this is the C++ half.
std::string ClassNoun(const char* cls)
{
    if (!cls || !cls[0]) return "device";
    struct Row { const char* prefix; const char* noun; };
    static const Row rows[] = {
        { "PlugAnal", "anal plug" }, { "PlugVaginal", "vaginal plug" }, { "Plug", "plug" },
        { "PiercingsNipple", "nipple piercings" }, { "PiercingsVaginal", "intimate piercing" },
        { "GagRing", "ring gag" }, { "Gag", "gag" }, { "ArmbinderElbow", "elbow binder" },
        { "Armbinder", "armbinder" }, { "YokeBB", "breast yoke" }, { "YokeFront", "fiddle" },
        { "Yoke", "yoke" }, { "Collar", "collar" }, { "Belt", "chastity belt" }, { "Harness", "harness" },
        { "Boots", "boots" }, { "Gloves", "gloves" }, { "Corset", "corset" }, { "Blindfold", "blindfold" },
        { "Hood", "hood" }, { "ArmCuffs", "arm cuffs" }, { "CuffsArms", "arm cuffs" },
        { "LegCuffs", "leg cuffs" }, { "CuffsLegs", "leg cuffs" }, { "CuffsFront", "cuffs" },
        { "AnkleShackles", "ankle shackles" }, { "HobbleSkirt", "hobble skirt" },
        { "StraitJacket", "straitjacket" }, { "PetSuit", "pet suit" }, { "Suit", "suit" },
        { "PonyGear", "pony gear" }, { "ElbowTie", "elbow tie" }, { "BondageMittens", "mittens" },
        { "Bra", "chastity bra" }, { "Clamps", "clamps" }, { "HeavyBondage", "arm restraint" },
    };
    for (const auto& r : rows)
        if (_strnicmp(cls, r.prefix, std::strlen(r.prefix)) == 0) return r.noun;
    std::string s(cls);
    for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

// ⛔ Takes g_mtx per device (NameForDevice) - never call it with the lock held.
std::vector<std::string> CoveredNameList(RE::Actor* a)
{
    std::vector<std::string> out;
    std::vector<std::uint32_t> seen;
    ForEachWorn(a, [&](RE::TESObjectARMO* w) {
        if (!IsCoveredDevice(w) || KeywordlessOf(w)) return true;
        if (std::find(seen.begin(), seen.end(), w->GetFormID()) != seen.end()) return true;
        seen.push_back(w->GetFormID());
        char cls[64]; ClassOf(w, cls, sizeof cls);
        std::string nm = NameForDevice(a->GetFormID(), w, cls);
        CleanDeviceName(nm);
        if (nm.empty()) nm = "the " + ClassNoun(cls);
        out.push_back(nm);
        return true;
    });
    return out;
}

std::string CoveredNames(RE::Actor* a)
{
    std::string out;
    for (auto& n : CoveredNameList(a)) { if (!out.empty()) out += ", "; out += n; }
    return out;
}

// ★ 1.3.4 THE STRUGGLE'S DEVICE LISTS IN ENGLISH (the user, 2026-09-14, on "Carmella has taken off Pony
// Tail Plug, Grand Soulgem Vaginal Plug, and Black Leather Pony Boots (Tight), Black Leather Ball Strap
// Gag, Copper Wrist Cuffs Front stayed on." - one run-on list that read as if the boots came off too).
// "the A", "the A and the B", "the A, the B and the C". No article on a name that already has one
// ("the gag") or is a possessive proper name ("Mara's Grace", "Svana's Saviour").
// ⚠ BUILT HERE, NOT IN PAPYRUS, because the VM's string table is case-insensitive: a Papyrus "The "
// literal can come back as "the " (the 1.2.6 she/She lesson), so the capitalised sentence start is made
// in C++ and travels inside one whole field.
std::string WithArticle(const std::string& n)
{
    if (n.empty()) return n;
    if (_strnicmp(n.c_str(), "the ", 4) == 0) return n;
    if (n.find("'s ") != std::string::npos || n.find("\xE2\x80\x99s ") != std::string::npos) return n;
    return "the " + n;
}

std::string EnglishList(const std::vector<std::string>& names, bool capitalise)
{
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) out += (i + 1 == names.size()) ? " and " : ", ";
        out += WithArticle(names[i]);
    }
    if (capitalise && !out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 'a' + 'A');
    return out;
}

// SKSE task (a BSJobs worker thread - NOT the main thread: the 2026-09-14 log shows these tasks on six
// different threads), from PollStates' per-actor task. 1.3.3: PollStates also ticks an actor who has a
// struggle record but is no longer in g_on (a FULL escape took everything off - her END used to be lost).
// ⛔⛔ 1.3.2 - THE CTD OF 2026-09-14 (crash-2026-09-14-22-55-11): this used to call CoveredNames
// INSIDE its g_mtx scope. CoveredNames -> NameForDevice takes g_mtx itself, std::mutex is not
// recursive, and MSVC's lock() on a mutex the same thread already owns THROWS std::system_error
// "resource deadlock would occur". Nothing catches it on an SKSE task -> the game dies. It shipped
// in 1.2.9 and fired on the very first Better NPC Support escape attempt ever seen (Carmella, in the
// same 3 s poll as a Black Soulgem shock, which is why it looked like the shock). So: DECIDE under
// the lock, read the names with NO lock held, then COMMIT under the lock after re-checking - an
// equip sink on another thread may have moved g_struggle in between.
void StruggleTick(RE::Actor* a)
{
    auto* fac = DdnfStrugglingFaction();
    if (!fac || !a || a->GetFormID() == 0x14) return;
    const std::uint32_t fid = a->GetFormID();
    const double now = NowSeconds();
    const bool strug = !a->IsDead() && a->IsInFaction(fac);
    bool wantStart = false, wantEnd = false;
    {
        std::scoped_lock lk(g_mtx);
        auto it = g_struggle.find(fid);
        if (strug) {
            wantStart = (it == g_struggle.end() || !it->second.active);
        } else if (it != g_struggle.end()) {
            Struggle& s = it->second;
            if (s.active) { s.active = false; s.endedAt = now; }
            wantEnd = (now - s.endedAt) >= kStruggleSettleS && !AnyRemovalHeld(fid);
        }
    }
    if (!wantStart && !wantEnd) return;
    // ⛔ takes g_mtx per device - never call it locked. 1.3.4: kept as a LIST so the payloads carry
    // English ("the A, the B and the C") instead of a bare comma run.
    const std::vector<std::string> coveredList = CoveredNameList(a);
    const std::string covered      = EnglishList(coveredList, false);
    const std::string coveredCap   = EnglishList(coveredList, true);
    // ★ 1.3.3: the key route of every covered device, read BEFORE anything comes off (an unlock can
    // consume the key). EscapeRouteOf takes g_mtx itself - this runs with no lock held.
    std::unordered_map<std::uint32_t, FreedHow> route;
    int nUnlock = 0, nTakeOff = 0, nForced = 0;
    if (wantStart) {
        const bool blocked = HandsBlockedForKeys(a);
        ForEachWorn(a, [&](RE::TESObjectARMO* w) {
            if (!IsCoveredDevice(w) || KeywordlessOf(w)) return true;
            if (route.count(w->GetFormID())) return true;
            const FreedHow h = EscapeRouteOf(a, w, blocked);
            route.emplace(w->GetFormID(), h);
            (h == FreedHow::Unlocked ? nUnlock : h == FreedHow::TakenOff ? nTakeOff : nForced)++;
            return true;
        });
    }
    bool sendStart = false, sendEnd = false;
    std::string payload;
    int kneel = 0;
    {
        std::scoped_lock lk(g_mtx);
        auto it = g_struggle.find(fid);
        if (wantStart) {
            if (it == g_struggle.end() || !it->second.active) {
                Struggle s{};
                s.active = true; s.startedAt = now;
                s.names = covered;
                s.route = std::move(route);
                g_struggle[fid] = std::move(s);
                sendStart = true;
                payload = "start|" + (covered.empty() ? std::string("-") : covered);
            }
        } else if (it != g_struggle.end() && !it->second.active &&
                   (now - it->second.endedAt) >= kStruggleSettleS && !AnyRemovalHeld(fid)) {
            Struggle& s = it->second;
            std::vector<std::string> forcedV, unlockedV, takenOffV;
            for (auto& fr : s.freed) {
                auto& list = fr.how == FreedHow::Unlocked ? unlockedV
                           : fr.how == FreedHow::TakenOff ? takenOffV : forcedV;
                list.push_back(fr.name);
            }
            std::string forced    = EnglishList(forcedV, false);
            std::string unlocked  = EnglishList(unlockedV, false);
            std::string takenOff  = EnglishList(takenOffV, false);
            std::string remaining = covered;
            std::string remainCap = coveredCap;
            // ⚠ never an empty field: Papyrus's StringUtil.Split DROPS them (the F1 lesson)
            if (forced.empty())    forced = "-";
            if (unlocked.empty())  unlocked = "-";
            if (takenOff.empty())  takenOff = "-";
            if (remaining.empty()) remaining = "-";
            if (remainCap.empty()) remainCap = "-";
            // 1.3.4: field 7 = the still-on list capitalised for a sentence start (appended LAST so a
            // 1.3.3 Controller still reads fields 1-6).
            payload = "end|" + forced + "|" + unlocked + "|" + takenOff + "|" + remaining + "|" +
                      std::to_string(s.kneelPhase) + "|" + remainCap;
            kneel = s.kneelPhase;
            sendEnd = true;
            g_struggle.erase(it);
        }
    }
    if (sendStart) {
        logger::info("[WORN] STRUGGLE start on 0x{:08X} against: {} (routes: {} unlock, {} no key, {} forced)",
                     fid, payload.c_str() + 6, nUnlock, nTakeOff, nForced);
        SendDeviceChange(a, "VRTE_DDZaZ_Struggle", payload.c_str());
    } else if (sendEnd) {
        logger::info("[WORN] STRUGGLE end on 0x{:08X} -> {}", fid, payload);
        SendDeviceChange(a, "VRTE_DDZaZ_Struggle", payload.c_str());
        if (kneel >= 4 && !a->IsDead() && !a->IsUnconscious() && a->Is3DLoaded() && !PoseBusy(fid)) {
            DispatchPapyrus("TiredKneel", static_cast<std::int32_t>(fid), kneel, 0);
            logger::info("[WORN] TIRED KNEEL on 0x{:08X} - at the end of the struggle (phase {})", fid, kneel);
        }
    }
}

// An equip of the same device while its removal is still held: DD re-asserting. Nothing is said.
bool CancelOff(std::uint32_t afid, std::uint32_t dfid)
{
    std::scoped_lock lk(g_mtx);
    for (auto it = g_pendingOff.begin(); it != g_pendingOff.end(); ++it)
        if (it->afid == afid && it->dfid == dfid) { g_pendingOff.erase(it); return true; }
    return false;
}

// SKSE task (a BSJobs worker thread, not the main thread), from PollStates (every 3 s): a held
// removal older than the window is real.
void DrainPendingOff()
{
    const double now   = NowSeconds();
    const bool   scene = DeviceEquip::SceneOnNow();
    std::vector<PendingOff> due;
    {
        std::scoped_lock lk(g_mtx);
        for (auto it = g_pendingOff.begin(); it != g_pendingOff.end();) {
            if (now - it->at >= kOffDeferS) { due.push_back(*it); it = g_pendingOff.erase(it); }
            else ++it;
        }
    }
    // ⚠ While an entry is out of the list, a re-equip on another thread cannot CancelOff it (CancelOff
    // finds nothing and the equip is recorded as an ordinary on). The worn check below is what keeps
    // such a device from being narrated as removed. (The old comment called this the main thread; the
    // 2026-09-14 review found these tasks on BSJobs worker threads. The window is theoretical so far.)
    std::vector<PendingOff> rehold;
    bool removed = false;                                    // 1.3.0: a real removal -> refresh the file
    std::unordered_map<std::uint32_t, int> kneel;           // actor -> deepest qualifying phase
    for (auto p : due) {
        auto* af = RE::TESForm::LookupByID(p.afid);
        auto* a  = af ? af->As<RE::Actor>() : nullptr;
        auto* df = RE::TESForm::LookupByID(p.dfid);
        auto* w  = df ? df->As<RE::TESObjectARMO>() : nullptr;
        if (!a || !w) {                                   // forms gone: close the record, say nothing
            NoteOff(p.afid, p.dfid, nullptr);
            continue;
        }
        // ⚠ Still off? A re-equip that missed the cancel (a different route, a load) must not
        // narrate a removal of something she is wearing - and its wear record was never touched.
        bool worn = false;
        ForEachWorn(a, [&](RE::TESObjectARMO* x) { if (x == w) { worn = true; return false; } return true; });
        if (worn) {
            logger::info("[WORN] held removal of 0x{:08X} on 0x{:08X} dropped - it is back on", p.dfid, p.afid);
            continue;
        }
        // ★★ 1.2.8 - off her body, but is it off HER? (see the PendingOff banner)
        if (scene) {
            if (!p.stripped)
                logger::info("[WORN] 0x{:08X} off 0x{:08X} during a scene - held until the scene is over, nothing changed",
                             p.dfid, p.afid);
            p.stripped = true; p.at = now; p.holdFrom = now;
            rehold.push_back(p);
            continue;
        }
        const bool ddDevice = HasKw(w, "zad_Lockable") || HasKw(w, "zad_DeviousPlug");
        // ⚠ GetItemCount is PlayerCharacter-only in CommonLibVR; every actor has GetInventoryCounts.
        std::int32_t stillHers = 0;
        if (ddDevice) {
            const auto counts = a->GetInventoryCounts([w](RE::TESBoundObject& o) { return &o == w; });
            if (auto c = counts.find(w); c != counts.end()) stillHers = c->second;
        }
        if (ddDevice && stillHers > 0) {
            if (now - p.holdFrom < kStrippedCapS) {
                if (!p.stripped)
                    logger::info("[WORN] 0x{:08X} off 0x{:08X} but still in her inventory - DD took it off her body, "
                                 "not off her (strip / outfit / re-fit): held, nothing changed", p.dfid, p.afid);
                p.stripped = true; p.at = now;
                rehold.push_back(p);
                continue;
            }
            logger::info("[WORN] 0x{:08X} still unworn in 0x{:08X}'s inventory after {:.0f} s outside a scene - treated as removed",
                         p.dfid, p.afid, now - p.holdFrom);
        }
        // ★ The removal is real: NOW the wear record closes and the after-state is written - on every
        // route and for the dead too, exactly as the unconditional chokepoint did before 1.2.7.
        Wear wornRec{};
        if (NoteOff(a->GetFormID(), w->GetFormID(), &wornRec)) {
            removed = true;
            StrainTier t = kStrainNone;
            const int phase = NoteAftermath(a, w, wornRec, &t);
            // ★★ THE TIRED KNEEL (the user, 2026-09-14): "it does the bleedout animation for when device
            // are removed that reached effect 4" - "only those device that are really restraining: Arm
            // binders, Yokes, Leg restraints, Rope binds". Gags and nipple clamps are out ("painful, but
            // wont throw someone on their knee"), shackles/cuffs/collars never reach phase 4 (strap
            // ceiling 2), mittens and suits were not chosen. The tier and phase are NoteAftermath's own.
            const bool restraining = (t == kStrainArms || t == kStrainYoke ||
                                      t == kStrainLimb || t == kStrainRope);
            // ★ 1.2.9: a removal that is part of HER STRUGGLE is folded into the struggle's result and
            // its kneel waits for the struggle's end (the user: "the tired animation is at the end of
            // the struggle"). Its own line is suppressed; the mod event still goes out below.
            bool struggling = false;
            // ⛔ 1.3.2: the name is read BEFORE the lock - NameForDevice takes g_mtx itself, and this
            // used to call it inside the scope below (the same re-lock throw as StruggleTick's CTD).
            // A real removal is rare, so reading one name that may go unused costs nothing.
            char foldCls[64]; ClassOf(w, foldCls, sizeof foldCls);
            std::string foldName = NameForDevice(p.afid, w, foldCls);
            CleanDeviceName(foldName);
            if (foldName.empty()) foldName = "the " + ClassNoun(foldCls);
            // ★ 1.3.3: the route as it reads NOW, used only when the struggle's START snapshot has no
            // entry for this device (it went on mid-struggle, or the start tick came late). Read with no
            // lock held - EscapeRouteOf takes g_mtx. A consumed key reads as Forced here, hence the snapshot.
            const FreedHow routeNow = EscapeRouteOf(a, w, HandsBlockedForKeys(a));
            FreedHow foldHow = routeNow;
            bool fromSnapshot = false;
            {
                std::scoped_lock lk(g_mtx);
                if (StruggleOwns(p.afid, now)) {
                    struggling = true;
                    auto& s = g_struggle[p.afid];
                    if (auto r = s.route.find(p.dfid); r != s.route.end()) { foldHow = r->second; fromSnapshot = true; }
                    s.freed.push_back({ foldName, foldHow });
                    if (restraining && phase >= 4) s.kneelPhase = (std::max)(s.kneelPhase, phase);
                }
            }
            if (struggling) {
                logger::info("[WORN] removal of 0x{:08X} on 0x{:08X} is part of her struggle - folded into its result ({}, {})",
                             p.dfid, p.afid,
                             foldHow == FreedHow::Unlocked ? "unlocked" : foldHow == FreedHow::TakenOff ? "taken off" : "forced",
                             fromSnapshot ? "route read at the start" : "no start record - route read now");
                if (p.emit && !a->IsDead() && !a->IsDisabled())
                    EmitDeviceChange(a, w, false, p.ours, /*narrate=*/false);
                continue;
            }
            if (restraining && phase >= 4) {
                int& k = kneel[p.afid];
                k = (std::max)(k, phase);
            }
        }
        if (p.emit && !a->IsDead() && !a->IsDisabled())
            EmitDeviceChange(a, w, false, p.ours);
    }
    if (!rehold.empty()) {
        std::scoped_lock lk(g_mtx);
        for (auto& p : rehold) g_pendingOff.push_back(p);
    }
    if (removed) PromptState::RequestRefresh("removal");
    // One kneel per NPC, however many qualifying devices came off together, and not again inside
    // kKneelCdS (an armbinder then the boots one poll apart is still one collapse).
    for (auto& [afid, phase] : kneel) {
        auto* af = RE::TESForm::LookupByID(afid);
        auto* a  = af ? af->As<RE::Actor>() : nullptr;
        if (!a || afid == 0x14 || a->IsDead() || a->IsDisabled() || !a->Is3DLoaded()) continue;
        if (a->IsUnconscious()) {                           // D3: nothing is done to an unconscious body
            logger::info("[WORN] tired kneel on 0x{:08X} skipped - unconscious", afid);
            continue;
        }
        {
            std::scoped_lock lk(g_mtx);
            auto& st = g_fx[afid];
            if (st.posing) {                                // 1.3.5: the climax clip owns her
                logger::info("[WORN] tired kneel on 0x{:08X} skipped - the climax pose is playing", afid);
                continue;
            }
            if (now < st.kneelCd) continue;
            st.kneelCd = now + kKneelCdS;
        }
        DispatchPapyrus("TiredKneel", static_cast<std::int32_t>(afid), phase, 0);
        logger::info("[WORN] TIRED KNEEL on 0x{:08X} - a restraining device at phase {} came off -> bleedout 3 s",
                     afid, phase);
    }
}

class EquipSink : public RE::BSTEventSink<RE::TESEquipEvent>
{
public:
    RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent*        e,
                                          RE::BSTEventSource<RE::TESEquipEvent>*) override
    {
        if (!e || !e->actor) return RE::BSEventNotifyControl::kContinue;
        auto* act = e->actor->As<RE::Actor>();
        if (!act) return RE::BSEventNotifyControl::kContinue;

        auto* form = RE::TESForm::LookupByID(e->baseObject);
        auto* armo = form ? form->As<RE::TESObjectARMO>() : nullptr;
        if (!armo) return RE::BSEventNotifyControl::kContinue;

        // Only devices. An NPC changing her boots must not touch this store.
        // ★ WIDENED 2026-08-30: ZaZ and DoM devices now get the wear clock and
        // the equip events too - the incorporation's tracker half.
        if (!IsCoveredDevice(armo))
            return RE::BSEventNotifyControl::kContinue;

        // ⚠ THE WORN HALF ONLY. A DD device is a PAIR and both halves can carry
        // the keywords above, so without this the sink fires TWICE for one
        // equip - once for the inventory record and once for the rendered one.
        // The rendered half is the one with biped slots; the inventory half has
        // none. Same pair-splitting rule as everywhere else in this AddOn.
        if (static_cast<std::uint32_t>(armo->GetSlotMask().underlying()) == 0)
            return RE::BSEventNotifyControl::kContinue;

        // ★ THE UNCONDITIONAL CHOKEPOINT. Everything below this line is gated
        // one way or another - the player filter, the dead/disabled test, and
        // `isPlug || !ours`. This is not, and it runs for every device change on
        // every route, which is why both halves of the wear model hang here.
        if (e->equipped) {
            // A held removal of this very device: DD re-asserting it. Nothing happened - the wear
            // record, the after-state and her effect state were never touched (1.2.7), so there is
            // nothing to restore and nothing to say.
            if (CancelOff(act->GetFormID(), armo->GetFormID())) {
                // ⚠ NoteOn only STARTS a clock the record does not already have (a device first seen
                // off, e.g. worn since before this session) - for a tracked device it is a no-op.
                NoteOn(act->GetFormID(), armo->GetFormID(), WearCredit(act, armo));
                logger::info("[WORN] re-equip of 0x{:08X} on 0x{:08X} inside the removal window - DD re-asserting, nothing changed",
                             armo->GetFormID(), act->GetFormID());
                // 1.3.11: the mirror of QueueOff's refresh. The strip wrote "not on her" within a frame, so the
                // re-assert (DD's own churn returns in 16-34 ms) must put it back just as fast, or the prompt
                // would read one device short until the next poll.
                PromptState::RequestRefresh("re-equip");
                return RE::BSEventNotifyControl::kContinue;
            }
            if (!NoteOn(act->GetFormID(), armo->GetFormID(), WearCredit(act, armo))) {
                logger::info("[WORN] equip of an already-worn device 0x{:08X} on 0x{:08X} - not a change",
                             armo->GetFormID(), act->GetFormID());
                return RE::BSEventNotifyControl::kContinue;
            }
            ForgetStaleClassName(act, armo);                // D9, second pass
            PromptState::RequestRefresh("equip");            // 1.3.0: the next render sees it
            // ★ 1.3.9 A GAG OR HOOD ON THE NPC WHO IS TALKING CUTS HER LINE (the user, 2026-09-15: "When we give a
            // NPC a gag ball, or anything that goes in the mouth, like a hood, if that current NPC is the one
            // talking, they should get an interupt. Only if they are the currently talking NPC"). Seen that day: the
            // ball gag went on at 17:46:38.8 while Carmella's line ran 31.7 -> 40.8, unmuffled to its end. Fired HERE,
            // at the equip, on every route: VRTouchEvents' hand-equip line arrives ~1.5 s later and the AddOn's
            // menu line a task hop later, so the cut lands before the reaction and cannot swallow it.
            // ⚠ SkyrimNet's purge is global (other NPCs' queued lines go too) - one more reason it only fires when
            // SHE is the one talking.
            if (act->GetFormID() != 0x14 && !act->IsDead() && IsSpeechDevice(armo) && IsTalking(act->GetFormID())) {
                DispatchPapyrus("CutSpeech", static_cast<std::int32_t>(act->GetFormID()));   // ⛔ one Int
                logger::info("[SPEECH] 0x{:08X} is talking and 0x{:08X} (gag/hood) went on - her line is cut",
                             act->GetFormID(), armo->GetFormID());
            }
        } else {
            // ★★ 1.2.7: EVERY removal is held, on every route - the wear record, the after-state and
            // the line all wait for DrainPendingOff. The routing is decided HERE, while the claim that
            // says who did it is still live: the player's own record, a corpse or a disabled actor
            // closes its clock but never speaks; a keyword-less device (Job B) never speaks; a claimed
            // hand removal of a non-plug is VRTouchEvents' line; everything else takes our lane.
            bool emit = false, agent = false;
            if (act->GetFormID() != 0x14 && !act->IsDead() && !act->IsDisabled()) {
                const bool ours = GestureClaimLive(act->GetFormID());   // ⚠ a mutator - read once
                const bool menu = MenuClaimLive(act->GetFormID());
                if (KeywordlessOf(armo))
                    logger::info("[WORN] keyword-less device 0x{:08X} off 0x{:08X} - worn-block awareness only, no event",
                                 armo->GetFormID(), act->GetFormID());
                else if (IsPlugClass(armo) || !ours) {
                    emit  = true;
                    agent = ours || menu;
                }
            }
            QueueOff(act->GetFormID(), armo->GetFormID(), agent, emit);
            return RE::BSEventNotifyControl::kContinue;
        }

        // ── narrate it ──────────────────────────────────────────────────────
        // The player is excluded: SkyrimNet narrates to NPCs about what they
        // perceive, and every other event on this bus carries an NPC sender.
        //
        // ⚠ THE CLAIM DOES NOT APPLY TO PLUGS, and that asymmetry is the whole
        // point. The user asked for plug insertion to be "their own interrupt";
        // a hand equip is narrated by VRTouchEvents' own equip line (plus our
        // DeviceFitted awareness since 2026-09-13), never at the interrupt tier.
        // If a hand-placed plug were claimed
        // here it would be suppressed at the loud tier and reported at the quiet
        // one - exactly backwards. So a plug ALWAYS emits from here, whatever
        // put it there, and VRTE suppresses the persistent line for plug classes
        // instead. Every other device keeps the claim, because for those the
        // gesture's own event is the better one.
        if (act->GetFormID() != 0x14 && !act->IsDead() && !act->IsDisabled()) {
            // ⚠ Read the claim ONCE. GestureClaimLive erases expired entries, so
            // it is a mutator as well as a predicate, and it now answers two
            // different questions: may the sink speak, and who did this.
            const bool ours   = GestureClaimLive(act->GetFormID());
            // ★ THE MENU CLAIM (2026-08-27): the player working this actor's
            // trade/container menu IS the flatrim gesture. It only decorates
            // the agent flag - it never suppresses, because for a menu equip
            // the sink's own event is the only narration there is.
            const bool menu   = MenuClaimLive(act->GetFormID());
            // ⚠ IsPlugClass, not PlugSitesOf != 0: the claim exemption belongs
            // to every plug, including one whose orifice we cannot name.
            const bool isPlug = IsPlugClass(armo);
            // ★ D2 (the user, 2026-09-13: "b") - a plug PPB cannot class is VRTouchEvents' to
            // narrate when the player's HANDS put it IN: VRTE voices that equip as ordinary gear
            // (PPB_GestureGearEquipped, <ordinary> 0), so our plug line on top was a second telling
            // of the same act.
            // ⛔ INSERTION ONLY (the user, same day: "plug removal, it will be in the AddOn, there is
            // no case where this will happen with normal gears anyway"). Every plug REMOVAL takes
            // the plug lane here - hand, fingertip, menu, key, script - and VRTouchEvents is asked to
            // skip a removal on the plug slots 57/48 (DDSN_to_VRTE_Response_2026-09-13_D1_D2.md).
            const bool vrtePlug = isPlug && ours && e->equipped && !PpbSeesPlug(armo);
            if (vrtePlug) {
                logger::info("[WORN] hand-gesture plug 0x{:08X} put in 0x{:08X} has no DD plug class - "
                             "VRTouchEvents narrates the insertion, the plug lane stays quiet",
                             armo->GetFormID(), act->GetFormID());
                // ★ D16 (2026-09-13, my own regression from D2): the MOD EVENT still goes out.
                // VRTouchEvents' PlugInserted handler is pacing-only - it stamps the intimate
                // clock so a genital touch in the next seconds is not narrated on top of its own
                // insertion line. Only OUR line stays quiet.
                EmitDeviceChange(act, armo, true, true, /*narrate=*/false);
            }
            // ★ JOB B (2026-09-13): a keyword-less device keeps its wear clock (above) and reaches the
            // LLM through the worn block and the observer view only - the user's "basic awareness".
            // No on/off event: Pama's furniture scripts equip these themselves ("Pama controls them"),
            // and every furniture scene would otherwise speak "is now wearing ropes" to the room.
            else if (KeywordlessOf(armo))
                logger::info("[WORN] keyword-less device 0x{:08X} on 0x{:08X} - worn-block awareness only, no event",
                             armo->GetFormID(), act->GetFormID());
            // (only EQUIPS reach this block since 1.2.7 - every removal returned above, held)
            else if (isPlug || !ours)
                EmitDeviceChange(act, armo, true, ours || menu);
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

class EffectSink : public RE::BSTEventSink<RE::TESActiveEffectApplyRemoveEvent>
{
public:
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESActiveEffectApplyRemoveEvent*                 e,
        RE::BSTEventSource<RE::TESActiveEffectApplyRemoveEvent>*) override
    {
        // ⚠ This fires for EVERY magic effect on every actor in the cell, so the
        // filter has to be the first thing and it has to be cheap: only actors we
        // already know are wearing a device get looked at any further.
        if (!e || !e->target) return RE::BSEventNotifyControl::kContinue;
        auto* act = e->target->As<RE::Actor>();
        if (!act) return RE::BSEventNotifyControl::kContinue;
        {
            std::scoped_lock lk(g_mtx);
            if (g_on.find(act->GetFormID()) == g_on.end())
                return RE::BSEventNotifyControl::kContinue;
        }
        // The event carries a unique id rather than the effect, so re-read the
        // actor's state instead of trying to resolve it. Same answer, less to
        // get wrong.
        ReEvaluate(act);
        return RE::BSEventNotifyControl::kContinue;
    }
};

EquipSink  g_equipSink;
EffectSink g_effectSink;
bool       g_armed = false;

} // namespace

void ArmOrgasmSink();   // 1.3.5, defined with the sink beside NoteClimax

void Install()
{
    if (g_armed) return;
    // ★ EAGERLY, so the log states the tuning at startup rather than at the
    // first strain query. Every consumer also lazy-guards on g_scLoaded, so a
    // path that runs before Install still gets the right numbers - this call
    // only decides WHEN the "[SCALES] n taken, n refused" line appears, which
    // is the line a user retuning the file needs to see.
    if (!g_scLoaded) LoadScalesIni();
    // Job B (2026-09-13): resolve the keyword-less device records BEFORE any sink can ask about them.
    if (g_keywordless.empty()) ResolveKeywordless();
    auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
    if (!holder) {
        logger::warn("[WORN] no event source holder - the wear clock is INERT.");
        return;
    }
    holder->AddEventSink<RE::TESEquipEvent>(&g_equipSink);
    holder->AddEventSink<RE::TESActiveEffectApplyRemoveEvent>(&g_effectSink);
    holder->AddEventSink<RE::TESSpellCastEvent>(&g_castSink);
    // The ContainerMenu open/close edges, for the flatrim player-agent claim.
    // UI exists well before kDataLoaded; if it somehow does not, the claim
    // degrades to the live IsMenuOpen test alone and loses only the
    // after-close window.
    if (auto* ui = RE::UI::GetSingleton())
        ui->AddEventSink<RE::MenuOpenCloseEvent>(&g_menuSink);
    else
        logger::warn("[WORN] no UI singleton - the menu-agent claim has no "
                     "after-close window.");
    ArmLocationKeywords();
    ArmOrgasmSink();
    g_armed = true;
    logger::info("[WORN] armed: TESEquipEvent (wear clock) + "
                 "TESActiveEffectApplyRemoveEvent (vibration / shock edges) + "
                 "TESSpellCastEvent (the magicka-reactive trigger) + "
                 "MenuOpenCloseEvent (the menu-agent claim) + "
                 "DeviceActorOrgasm (DD's orgasm signal, the climax pose).");
}

// ─────────────────────────────────────────────────────────────────────────────
// ── PUSHED IN FROM THE QUEST SCRIPT ─────────────────────────────────────────
// The quest script polls arousal for these. ⚠ It has to be a poll rather than
// an event subscription, because the two arousal mods this has to work with -
// OSL Aroused and classic SexLab Aroused - do not share an event, but they DO
// share the `slaUtilScr.GetActorExposure` API. Polling the shared API is the
// only genuinely interchangeable route.
// The 3 s heartbeat for the two observable device states. See WornDevices.h for
// why the effect sink alone could not see a vibration start or stop.
//
// ⚠ IT HOPS TO THE MAIN THREAD, and that is not optional. This native is called
// from Papyrus, i.e. on the VM thread, but ReEvaluate has always run on the main
// thread (the two engine sinks) and its unlocked read of `weStartedVib` is safe
// only because every writer shares that thread. Running it here directly would
// break that premise and would also re-enter the VM from inside a running
// Papyrus stack. The task interface is the same hop DeviceEquip already uses.
//
// ⚠ SNAPSHOT THE ACTOR LIST UNDER THE LOCK, then work outside it: ReEvaluate
// takes g_mtx itself, and std::mutex is not recursive.
//
// ⚠ Shock pacing is unchanged. The roll inside ReEvaluate is bounded by the 90 s
// per-actor cooldown, not by how often it is asked - and active-effect events
// already arrive in BURSTS, so a steady 3 s tick asks no more often than the
// sink did, and usually less.
// ★★ THE AFTER-STATE AS A PROMPT BLOCK (2026-09-10). The user: "keep the same thing, a state
// prompt in the LLM context window, except change it for how they feel after release according
// to how far the effect was. And only keep it for a couple of prompt, with subsequent one
// explaining the pain/ache is diminishing."
//   prompt 1   AftermathLine at the phase reached - how far the strain got decides the words
//   prompt 2   the same line, easing (phase 5 moves on to its deficit line)
//   prompt 3   AftermathFading - what little remains
// then nothing. One countdown per strain tier, so an armbinder and a gag coming off together read
// as two sentences. The clock is the decorator call itself (SkyrimNet runs every Papyrus decorator
// once per conversation cycle), with the recovery blocks' 5-second floor. It also runs out by game
// time - the phase's [aftermath] hours - if she is part of no conversation in the meantime.
namespace {
std::string CapFirst(const char* text)
{
    std::string out = text ? text : "";
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z')
        out[0] = static_cast<char>(out[0] - 'a' + 'A');
    return out;
}

// What is left by the THIRD prompt: the faintest form of each tier. Physical fact, no pronouns.
const char* AftermathFading(StrainTier t)
{
    switch (t) {
    case kStrainArms:    return "a dull ache in the shoulders and some weakness in the arms";
    case kStrainYoke:    return "a dull burn across the shoulders";
    case kStrainMitt:    return "some stiffness in the fingers";
    case kStrainLimb:    return "a dull ache in the legs";
    case kStrainRope:    return "faint marks where the turns sat, and a little stiffness under them";
    case kStrainStrap:   return "a faint line on the skin where the edges sat";
    case kStrainGagOpen: return "a little stiffness in the jaw";
    case kStrainGagFill: return "a little soreness in the jaw";
    case kStrainClamp:   return "a faint throb where the grip was";
    case kStrainSuit:    return "a last trace of dampness on the skin";
    default:             return "a fading soreness where the restraint sat";
    }
}
}  // namespace

// ★ 1.3.0: the state FILE reads a residual's stage from its elapsed LIFE - the first third full, the
// second easing, the last fading - and never touches the prompt countdown (which stays the
// decorators' clock for any prompt that still calls them).
int FileStageOf(const Residual& r, float day)
{
    const float span = r.endDay - r.startDay;
    const float frac = span > 0.0f ? (day - r.startDay) / span : 1.0f;
    return frac < (1.0f / 3.0f) ? 3 : (frac < (2.0f / 3.0f) ? 2 : 1);
}

std::string AftermathState(RE::Actor* a, bool advance)
{
    if (!a) return {};
    const float  day  = GameDaysNow();
    const double real = NowSeconds();
    std::string out;
    std::scoped_lock lk(g_mtx);
    auto ia = g_after.find(a->GetFormID());
    if (ia == g_after.end()) return {};
    for (auto it = ia->second.begin(); it != ia->second.end();) {
        Residual&  r = it->second;
        const auto t = static_cast<StrainTier>(it->first);
        if (day >= r.endDay) { it = ia->second.erase(it); continue; }      // ran out unseen
        int stage = 3;
        if (advance) {
            if (r.shownAt <= 0.0) {
                r.shownAt = real;                                            // the first prompt to see it
            } else if (real - r.shownAt >= 5.0) {
                --r.left;                                                    // one step per prompt
                r.shownAt = real;
            }
            if (r.left <= 0) { it = ia->second.erase(it); continue; }
            stage = r.left;
        } else {
            stage = FileStageOf(r, day);
        }
        std::string line;
        if (stage >= 3) {
            if (const char* l = AftermathLine(t, r.phase, false)) line = CapFirst(l) + ".";
        } else if (stage == 2) {
            if (const char* l = AftermathLine(t, r.phase, r.phase >= 5))
                line = CapFirst(l) + ". It is already easing, noticeably less than when the restraint came off.";
        } else {
            line = std::string("What remains now is fading fast: ") + AftermathFading(t) + ".";
        }
        if (!line.empty()) {
            if (!out.empty()) out += " ";
            out += line;
        }
        ++it;
    }
    if (ia->second.empty()) g_after.erase(ia);
    return out;
}

// What an ONLOOKER can see of it: AftermathWitness, the outward-sign table, while the wearer is still
// on the full or easing prompt. Never moves the countdown - someone looking does not heal her.
std::string AftermathVisible(RE::Actor* a, bool forFile)
{
    if (!a) return {};
    const float day = GameDaysNow();
    std::string out;
    std::scoped_lock lk(g_mtx);
    auto ia = g_after.find(a->GetFormID());
    if (ia == g_after.end()) return {};
    for (const auto& [tier, r] : ia->second) {
        if (day >= r.endDay) continue;
        if (forFile ? (FileStageOf(r, day) <= 1) : (r.left <= 1)) continue;
        if (const char* w = AftermathWitness(static_cast<StrainTier>(tier), r.phase)) {
            if (!out.empty()) out += " ";
            out += CapFirst(w) + ".";
        }
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★ THE PROMPT STATE FILE - the snapshot side (1.3.0, 2026-09-14). PromptState.cpp writes the
// file; this builds what goes in it, from the same C++ the decorators call, plus two things the
// Papyrus side used to own: the restraint label's fallback chain and the release countdown.
// ═══════════════════════════════════════════════════════════════════════════
namespace
{
    // The Papyrus decorators' second step (VRTE_DDZaZ_Decorators.psc GetGagName & co.): DeviceEquip's
    // registry name first, then the named half of the first device of that category she wears - here
    // through NameForDevice (pushed name -> pair table -> class push) - then the category noun. "" only
    // when she is genuinely free in that category, exactly as the decorators promise the prompts.
    std::string RestraintLabel(RE::Actor* a, int cat)
    {
        if (!a || cat < 0 || cat > 5 || !DeviceEquip::IsRestrained(a, cat)) return {};
        std::string nm = DeviceEquip::RestraintName(a, cat);
        if (!nm.empty()) return nm;
        static const char* const kPrefix[6][8] = {
            { "Gag", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr },
            { "Blindfold", "Hood", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr },
            { "Armbinder", "Yoke", "BondageMittens", "CuffsFront", "ElbowTie", "StraitJacket", "Boxbinder", "HeavyBondage" },
            { "HobbleSkirt", "AnkleShackles", "Boots", "PonyGear", "PetSuit", nullptr, nullptr, nullptr },
            { "StraitJacket", "PetSuit", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr },
            { "Hood", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr },
        };
        static const char* const kNoun[6] = { "a gag", "a blindfold", "arm restraints",
                                              "leg restraints", "full-body restraint", "a hood" };
        std::string found;
        ForEachWorn(a, [&](RE::TESObjectARMO* w) {
            if (!IsCoveredDevice(w)) return true;
            char cls[64]; ClassOf(w, cls, sizeof cls);
            for (int i = 0; i < 8 && kPrefix[cat][i]; ++i) {
                if (_strnicmp(cls, kPrefix[cat][i], std::strlen(kPrefix[cat][i])) == 0) {
                    std::string n = NameForDevice(a->GetFormID(), w, cls);
                    CleanDeviceName(n);
                    if (!n.empty()) { found = n; return false; }
                }
            }
            return true;
        });
        return found.empty() ? std::string(kNoun[cat]) : found;
    }

    // ★ THE RELEASE COUNTDOWN, DLL-owned and TIME-based (the first cut the port allowed for): "3"
    // for the first kReleaseStepS after a category is released, then "2", then "1", then "". "" while
    // it is on, so a constraint block and its release block are never both up. The decorators'
    // render-counted version stays for prompts that still call them.
    struct Release { bool was[6] = {}; double at[6] = {}; };
    std::unordered_map<std::uint32_t, Release> g_release;   // under g_mtx
    constexpr double kReleaseStepS = 20.0;

    std::string ReleaseCount(std::uint32_t afid, int cat, bool restrainedNow, double now)   // g_mtx held
    {
        auto& r = g_release[afid];
        if (restrainedNow) { r.was[cat] = true; r.at[cat] = 0.0; return {}; }
        if (r.was[cat]) { r.was[cat] = false; r.at[cat] = now; }
        if (r.at[cat] <= 0.0) return {};
        const double age = now - r.at[cat];
        if (age < kReleaseStepS)     return "3";
        if (age < 2 * kReleaseStepS) return "2";
        if (age < 3 * kReleaseStepS) return "1";
        r.at[cat] = 0.0;
        return {};
    }
}

// ★ 1.3.1 - VRTouchEvents' refused-pull line ("Telord pulled at The Gag on Carmella, but The Gag
// stayed on") names the CLASS because PPB carries the class as the piece name when the held half is
// nameless. VRTE asked for the name we know (VRTE_to_DDSN_Request_2026-09-14_WornDeviceName_Native).
// Keyed on the slot mask - what VRTE holds at that moment from PPB_GestureUndressEnd.
std::string WornDeviceName(RE::Actor* a, std::uint32_t slotMask)
{
    if (!a || !slotMask) return {};
    std::string found;
    ForEachWorn(a, [&](RE::TESObjectARMO* w) {
        if (!IsCoveredDevice(w)) return true;
        const std::uint32_t bits = static_cast<std::uint32_t>(w->GetSlotMask().underlying());
        if ((bits & slotMask) == 0) return true;
        char cls[64]; ClassOf(w, cls, sizeof cls);
        std::string nm = NameForDevice(a->GetFormID(), w, cls);
        CleanDeviceName(nm);
        if (!nm.empty()) { found = nm; return false; }
        return true;
    });
    logger::info("[NAME] WornDeviceName 0x{:08X} slot {} -> '{}'", a->GetFormID(), slotMask, found);
    return found;
}

std::vector<std::uint32_t> PromptActors()
{
    std::vector<std::uint32_t> out;
    std::scoped_lock lk(g_mtx);
    auto add = [&](std::uint32_t f) { if (std::find(out.begin(), out.end(), f) == out.end()) out.push_back(f); };
    for (auto& [afid, devs] : g_on) add(afid);
    for (auto& [afid, tiers] : g_after) add(afid);
    for (auto& [afid, r] : g_release)
        for (int c = 0; c < 6; ++c)
            if (r.was[c] || r.at[c] > 0.0) { add(afid); break; }
    return out;
}

// Main thread. Everything here is what the decorators would have returned for her right now.
bool PromptEntry(RE::Actor* a, PromptFields& out)
{
    if (!a || a->GetFormID() == 0x14) return false;
    const std::uint32_t afid = a->GetFormID();
    const double now = NowSeconds();
    if (a->IsDead() || a->IsDisabled()) {
        std::scoped_lock lk(g_mtx);
        g_release.erase(afid);
        return false;
    }
    const char* dn = a->GetDisplayFullName();   // TESObjectREFR - the name a template sees
    out.name        = dn ? dn : "";
    out.worn        = Report(a, false);
    out.wornVisible = Report(a, true);
    bool on[6];
    for (int c = 0; c < 6; ++c) on[c] = DeviceEquip::IsRestrained(a, c);
    out.gag   = on[0] ? RestraintLabel(a, 0) : std::string();
    out.blind = on[1] ? RestraintLabel(a, 1) : std::string();
    out.arms  = on[2] ? RestraintLabel(a, 2) : std::string();
    out.legs  = on[3] ? RestraintLabel(a, 3) : std::string();
    out.all   = on[4] ? RestraintLabel(a, 4) : std::string();
    out.deaf  = on[5] ? RestraintLabel(a, 5) : std::string();
    out.after        = AftermathState(a, /*advance=*/false);
    out.afterVisible = AftermathVisible(a, /*forFile=*/true);
    bool anyRelease = false, tracked = false;
    {
        std::scoped_lock lk(g_mtx);
        out.ungag       = ReleaseCount(afid, 0, on[0], now);
        out.unblind     = ReleaseCount(afid, 1, on[1], now);
        out.unboundArms = ReleaseCount(afid, 2, on[2], now);
        out.unboundLegs = ReleaseCount(afid, 3, on[3], now);
        out.unboundAll  = ReleaseCount(afid, 4, on[4], now);
        out.undeaf      = ReleaseCount(afid, 5, on[5], now);
        auto r = g_release.find(afid);
        if (r != g_release.end()) {
            for (int c = 0; c < 6; ++c) if (r->second.was[c] || r->second.at[c] > 0.0) anyRelease = true;
            if (!anyRelease) g_release.erase(r);   // nothing on, nothing counting: forget her
        }
        tracked = g_on.find(afid) != g_on.end();
    }
    // An actor with nothing on, nothing left behind and no countdown running drops out of the file.
    return tracked || !out.after.empty() || anyRelease;
}

void PollStates()
{
    DeviceEquip::TickHolds();   // the undress hold's 4 s timeout rides this 3 s poll
    auto* task = SKSE::GetTaskInterface();
    if (!task) return;
    task->AddTask([]() { DrainPendingOff(); });   // held removals (2026-09-13), an SKSE task (BSJobs worker)
    std::vector<std::uint32_t> fids;
    // ★ 1.3.3 (review of 2026-09-14): an actor whose struggle took EVERY tracked device off has left
    // g_on (NoteOff erases her with her last device), so she was never ticked again - no END, no
    // narration, no deferred kneel, and a stale record that fired hours later if she was re-fitted.
    // Such actors get a StruggleTick of their own until the END erases the record.
    std::vector<std::uint32_t> struggleOnly;
    {
        std::scoped_lock lk(g_mtx);
        fids.reserve(g_on.size());
        for (auto& [afid, devs] : g_on) fids.push_back(afid);
        for (auto& [afid, s] : g_struggle)
            if (g_on.find(afid) == g_on.end()) struggleOnly.push_back(afid);
    }
    // ★ 1.3.0: the prompt state file - rebuilt every poll, written only if its text changed. Queued
    // before the early return: an actor with nothing on but a release countdown or an after-state
    // still running needs her entry refreshed (the wear-clock and after-state text move with time).
    task->AddTask([]() { PromptState::Refresh("poll"); });
    if (!struggleOnly.empty()) {
        task->AddTask([struggleOnly = std::move(struggleOnly)]() {
            for (auto fid : struggleOnly) {
                auto* f = RE::TESForm::LookupByID(fid);
                auto* a = f ? f->As<RE::Actor>() : nullptr;
                if (a) { StruggleTick(a); continue; }
                // The form is gone (a deleted reference): nothing can ever end this record.
                std::scoped_lock lk(g_mtx);
                g_struggle.erase(fid);
            }
        });
    }
    if (fids.empty()) return;
    task->AddTask([fids = std::move(fids)]() {
        for (auto fid : fids) {
            auto* f = RE::TESForm::LookupByID(fid);
            auto* a = f ? f->As<RE::Actor>() : nullptr;
            // ReEvaluate does its own player / dead / disabled / child rejects.
            if (a) ReEvaluate(a);
            // The blind module: melee-only enforcement + the trip roll. Its
            // own rejects inside; erases its state when the blindfold is off.
            if (a) BlindTick(a);
            // The gag module: no words, no spell. Own rejects inside; holds no
            // state, so there is nothing to erase when the gag comes off.
            if (a) GagTick(a);
            // The struggle (1.2.9): Better NPC Support's escape attempts, told and resolved.
            if (a) StruggleTick(a);
        }
    });
}

// The pushed inventory-half name for a class, exposed for DeviceEquip's
// registry (2026-08-30): the arms row needs to tell a RESTRICTIVE glove from a
// plain latex one, and only the name says - on the inventory half, which only
// this file's store has.
std::string PushedName(std::uint32_t actorFid, const char* cls)
{
    return NameForClass(actorFid, cls);
}

// ★ The deaf row's real question, exposed for DeviceEquip's registry
// (2026-09-08). Asked per DEVICE, not per actor, so wearing a mis-tagged
// blindfold AND a genuine hood still reads deaf - the hood answers for itself.
// Only when EVERY hood-keyworded device she has on turns out to be a blindfold
// does the category go quiet.
// ⚠ Must live OUT here, not in the anonymous namespace above: DeviceEquip links
// against it, and an internal-linkage definition is the LNK2019 this cost.
// ★★ THE EXTREME HOOD FAMILY (the user, 2026-09-12, reviewing the visualiser).
// Twelve DD records, six colour/material pairs, each pair two forms of ONE hood:
//   "<colour> <mat> Extreme Hood"         957/959/961/963/965/967
//   "<colour> <mat> Extreme Hood (Open)"  958/960/962/964/966/968
// The user's ruling: the (Open) form has holes for the eyes - SHE CAN SEE, Mid
// layer. The closed form "is just a blindfold, not hearing or speech muffled".
// ⛔ WHY THIS IS NAME-KEYED, NOT MESH-KEYED: both forms render the SAME eye-piece
// mesh (hoodExtremeBlind<mat>01) and both carry zad_DeviousBlindfold + Hood, so
// neither the keywords nor WornMeshIsBlindfold can tell them apart. Only the
// "(Open)" in the inventory-half name does. That is why "Hood" was added to
// VRTEDD_Controller.ReconcileNames the same day - without it, a hood worn before
// the session started has no name here and falls through to the plain-hood rules.
// ⚠ They are NOT a combo, whatever the numbering suggests: both forms occupy
// slots 31 and 42, so the engine will not let one be worn over the other.
bool IsExtremeHood(const char* label)
{
    return label && ContainsNoCase(label, "Extreme Hood");
}
bool IsExtremeHoodOpen(const char* label)
{
    return IsExtremeHood(label) && ContainsNoCase(label, "(Open)");
}

bool WearsRealHood(RE::Actor* a)
{
    if (!a) return false;
    const std::uint32_t afid = a->GetFormID();
    bool real = false;
    ForEachWorn(a, [&](RE::TESObjectARMO* w) {
        if (!HasKw(w, "zad_DeviousHood")) return true;
        if (WornMeshIsBlindfold(w)) return true;                     // a mis-tagged blindfold
        // ★ 2026-09-12: the CLOSED Extreme Hood is a blindfold too, by the user's
        // ruling, but its mesh does not say so - ask its name.
        char cls[64] = {};
        ClassOf(w, cls, sizeof cls);
        const std::string nm = NameForDevice(afid, w, cls);
        if (IsExtremeHood(nm.c_str()) && !IsExtremeHoodOpen(nm.c_str())) return true;
        real = true;
        return false;                                                 // stop early
    });
    return real;
}

// ★ 2026-09-12: is she wearing a boot that genuinely HOBBLES her? Only a FULL-grade
// boot enters the legs restraint state. The state's prompt says "moves at a slow
// shuffle and stays at that pace" and forbids "keeping pace on foot"; the user's
// leg-grade sentence says "pace NOT reduced", which contradicts it outright, and
// socks and plain Oil Boots are not a restraint at all. Before this every
// zad_DeviousBoots counted, so a latex sock told the LLM she was hobbled.
bool WearsFullGradeBoot(RE::Actor* a)
{
    if (!a) return false;
    const std::uint32_t afid = a->GetFormID();
    bool full = false;
    ForEachWorn(a, [&](RE::TESObjectARMO* w) {
        if (!HasKw(w, "zad_DeviousBoots")) return true;
        char cls[64] = {};
        ClassOf(w, cls, sizeof cls);
        const std::string nm = NameForDevice(afid, w, cls);
        if (BootGradeOf(cls, nm.c_str()) != BootGrade::Full) return true;
        full = true;
        return false;
    });
    return full;
}

// ★ 2026-09-12: does anything she wears actually block her SIGHT? The blind state
// used to count every zad_DeviousBlindfold, but the Extreme Hood (Open) carries
// that keyword while leaving the eyes clear (the user: "hole for the eye, can
// see"). Without this the worn line and the blind STATE would contradict each
// other on the same eyes - the defect class this AddOn keeps paying for.
bool WearsSightBlocker(RE::Actor* a)
{
    if (!a) return false;
    const std::uint32_t afid = a->GetFormID();
    bool blocked = false;
    ForEachWorn(a, [&](RE::TESObjectARMO* w) {
        if (!HasKw(w, "zad_DeviousBlindfold")) return true;
        char cls[64] = {};
        ClassOf(w, cls, sizeof cls);
        const std::string nm = NameForDevice(afid, w, cls);
        if (IsExtremeHoodOpen(nm.c_str())) return true;              // eye holes
        blocked = true;
        return false;
    });
    return blocked;
}

std::vector<std::uint32_t> TrackedActors()
{
    std::vector<std::uint32_t> out;
    std::scoped_lock lk(g_mtx);
    out.reserve(g_on.size());
    for (auto& [afid, devs] : g_on) out.push_back(afid);
    return out;
}

// Called by DeviceEquip just BEFORE one of our own gestures equips or removes
// a device, so the engine sink does not narrate the same act a second time.
// See the claim ledger above for why it is keyed on the ACTOR rather than the
// device, and why it is a window rather than a consumable token.
void ClaimGesture(std::uint32_t actorFid)
{
    if (!actorFid) return;
    std::scoped_lock lk(g_mtx);        // render thread; the sink reads this
    g_gestureClaim[actorFid] = NowSeconds() + g_sc.cdClaim;
}

void NoteArousal(RE::Actor* a, int value)
{
    if (!a) return;
    std::scoped_lock lk(g_mtx);        // VM thread; the sinks read this
    g_arousal[a->GetFormID()] = value;
}

void NoteDeviceName(RE::Actor* a, const char* cls, const char* name)
{
    if (!a || !cls || !cls[0] || !name || !name[0]) return;
    {
        std::scoped_lock lk(g_mtx);
        g_names[a->GetFormID()][cls] = name;
    }
    logger::info("[WORN] name 0x{:08X} {} = '{}'", a->GetFormID(), cls, name);
    PromptState::RequestRefresh("name");                     // 1.3.0: the name arrives ~4 s after the equip
}

// ★ THE EXACT-FORMID NAME PUSH (2026-09-13, D4/D9/D10). The Controller's key sweep already walks
// every worn inventory device and resolves its rendered half (zadNativeFunctions.GetRenderDevice);
// the name rides on that push, so EVERY class is named within ~30 s of a load - not the 13 the
// class probe covers - and it is filed under the record the readers are handed, never a token.
void NoteDeviceRecord(RE::Actor* a, std::uint32_t renderedFid, const char* name)
{
    if (!a || !renderedFid || !name || !name[0]) return;
    bool fresh = false;
    {
        std::scoped_lock lk(g_mtx);
        auto& slot = g_devNames[a->GetFormID()][renderedFid];
        fresh = (slot != name);
        slot  = name;
    }
    if (fresh) logger::info("[WORN] name 0x{:08X} device 0x{:08X} = '{}'", a->GetFormID(), renderedFid, name);
    if (fresh) PromptState::RequestRefresh("name");
}

// ═══════════════════════════════════════════════════════════════════════════
// THE KEY REGISTRY AND THE REMOVAL RULE (2026-09-08)
//
// The user, after finding a key in inventory did nothing: "if i have the key for
// that device in my inventory, i should be able to remove the device... it's more
// that the AddOn is the one telling PPB if a device can be removed or not."
//
// ⛔ WHAT WAS ACTUALLY HAPPENING. Nothing here decided removal at all - the
// consumer did, locally, with one keyword test:
//     if fromFinger == 0.0 && rendered.HasKeyword(libs.zad_Lockable)
//         Debug.Notification("You are missing the key to remove this device.")
// It never looked in an inventory. The message was printed whether or not the
// player held the key, which is exactly what the user hit.
//
// ★ DD'S REAL MODEL is per device, not per keyword: the INVENTORY half's
// zadEquipScript carries `deviceKey` (:32) and `NumberOfKeysNeeded` (:35), and
// DD's own unlock does `PlayerRef.GetItemCount(DeviceKey) >= NumberOfKeysNeeded`
// (:570-577). A blanket "has any DD key" would open a chastity belt with a
// restraints key, so we honour the per-device pair.
//
// ⚠ C++ CANNOT READ A PAPYRUS PROPERTY, and at removal time we are handed the
// RENDERED half, which carries neither the script nor a name. So the controller
// pushes the pair in from OnDeviceEquipped - the one place both halves are in
// hand - keyed (actor, class), exactly like NoteDeviceName.
//
// ⚠ FAIL-SAFE DIRECTION: an unknown key means REFUSE, not allow. A device that
// will not come off is a nuisance; one that falls off a locked captive because
// we had no record is a broken scene.
// ═══════════════════════════════════════════════════════════════════════════
// ⛔ KEYED ON THE RENDERED FORMID SINCE 2026-09-10, not on the device class.
// The class key needed TWO resolvers to agree: the push used the class DD
// ANNOUNCED (DDI_DeviceEquipped's akKeyword), the lookup used our own ClassOf
// over the rendered half's keywords. On a multi-keyword device those can differ,
// and the key is filed under one class and looked for under another - the "two
// detectors for one thing will drift" rule this project keeps paying for. The
// removal call is handed one exact rendered FormID; looking up by that FormID
// cannot disagree with anything.
// ⚠ Still per ACTOR: a rendered half can back several inventory devices
// (shared_with_other_devices, 8 records) with different keys, and one actor
// wears only one of them at a time.
struct KeyReq { std::uint32_t keyFid = 0; int needed = 1; };
std::unordered_map<std::uint32_t, std::unordered_map<std::uint32_t, KeyReq>> g_keys;

// ★ THE KEYED APPROVALS (2026-09-10). Devious Devices spends the key AFTER an NPC unlock
// (zadEquipScript.psc OnContainerChanged :460-463: DestroyKey, or the MCM "Consume Keys"
// with a zad_NonUniqueKey key). A VR hand unlock goes through PPB's UnlockDeviceByKeyword,
// which skips that - one Simple Skeleton Key opened the same gag twice in the key test.
// An approval is not a removal (PPB's Papyrus half can still refuse), so it is only
// RECORDED here; the AddOn's bridge spends it when PPB_GestureUnlocked confirms the
// unlock, and hands DD's rule to VRTEDD_Keys. One per actor - one pair of hands, one
// pull. A finger extraction and a keyless device never record one, so they never
// consume a key.
struct KeyedAllow { std::uint32_t renderedFid = 0; std::uint32_t keyFid = 0; int needed = 1; double at = 0.0; };
std::unordered_map<std::uint32_t, KeyedAllow> g_keyedAllow;

bool TakeKeyedAllow(std::uint32_t actorFid, double maxAgeS, std::uint32_t& renderedFid,
                    std::uint32_t& keyFid, int& needed)
{
    std::scoped_lock lk(g_mtx);
    auto it = g_keyedAllow.find(actorFid);
    if (it == g_keyedAllow.end()) return false;
    const KeyedAllow ka = it->second;
    g_keyedAllow.erase(it);                           // one-shot: an approval is spent once
    if (NowSeconds() - ka.at > maxAgeS) return false; // stale: that pull never completed
    renderedFid = ka.renderedFid;
    keyFid      = ka.keyFid;
    needed      = ka.needed;
    return true;
}

// The LIVE display name of a key, for messages and receipts. ⚠ Live, never the
// editorID: Devious Lore renames zad_RestraintsKey -> "Simple Skeleton Key" and
// zad_ChastityKey -> "Ornate Skeleton Key", and a message naming "Restraints Key"
// sends the player hunting for an item that does not exist under that name.
// PPB follow-up #3 §G4.4: it cost their user several sessions of believing the
// key had never been found.
static std::string KeyNameOf(std::uint32_t keyFid)
{
    if (!keyFid) return "";
    auto* f = RE::TESForm::LookupByID(keyFid);
    const char* n = f ? f->GetName() : nullptr;
    return (n && n[0]) ? std::string(n) : std::string("the proper key");
}

void NoteDeviceKey(RE::Actor* a, std::uint32_t renderedFid, std::uint32_t keyFid, int needed)
{
    if (!a || !renderedFid) return;
    const int n = needed < 1 ? 1 : needed;
    bool changed = false;
    {
        std::scoped_lock lk(g_mtx);
        auto& slot = g_keys[a->GetFormID()];
        auto it = slot.find(renderedFid);
        if (it == slot.end() || it->second.keyFid != keyFid || it->second.needed != n) {
            slot[renderedFid] = KeyReq{ keyFid, n };
            changed = true;
        }
    }
    // ⚠ LOG ONLY A NEW OR CHANGED RECORD. ReconcileKeys re-pushes every worn
    // device every ~30 s; logging each push would write ~1,200 lines an hour for
    // two actors in five devices and bury the one line PPB pairs against.
    if (!changed) return;
    if (keyFid)
        logger::info("[WORN] key 0x{:08X} device 0x{:08X} = 0x{:08X} '{}' x{}",
                     a->GetFormID(), renderedFid, keyFid, KeyNameOf(keyFid), n);
    else
        logger::info("[WORN] key 0x{:08X} device 0x{:08X} = none (DD requires no key)",
                     a->GetFormID(), renderedFid);
}

// ★ 1.3.3 - the struggle's key route (declared above the struggle block; see FreedHow). Both mirror
// Better NPC Support's TryToEscapeDevice (ddnf_npctracker_npc.psc :2126-2215), the only reader that
// matters: it UNLOCKS when the hands are free, the device is no quest device, and she holds DD's key
// count (deviceKey None counts as held); otherwise it picks, struggles or cuts.
// ⛔ EscapeRouteOf takes g_mtx (the key store, PairedInventory) - never call it with the lock held.
// HandsBlockedForKeys reads worn keywords only and takes no lock.
bool HandsBlockedForKeys(RE::Actor* a)
{
    if (!a) return true;
    // BNSDD: npc.GetItemCount(zad_DeviousBondageMittens) > 0 || GetItemCount(zad_DeviousStraitJacket) > 0
    // - counted on what she carries; a worn rendered half is what carries those keywords.
    bool blocked = false;
    ForEachWorn(a, [&](RE::TESObjectARMO* w) {
        if (HasKw(w, "zad_DeviousBondageMittens") || HasKw(w, "zad_DeviousStraitJacket")) {
            blocked = true;
            return false;
        }
        return true;
    });
    return blocked;
}

FreedHow EscapeRouteOf(RE::Actor* a, RE::TESObjectARMO* w, bool handsBlocked)
{
    if (!a || !w) return FreedHow::Forced;
    // BNSDD deviceIsQuestDevice: zad_BlockGeneric or zad_QuestItem on either half -> no unlock chance.
    auto* inv = PairedInventory(w);   // takes g_mtx
    if (HasKw(w, "zad_QuestItem") || HasKw(w, "zad_BlockGeneric") ||
        (inv && (HasKw(inv, "zad_QuestItem") || HasKw(inv, "zad_BlockGeneric"))))
        return FreedHow::Forced;
    if (handsBlocked) return FreedHow::Forced;
    KeyReq req{};
    bool known = false;
    {
        std::scoped_lock lk(g_mtx);
        if (auto ia = g_keys.find(a->GetFormID()); ia != g_keys.end()) {
            if (auto id = ia->second.find(w->GetFormID()); id != ia->second.end()) {
                req   = id->second;
                known = true;
            }
        }
    }
    // No key record pushed for this device yet: say only what can be proven - that it came off.
    if (!known) return FreedHow::Forced;
    if (!req.keyFid) return FreedHow::TakenOff;               // DD wants no key for it
    auto* kf = RE::TESForm::LookupByID(req.keyFid);
    auto* kb = kf ? kf->As<RE::TESBoundObject>() : nullptr;
    if (!kb) return FreedHow::Forced;
    // ⚠ GetItemCount is PlayerCharacter-only in CommonLibVR; every actor has GetInventoryCounts.
    const auto counts = a->GetInventoryCounts([kb](RE::TESBoundObject& o) { return &o == kb; });
    const auto c = counts.find(kb);
    const int held = (c != counts.end()) ? static_cast<int>(c->second) : 0;
    return held >= req.needed ? FreedHow::Unlocked : FreedHow::Forced;
}

// ★ THE CORE, added at build 3 (2026-09-08) on PPB's ask. It answers a STATE,
// not just a sentence, because "blocked" and "we have no record" are different
// facts and only the caller can decide what to do about the second.
//
// PPB's argument for the split, and it is the right one: an UNKNOWN device
// reported as BLOCKED gets refused with OUR message about a key the player may
// well be holding - which is exactly how the message we came here to fix
// started lying in the first place. On UNKNOWN they fall back to their own
// local rule, so the outcome is today's behaviour with today's wording, and
// this gate stops being blamed for a verdict it had no data to reach.
//
// ⚠ NOT a sentinel from RemovalBlockedBy. Its published contract says the
// return is "empty, non-null", and narrowing an existing method's return domain
// is a silent break for anyone who trusted it - the same append-only rule that
// keeps slot 0 as GetBuildNumber forever.
//   0 = ALLOW · 1 = BLOCKED · 2 = UNKNOWN
static int RemovalDecide(RE::Actor* wearer, RE::TESObjectARMO* device, bool byHand,
                         std::string& reason)
{
    reason.clear();
    if (!wearer || !device) { reason = "This device stays on."; return 1; }

    // ── 1. the absolute refusals, before anything else ──────────────────────
    // DD marks these as "no key opens this, ever". They outrank the key check.
    if (HasKw(device, "zad_QuestItem")) {
        reason = "This device is bound to a quest and stays on until that quest releases it.";
        return 1;
    }
    if (HasKw(device, "zad_BlockGeneric")) {
        reason = "This device is sealed against ordinary keys and stays on until it is released another way.";
        return 1;
    }
    // ★ D7 (2026-09-13): DD's own UnlockDevice tests these two flags on the INVENTORY half
    // (zadLibs :409-413, "mods put them there"), and four shipped devices carry them there ONLY
    // (#1119 Lively Rope Harness, #1126 / #1128 / #1129 Strong Leather). This gate answered
    // ALLOWED off the rendered half, DD then refused, and the player saw "you don't have the
    // key". Both halves are tested now, through the pair map.
    if (auto* inv = PairedInventory(device)) {
        if (HasKw(inv, "zad_QuestItem")) {
            reason = "This device is bound to a quest and stays on until that quest releases it.";
            return 1;
        }
        if (HasKw(inv, "zad_BlockGeneric")) {
            reason = "This device is sealed against ordinary keys and stays on until it is released another way.";
            return 1;
        }
    }
    // ★ F2 (the user, handover 36: rows 1507/1508 are "magical, CANNOT be removed"). Diary of
    // Mine's leash collars: the description line ("the enchantment worked into it is made to keep
    // a slave from straying") has shipped since round 9, the refusal never had. Enforcing wording.
    if (HasKw(device, "DOMWornCollarLeash") || HasKw(device, "DOMLeash")) {
        reason = "This collar is sealed by the enchantment worked into it and stays on until whoever put it on releases it.";
        return 1;
    }

    // ── 1b. the remover's own hands (2026-09-10) ────────────────────────────
    // Devious Devices refuses the player unlocking ANY device on an NPC while the player
    // wears heavy bondage - zadEquipScript.psc OnContainerChanged, before the belt and the
    // key are even looked at ("Your wrist bindings prevent you from removing..."). Mirrored
    // so the hand path agrees with DD's menu, the finger path included, as in DD.
    if (auto* pc = RE::PlayerCharacter::GetSingleton();
        pc && wearer != pc && WornByKw(pc, "zad_DeviousHeavyBondage")) {
        reason = "This device can only be removed with free hands, and will stay on while the wrists are bound.";
        return 1;
    }

    // ── 2. the layer model: is something worn OVER it? ──────────────────────
    // The same question BlockedBy answers for equipping, asked backwards. A plug
    // under a chastity belt does not come out until the belt does, whichever
    // route is used - so this runs for the finger path too.
    const std::string over = ClothingBlockOn(wearer, device);
    if (!over.empty()) {
        reason = "It is held in place by " + over + ".";
        return 1;
    }

    // ── 3. the lock, and the KEY ────────────────────────────────────────────
    // ⚠ A FINGER EXTRACTION IS EXEMPT FROM THE LOCK, by the user's 2026-08-24
    // ruling ("you can lock a plug in the orifice, you need a chastity belt on
    // top"). Step 2 above is that path's real barrier and it has already run.
    if (byHand) return 0;                                        // exempt - allow
    if (!HasKw(device, "zad_Lockable")) return 0;                // not locked at all

    // ★ LOOKED UP BY THE EXACT RENDERED FORMID (2026-09-10) - see g_keys.
    const std::uint32_t devFid = device->GetFormID();
    KeyReq req;
    bool known = false;
    {
        std::scoped_lock lk(g_mtx);
        auto ia = g_keys.find(wearer->GetFormID());
        if (ia != g_keys.end()) {
            auto id = ia->second.find(devFid);
            if (id != ia->second.end()) { req = id->second; known = true; }
        }
    }
    if (!known) {
        // ⚠ NO RECORD YET - this device's key has not been read. An absence of
        // evidence, not a refusal: state 2 tells a build-3 caller to apply its
        // own rule. Since 2026-09-10 the controller's sweep reads every worn
        // device within ~30 s, so this is a short window after a load rather than
        // a permanent state. The sentence exists only for build-2 callers, who
        // can read nothing but RemovalBlockedBy - and it is the user's own
        // enforcing wording.
        reason = "This device can only be removed with the proper key and will stay on otherwise.";
        return 2;
    }
    if (req.keyFid == 0) {
        // ⛔ CORRECTED 2026-09-10. Build 3 called this BLOCKED ("no key opens this
        // one") - the OPPOSITE of Devious Devices' own rule, and stricter than
        // DD's menu. zadEquipScript.RemoveDeviceWithKey (:569) wraps the entire
        // key check in `If DeviceKey` with no else: a lockable device with no key
        // form needs NO key, and DD removes it. The genuine "no key ever" devices
        // are zad_QuestItem / zad_BlockGeneric, and step 1 has already refused
        // those. ★ The gate should agree with DD's menu; "opens in the menu but
        // not by hand" reads as a bug even when each half is self-consistent.
        logger::info("[WORN] REMOVE ALLOWED 0x{:08X} device 0x{:08X} - DD requires no key",
                     wearer->GetFormID(), devFid);
        return 0;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* keyForm = RE::TESForm::LookupByID(req.keyFid);
    auto* keyBound = keyForm ? keyForm->As<RE::TESBoundObject>() : nullptr;
    const std::int32_t have =
        (player && keyBound) ? player->GetItemCount(keyBound) : 0;
    std::string keyName = KeyNameOf(req.keyFid);
    if (keyName.rfind("the ", 0) == 0) keyName.erase(0, 4);   // KeyNameOf's fallback reads "the proper key"
    if (have < req.needed) {
        // ★ ENFORCING, NOT NEGATING - the user's standing rule, and their own
        // example was this exact message: "This device can only be removed with
        // the proper key and will stay on otherwise." With the LIVE key name, so
        // the player looks for "Simple Skeleton Key", which exists in their game,
        // not "Restraints Key", which does not (PPB follow-up #3 §G4.4).
        reason = (req.needed > 1)
            ? "This device can only be removed with " + std::to_string(req.needed) +
              " " + keyName + "s, and will stay on otherwise."
            : "This device can only be removed with the " + keyName +
              ", and will stay on otherwise.";
        return 1;
    }
    logger::info("[WORN] REMOVE ALLOWED 0x{:08X} device 0x{:08X} - '{}' x{} held ({})",
                 wearer->GetFormID(), devFid, keyName, req.needed, have);
    {
        std::scoped_lock lk(g_mtx);
        g_keyedAllow[wearer->GetFormID()] = KeyedAllow{ devFid, req.keyFid, req.needed, NowSeconds() };
    }
    return 0;
}

// ★ EVERY VERDICT IS LOGGED AND ANNOUNCED (2026-09-10). The user: "log both" - a refusal
// pairs with PPB's own refusal line exactly as an approval already did (PPB follow-up #4
// §K3). PPB asks RemovalStateOf and then, on a refusal, RemovalBlockedBy - one pull, two
// calls - so the same actor/device/state inside 2 s is the same verdict and logs once.
// Every non-finger verdict also goes to the AddOn's bridge, which is holding that pull's
// narration until it knows whether anything actually came off.
int RemovalCheck(RE::Actor* wearer, RE::TESObjectARMO* device, bool byHand,
                 std::string& reason)
{
    const int st = RemovalDecide(wearer, device, byHand, reason);
    const std::uint32_t afid = wearer ? wearer->GetFormID() : 0;
    const std::uint32_t dfid = device ? device->GetFormID() : 0;
    if (st != 0) {
        static std::mutex    s_logMtx;
        static std::uint32_t s_a = 0, s_d = 0;
        static int           s_st = -1;
        static double        s_at = -100.0;
        const double now = NowSeconds();
        bool fresh = true;
        {
            std::scoped_lock lk(s_logMtx);
            fresh = !(s_a == afid && s_d == dfid && s_st == st && now - s_at < 2.0);
            s_a = afid; s_d = dfid; s_st = st; s_at = now;
        }
        if (fresh) {
            if (st == 1)
                logger::info("[WORN] REMOVE REFUSED 0x{:08X} device 0x{:08X} - {}", afid, dfid, reason);
            else
                logger::info("[WORN] REMOVE UNKNOWN 0x{:08X} device 0x{:08X} - no key record yet; "
                             "the caller applies its own lock rule", afid, dfid);
        }
    }
    if (!byHand && afid) DeviceEquip::OnRemovalVerdict(afid, st);
    return st;
}

// The two published faces of RemovalCheck. Both keep their build-2 contract.
const char* RemovalBlockedBy(RE::Actor* wearer, RE::TESObjectARMO* device, bool byHand)
{
    static std::string reason;
    // ⚠ UNKNOWN still reports a SENTENCE here, deliberately: a build-2 consumer
    // can only read this method, and for it "no record" must keep behaving as
    // it did on the day it shipped. Build 3 callers ask RemovalStateOf instead
    // and get the distinction.
    RemovalCheck(wearer, device, byHand, reason);
    return reason.c_str();
}

int RemovalStateOf(RE::Actor* wearer, RE::TESObjectARMO* device, bool byHand)
{
    std::string ignored;
    return RemovalCheck(wearer, device, byHand, ignored);
}

// ── 1.3.5: DD's orgasm SIGNAL, the pose's clip length, the credit guards ─────────────────────────
RE::TESFaction* AnimatingFaction()
{
    static RE::TESFaction* f = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (auto* raw = DdForm(0x029567, kIntegration)) f = raw->As<RE::TESFaction>();   // zadAnimatingFaction
        if (!f) logger::info("[WORN] zadAnimatingFaction did not resolve - DD's floor clip cannot be held off.");
    }
    return f;
}

// The credit guards, ONE answer for DD's orgasm signal and its orgasm event (a climax the signal posed must
// be the climax the event credits). 0 = creditable; 1 = not ours, say nothing; 2 = only keyword-less devices;
// 3 = no device running.
int ClimaxGuard(RE::Actor* a)
{
    if (!a) return 1;
    // ★ 1.3.3 THE PLAYER IS NOT OURS HERE (the user, 2026-09-14, asked what DD SN should do when the
    // player climaxes from a DD vibration: "Nothing"). No credit, no narration, no ClimaxPose - which
    // would have cut the player's vibration short (StopVibrating), played DD's clip on the VR body and
    // forced player arousal to 50. Found by the 1.3.2 review: this was the one climax path with no
    // player reject (ReEvaluate, StruggleTick and PromptEntry all have one), and DD raises
    // DeviceActorOrgasmEx for the player too. ClimaxPose carries its own guard as well.
    if (a == RE::PlayerCharacter::GetSingleton()) return 1;
    {
        std::scoped_lock lk(g_mtx);
        if (g_on.find(a->GetFormID()) == g_on.end()) return 1;   // not wearing anything of ours
    }
    // ★ JOB B (2026-09-13): a keyword-less device earns NO climax - the user on the Pama set: "awareness
    // ONLY - no animation, no climax. Pama controls them." Worn alone, they must not turn DD's orgasm
    // event into a device climax. (Whether any scene orgasm should count is a separate, open question -
    // the integration review's D1.)
    bool framework = false;
    ForEachWorn(a, [&](RE::TESObjectARMO* w) {
        if (IsCoveredDevice(w) && !KeywordlessOf(w)) { framework = true; return false; }
        return true;
    });
    if (!framework) return 2;
    // ★ D1 (the user, 2026-09-13: "b"): a climax is the DEVICES' only while a device is actually
    // RUNNING. DD NG's OstimDDCompatibility raises DeviceActorOrgasmEx for EVERY OStim orgasm and
    // the Controller does not filter the source (it cannot - DD's own paths use the same event),
    // so a scene orgasm on an NPC in a plain collar was told as "the devices brought her off",
    // with witnesses, an aftermath, and the kneel from the fourth. zadVibratorFaction membership
    // is DD's own "a vibration is running" flag: set at the top of VibrateEffect, cleared after
    // its loop, and ActorOrgasm fires INSIDE that loop (zadLibs.psc :2089 vs :2157) - so it is
    // true for our cast lane, DD's NPC loop and any third mod alike, and false for a scene.
    auto* fac = VibratorFaction();
    if (!fac || !a->IsInFaction(fac)) return 3;
    return 0;
}

// ★★ 1.3.5 DD'S ORGASM SIGNAL - THE POSE STARTS BEFORE DD'S FLOOR CLIP CAN (the user, 2026-09-14: "when she
// climax, there is a little jump in animation between each, like i can see she was about to go on her back
// as she goes 75% of the way, than come back and do the proper animation. Can we prevent that?").
// WHY THIS CAN WIN. zadLibs.ActorOrgasm (:1695) calls SendOrgasmEvent FIRST, and its first line is
// SendModEvent("DeviceActorOrgasm", <leveled base name>) (:1678). Only after the orgasm sound, the arousal
// framework's exposure write and its orgasm date does ActorOrgasm test `if !IsAnimating(akActor)` (:1705)
// before its floor clip - and IsAnimating is zadAnimatingFaction membership (:2329). SKSE's Form.SendModEvent
// is flagged NoWait (SKSE VR 2.0.12 source on disk: SkyrimVR/src/sksevr/skse64/PapyrusForm.cpp:274-281, :585)
// and hands the event to C++ sinks INSIDE the call, so this sink runs while DD's stack is still on that line,
// and DD then spends several frame-synced calls before its test. The task below puts her in
// zadAnimatingFaction: DD's test finds her "animating" and plays no floor clip (and skips its 20 s hold and
// EndThirdPersonAnimation - ClimaxPose owns the end instead). ⚠ Race-dependent; not yet seen in VR.
// ⚠ DeviceActorOrgasmEx (NoteClimax) carries the actor as a Form but is QUEUED to Papyrus - it arrives after
//   DD's test, which is why the 1.3.2-1.3.4 pose always landed on top of the floor clip.
// ⚠ This signal carries only a NAME. 1.3.6 (review): it poses ONLY when exactly one tracked, vibrating,
//   creditable actor has that name. Two same-named NPCs vibrating at once are left to NoteClimax, whose
//   event carries the actor itself (the pose then lands on DD's clip, the 1.3.4 look).
// ⚠ If the race is ever lost, DD's clip plays and ClimaxPose lands on it - the 1.3.4 look, not a break.

// ★ 1.3.6 WILL THE KNEEL PLAY? (the user, 2026-09-15, on the kneel over bound arms: "Kneel only if arms
// free"). The DDSN_ClimaxKneel submod carries DD NG's own 100303 exclusion list - the ten restraint keywords
// under which DD itself never plays the Belt Edged kneel - so a bound NPC keeps DD's device clip. This is the
// same list, read here so the climax narration says "knees" only when the kneel can actually play. It also
// needs OAR and DD NG's "DD to OAR/100303" clip on disk (checked once).
bool KneelAssetsPresent()
{
    static const bool present = [] {
        const bool oar = REX::W32::GetModuleHandleW(L"OpenAnimationReplacer.dll") != nullptr;
        std::error_code ec;
        const bool clip = std::filesystem::exists(
            "Data/meshes/actors/character/animations/OpenAnimationReplacer/DD to OAR/100303/DD/ZazHornyD.hkx", ec);
        logger::info("[WORN] climax kneel: Open Animation Replacer {}, DD NG's Belt Edged clip {}{}",
                     oar ? "loaded" : "NOT loaded", clip ? "found" : "NOT found",
                     (oar && clip) ? "" : " - from the 4th climax she stays on her feet (and is told so)");
        return oar && clip;
    }();
    return present;
}

// DD NG's own list: the ten keywords under which DD NG never plays its Belt Edged kneel (100303/config.json -
// resolved 2026-09-15: armbinder, elbow binder, elbow tie, straitjacket, front cuffs, yoke, BB yoke, pet suit,
// pony gear, hobble skirt). With one of these on, DD NG has its OWN clip for that device and keeps it.
bool DDSetOwnsClip(RE::Actor* a)
{
    static const std::vector<RE::BGSKeyword*> ddTen = [] {
        static const struct { std::uint32_t id; const char* plugin; } rows[] = {   // DD NG 100303/config.json
            { 0x062539, kIntegration }, { 0x060A46, kIntegration }, { 0x0866B8, kIntegration },
            { 0x05F4BA, kIntegration }, { 0x08A76C, kIntegration }, { 0x063AD9, kIntegration },
            { 0x02C531, "Devious Devices - Assets.esm" }, { 0x00CA3A, "Devious Devices - Assets.esm" },
            { 0x086C1D, kIntegration }, { 0x062538, kIntegration },
        };
        std::vector<RE::BGSKeyword*> out;
        for (const auto& r : rows)
            if (auto* f = DdForm(r.id, r.plugin))
                if (auto* kw = f->As<RE::BGSKeyword>()) out.push_back(kw);
        logger::info("[WORN] climax clip: {} of DD NG's 10 device keywords resolved", out.size());
        return out;
    }();
    if (!a) return false;
    bool owned = false;
    ForEachWorn(a, [&](RE::TESObjectARMO* w) {
        for (auto* kw : ddTen)
            if (w->HasKeyword(kw)) { owned = true; return false; }
        return true;
    });
    return owned;
}

// ★ 1.3.7 ZaZ AND DoM RESTRAINTS COUNT (the user, 2026-09-15: "make sure ... ZaZ and DoM device are recognized
// in our own plugin and working along fine with the DD device"). DD NG's list knows only DD keywords, so ZaZ
// irons, ZaZ rope ties and DoM's cuffs never exclude its Belt Edged kneel - a belted NPC in ZaZ irons knelt
// on EVERY edged clip through DD NG's own 100303. RestraintOf is this file's answer for every framework (35
// keywords across DD, ZaZ and DoM, all verified present in this load order 2026-09-15): hands in front,
// behind, up on a yoke or crossed in a jacket = arms bound.
bool ArmsBoundAnyFramework(RE::Actor* a)
{
    if (!a) return false;
    bool bound = false;
    ForEachWorn(a, [&](RE::TESObjectARMO* w) {
        const Restraint r = RestraintOf(w);
        if (r == Restraint::HandsFront || r == Restraint::HandsBack || r == Restraint::HandsHead ||
            r == Restraint::HandsCrossed) { bound = true; return false; }
        return true;
    });
    return bound;
}

// ★ 1.3.6/1.3.7 WHICH CLIP THE CLIMAX POSE ASKS OAR FOR. ClimaxPose puts her in DD SN's OWN marker faction
// (DDSN_ClimaxPoseFaction, DD SN Database.esp 0x801) at this RANK for the clip's length, and DD SN's two
// config-only OAR submods read that rank. ⛔ 1.3.6 used the vanilla actor value Variable03 - shared by every
// mod and the game's own AI, so not ours (the user, 2026-09-15: "make sure it's our own variable").
// ONE answer, used by the pose and by the climax narration:
//   4 = the Belt Edged kneel - 4th climax or later, arms free, OAR + DD NG's clip present  (DDSN_ClimaxKneel)
//   1 = DD's plain STANDING edged clip, even if she is belted                              (DDSN_EdgedStanding)
//       - climaxes 1-3 with arms free, and EVERY climax when ZaZ/DoM restraints bind her arms (no kneel
//         while bound - "Kneel only if arms free" - and DD NG's own belt kneel held off)
//   0 = DD NG's own clip for a DD device it has one for (armbinder, yoke, front cuffs, hobble, pet suit ...)
int ClimaxClipMode(RE::Actor* a, int n)
{
    if (!a || DDSetOwnsClip(a)) return 0;
    if (ArmsBoundAnyFramework(a)) return 1;
    return (n >= 4 && KneelAssetsPresent()) ? 4 : 1;
}

void OnDDOrgasmSignal(const std::string& name)
{
    std::vector<std::uint32_t> fids;
    {
        std::scoped_lock lk(g_mtx);
        fids.reserve(g_on.size());
        for (const auto& kv : g_on) fids.push_back(kv.first);
    }
    std::vector<RE::Actor*> matches;
    for (const auto fid : fids) {
        auto* a = RE::TESForm::LookupByID<RE::Actor>(fid);
        if (!a) continue;
        auto* base = a->GetActorBase();
        const char* bn = base ? base->GetName() : nullptr;
        const char* dn = a->GetDisplayFullName();
        if (!((bn && _stricmp(bn, name.c_str()) == 0) || (dn && _stricmp(dn, name.c_str()) == 0))) continue;
        if (ClimaxGuard(a) != 0) continue;              // NoteClimax logs the refusal when the event arrives
        matches.push_back(a);
    }
    if (matches.size() != 1) {
        if (matches.size() > 1)
            logger::info("[WORN] DD orgasm signal '{}' matches {} vibrating NPCs - left to DD's orgasm event",
                         name, matches.size());
        return;
    }
    auto* a = matches.front();
    const std::uint32_t fid = a->GetFormID();
    if (DeviceEquip::SceneOnNow()) return;             // D5: credited there, never posed
    if (a->IsDead() || a->IsDisabled() || !a->Is3DLoaded() || a->IsUnconscious()) return;
    // Seated or mounted, DD's IsAnimating is already true and it plays no clip - nothing to hold off.
    if (a->IsOnMount() || a->AsActorState()->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) return;
    int n = 0;
    {
        std::scoped_lock lk(g_mtx);
        auto& st = g_fx[fid];
        if (st.posing) return;                          // a pose is already playing on her
        st.posing      = true;
        st.posePending = true;
        n = st.climaxes + 1;                             // the number NoteClimax is about to credit
    }
    auto* anim = AnimatingFaction();
    const bool ddFirst = anim && a->IsInFaction(anim);
    const bool owned   = anim && !ddFirst;
    if (owned) a->AddToFaction(anim, 1);
    // ⛔ THREE Ints since 1.3.6: the third says the DLL added zadAnimatingFaction, so ClimaxPose removes it on
    // EVERY exit - including the ones where no pose can play (review of 1.3.5: it leaked, saved with the game).
    DispatchPapyrus("ClimaxPose", static_cast<std::int32_t>(fid), n, owned ? 1 : 0);
    logger::info("[WORN] DD orgasm signal on 0x{:08X} - pose #{} {}", fid, n,
                 anim ? (ddFirst ? "(DD was already animating her - its clip may show first)"
                                 : "(DD's floor clip held off)")
                      : "(zadAnimatingFaction missing - DD's clip will show first)");
}

class OrgasmSink : public RE::BSTEventSink<SKSE::ModCallbackEvent>
{
public:
    RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* ev,
                                          RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
    {
        // ⚠ Runs on a Papyrus VM thread, inside DD's SendModEvent call, while the event source holds its lock:
        // copy the name and hand it to the relay. ⛔ NEVER AddTask here (1.3.6, TaskRelay.h).
        if (!ev || !ev->eventName.c_str() || _stricmp(ev->eventName.c_str(), "DeviceActorOrgasm") != 0)
            return RE::BSEventNotifyControl::kContinue;
        // DD's own (zadQuest, 00F624 in Integration) - the sender SendModEvent stamps.
        if (!ev->sender || (ev->sender->GetFormID() & 0x00FFFFFF) != 0x00F624)
            return RE::BSEventNotifyControl::kContinue;
        if (ev->strArg.empty()) return RE::BSEventNotifyControl::kContinue;
        std::string name = ev->strArg.c_str();
        TaskRelay::Add([name]() { OnDDOrgasmSignal(name); });
        return RE::BSEventNotifyControl::kContinue;
    }
};
OrgasmSink g_orgasmSink;

// ★★ 1.3.9 WHO IS TALKING (the gag/hood cut and the spoken effect reactions). SkyrimNet sends, sender = the speaker:
//   SkyrimNet_SpeechStarted  / SkyrimNet_SpeechComplete  - her reply is being WRITTEN (several Started per reply)
//   SkyrimNet_AudioStarted   / SkyrimNet_AudioEnded      - one pair per SPOKEN line
// The Mouth Bridge's payload receipts (VR, 2026-09-15 17:36) show the strArg JSON: "speakerFormId" in DECIMAL
// (318889551 = 0x1301DE4F, the sender), "isNarration", and on AudioEnded "remainingQueueSize" (her next line).
// "Talking" = from the moment her reply starts being written until her last queued line has played. The windows
// in IsTalking only bridge the gaps BETWEEN those signals (text done -> voice median 2.0 s but up to 12.7 s, line ->
// next line ~0.3 s; measured 1.3.10);
// they are detection tolerances, not limits on anything she does.
// ⚠ The sink only records - it runs inside SkyrimNet's SendModEvent and must not AddTask (TaskRelay.h).
namespace {
struct TalkState {
    double preparingAt = -1.0;   // last SpeechStarted
    double completeAt  = -1.0;   // SpeechComplete after it
    bool   playing     = false;  // AudioStarted without its AudioEnded yet
    double endedAt     = -1.0;   // last AudioEnded
    int    remaining   = 0;      // remainingQueueSize at that AudioEnded
};
std::mutex                                   g_talkMtx;   // held only to touch the map, never while calling out
std::unordered_map<std::uint32_t, TalkState> g_talk;

long long JsonIntField(const char* json, const char* key)
{
    if (!json || !key) return -1;
    const std::string needle = std::string("\"") + key + "\":";
    const char* p = std::strstr(json, needle.c_str());
    if (!p) return -1;
    return std::strtoll(p + needle.size(), nullptr, 10);
}

class SpeechSink : public RE::BSTEventSink<SKSE::ModCallbackEvent>
{
public:
    RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* ev,
                                          RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
    {
        if (!ev || !ev->eventName.c_str()) return RE::BSEventNotifyControl::kContinue;
        const char* name = ev->eventName.c_str();
        if (_strnicmp(name, "SkyrimNet_", 10) != 0) return RE::BSEventNotifyControl::kContinue;
        const bool sStart = _stricmp(name, "SkyrimNet_SpeechStarted") == 0;
        const bool sDone  = _stricmp(name, "SkyrimNet_SpeechComplete") == 0;
        const bool aStart = _stricmp(name, "SkyrimNet_AudioStarted") == 0;
        const bool aEnd   = _stricmp(name, "SkyrimNet_AudioEnded") == 0;
        if (!sStart && !sDone && !aStart && !aEnd) return RE::BSEventNotifyControl::kContinue;
        const char* json = ev->strArg.c_str();
        if (json && std::strstr(json, "\"isNarration\":true")) return RE::BSEventNotifyControl::kContinue;
        std::uint32_t fid = 0;
        if (ev->sender && ev->sender->GetFormType() == RE::FormType::ActorCharacter)
            fid = ev->sender->GetFormID();
        else if (const long long id = JsonIntField(json, "speakerFormId"); id > 0)
            fid = static_cast<std::uint32_t>(id);
        if (!fid || fid == 0x14) return RE::BSEventNotifyControl::kContinue;   // no actor / the player
        const double now = NowSeconds();
        std::scoped_lock lk(g_talkMtx);
        auto& t = g_talk[fid];
        if (sStart)      { t.preparingAt = now; t.completeAt = -1.0; }
        else if (sDone)  { t.completeAt = now; }
        else if (aStart) { t.playing = true; t.preparingAt = -1.0; t.completeAt = -1.0; }
        else             { t.playing = false; t.endedAt = now;
                           const long long r = JsonIntField(json, "remainingQueueSize");
                           t.remaining = r < 0 ? 0 : static_cast<int>(r); }
        return RE::BSEventNotifyControl::kContinue;
    }
};
SpeechSink g_speechSink;
} // namespace

bool IsTalking(std::uint32_t fid)
{
    if (!fid) return false;
    const double now = NowSeconds();
    std::scoped_lock lk(g_talkMtx);
    const auto it = g_talk.find(fid);
    if (it == g_talk.end()) return false;
    const TalkState& t = it->second;
    if (t.playing) return true;                                                          // her voice is playing
    if (t.remaining > 0 && t.endedAt >= 0.0 && now - t.endedAt < 2.0) return true;        // between two of her lines
    if (t.preparingAt >= 0.0) {
        if (t.completeAt < 0.0) return now - t.preparingAt < 20.0;                        // her reply is being written
        // 1.3.10: 15 s, was 5. Measured over 94 replies (SkyrimNet logs 09-14/15): written -> voice median 2.0 s,
        // p90 5.5 s, max 12.7 s, 13 % >= 5 s - at 5 s one reply in eight read "not talking" while its voice was
        // still coming, and a gag in that gap did not cut her. A new SpeechStarted or AudioStarted ends this early.
        return now - t.completeAt < 15.0;                                                 // written, voice on its way
    }
    return false;
}

// Anything that fills or covers the mouth: every gag keyword the restraint table knows (DD, ZaZ, DoM) and every hood.
// DOMWornHood and zad_DeviousGagInflatable are absent from this load order (checked 2026-09-15) - they simply never match.
bool IsSpeechDevice(RE::TESObjectARMO* w)
{
    static const char* const kw[] = {
        "zad_DeviousGag", "zad_DeviousGagLarge", "zad_DeviousGagPanel", "zad_DeviousGagBit", "zad_DeviousGagRing",
        "zad_DeviousGagTape", "zad_DeviousGagInflatable", "zbfWornGag", "DOMWornGag",
        "zad_DeviousHood", "zbfWornHood", "DOMWornHood",
    };
    if (!w) return false;
    for (const char* k : kw)
        if (HasKw(w, k)) return true;
    return false;
}

void ArmOrgasmSink()
{
    if (auto* src = SKSE::GetModCallbackEventSource()) {
        src->AddEventSink(&g_orgasmSink);
        src->AddEventSink(&g_speechSink);   // 1.3.9: who is talking
        logger::info("[SPEECH] listening for SkyrimNet's speech and audio signals (gag/hood cut, spoken reactions)");
    } else
        logger::warn("[WORN] no mod-event source - DD's floor clip will show before the climax pose.");
}

namespace {
// Plain reads, no objects to unwind - the caller runs this under SEH. A torn or stale node fails the vtable
// or the sanity test and is skipped; nothing is ever written.
float ClipLeftRaw(RE::hkbBehaviorGraph* bg, const char* needle, std::uintptr_t clipVtbl, float* durOut)
{
    float best = -1.0f, bestLocal = 1.0e9f;
    auto* nodes = bg ? bg->activeNodes : nullptr;
    if (!nodes) return -1.0f;
    const auto count = nodes->size();
    if (count <= 0 || count > 4096) return -1.0f;
    for (std::int32_t i = 0; i < count; ++i) {
        RE::hkbNode* node = nodes->data()[i].nodeClone;
        if (!node || *reinterpret_cast<std::uintptr_t*>(node) != clipVtbl) continue;
        auto* clip = static_cast<RE::hkbClipGenerator*>(node);
        const char* nm = clip->animationName.c_str();
        if (!nm || !ContainsNoCase(nm, needle)) continue;
        auto* bind = clip->binding;
        auto* anim = bind ? bind->animation.get() : nullptr;
        if (!anim) continue;
        const float dur = anim->duration;
        if (!(dur > 0.2f && dur < 600.0f)) continue;
        const float local = clip->localTime;
        if (!(local >= 0.0f) || local > bestLocal) continue;   // the most recently started one
        float speed = clip->playbackSpeed;
        if (!(speed > 0.05f && speed < 20.0f)) speed = 1.0f;
        const float left = (dur - (local < dur ? local : dur)) / speed;
        bestLocal = local;
        best = left;
        if (durOut) *durOut = dur;
    }
    return best;
}

float ClipLeftGuarded(RE::hkbBehaviorGraph* bg, const char* needle, std::uintptr_t clipVtbl, float* durOut)
{
    __try {
        return ClipLeftRaw(bg, needle, clipVtbl, durOut);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -2.0f;
    }
}
} // namespace

// ★ 1.3.5 HOW LONG THE CLIP SHE IS PLAYING LASTS (the user: "tracking how long it last"). DD NG's animation
// replacer picks the clip at play time - her devices choose it (front cuffs 19.77 s, plain 10.00 s, the
// kneel 15.30 s, measured from the files) - so the length is read from the clip generator the graph is
// actually running: the newest active hkbClipGenerator whose animation name contains `needle`. OAR swaps
// only animationBindingIndex (ActiveClip.cpp:120), so the name stays DD's and `binding` is the replacement.
// Read under the graph manager's own update lock (as PLANCK does) and SEH-guarded. Seconds left, or < 0.
float ClipSecondsLeft(RE::Actor* a, const char* needle)
{
    if (!a || !needle || !*needle) return -1.0f;
    RE::BSTSmartPointer<RE::BSAnimationGraphManager> mgr;
    if (!a->GetAnimationGraphManager(mgr) || !mgr) return -1.0f;
    const std::uintptr_t vtbl = RE::VTABLE_hkbClipGenerator[0].address();
    float best = -1.0f, dur = 0.0f;
    {
        RE::BSSpinLockGuard guard(mgr->GetRuntimeData().updateLock);
        for (auto& g : mgr->graphs) {
            if (!g || !g->behaviorGraph) continue;
            float d = 0.0f;
            const float left = ClipLeftGuarded(g->behaviorGraph, needle, vtbl, &d);
            if (left == -2.0f) {
                logger::info("[WORN] clip read on 0x{:08X} faulted - using DD's own hold", a->GetFormID());
                continue;
            }
            if (left > best) { best = left; dur = d; }
        }
    }
    // 1.3.6: no per-call line - ClimaxPose now re-reads this every step of the hold and logs the first read
    // itself (VRTE_DDZaZ_Native.LogLine); only a read that finds nothing is worth a line here.
    if (best < 0.0f)
        logger::info("[WORN] clip '{}' on 0x{:08X}: no active clip found", needle, a->GetFormID());
    (void)dur;
    return best;
}

void NotePoseEnd(std::uint32_t fid)
{
    std::scoped_lock lk(g_mtx);
    const auto it = g_fx.find(fid);
    if (it == g_fx.end()) return;
    it->second.posing      = false;
    it->second.posePending = false;
}

// ★ 1.3.8 DD'S VIBRATION RAN OUT WITHOUT A FINISH (the user, 2026-09-15, after two of three vibrations at full
// arousal left Carmella "at 100" doing nothing: "Device goes off again"). DD picks its own length (5-20 s) and
// rolls the climax at 8 % a second from the 4th second, so a short vibration usually ends with nothing - and the
// 100 lane, disarmed for the climb since 1.3.4, then stayed shut until her arousal fell on its own. `came` is
// VibrateEffect's own answer: 1+ she came (the climax disarmed the lane already), -1 DD edged her (public: she
// stays wanting, as told in 1.3.4), 0 it ran and nothing happened -> the device goes off again on the next pass.
// -2 (refused: a scene, already vibrating, invalid) is left alone.
void NoteVibrateResult(std::uint32_t fid, int came)
{
    if (came != 0) return;
    std::scoped_lock lk(g_mtx);
    const auto it = g_fx.find(fid);
    if (it == g_fx.end()) return;
    it->second.fullArmed = true;
    logger::info("[WORN] vibration on 0x{:08X} ran out with no finish - at full arousal the device goes off again", fid);
}

// The exact climax hook. DD raises DeviceActorOrgasmEx from ActorOrgasm with the
// actor as a Form, on every orgasm path it owns, for NPCs as well as the player.
void NoteClimax(RE::Actor* a)
{
    if (!a) return;
    const std::uint32_t fid = a->GetFormID();
    int nClimax = 0;
    // 1.3.5: a climax DD's orgasm signal already checked and posed. ClimaxPose ends DD's vibration within a
    // few frames of starting, and this event is queued behind it - so "no device running" is expected here
    // for a signalled climax and must not refuse the credit the signal's own guard already granted.
    bool signalled = false;
    {
        std::scoped_lock lk(g_mtx);
        const auto it = g_fx.find(fid);
        signalled = it != g_fx.end() && it->second.posePending;
    }
    switch (ClimaxGuard(a)) {
    case 1: return;
    case 2:
        logger::info("[WORN] climax on 0x{:08X} not credited - only keyword-less devices worn", fid);
        return;
    case 3:
        if (signalled) break;
        logger::info("[WORN] climax on 0x{:08X} not credited - no device running (a scene, or another source)", fid);
        return;
    default: break;
    }
    bool posePending = false, posing = false, claim = false;
    {   // ⛔ THIS is the VM-thread writer the other g_fx fixes exist for: this
        // operator[] inserts, and it used to run with no lock at all.
        std::scoped_lock lk(g_mtx);
        if (g_on.find(fid) == g_on.end()) return;
        auto& st = g_fx[fid];
        nClimax = ++st.climaxes;
        // 1.3.5: the signal already posed THIS climax - consume the mark, dispatch nothing.
        posePending = st.posePending;
        st.posePending = false;
        posing = st.posing;
        // ★ 1.3.4 FORGET THE PRE-CLIMAX AROUSAL (review of 2026-09-14; replaces 1.3.3's 5-minute hold,
        // which the user rejected). Since 1.3.2 ClimaxPose ends DD's vibration ~1 s after the orgasm, so
        // the next ReEvaluate saw `!vibrating` while the arousal it reads was still the CACHED pre-orgasm
        // value (>= 99 - DD's orgasm needs it) and the 100 lane fired a second vibration. Disarming the
        // lane here means that stale number cannot fire it: it re-arms only when a real reading comes in
        // below 99 (relief sets 50), so she climaxes again only when her arousal genuinely climbs back.
        st.fullArmed = false;
        // 1.3.5: the signal missed this one (name mismatch, a race the other way) and nothing is playing -
        // the pose is dispatched from here as before; claimed under the same lock so a late signal skips it.
        claim = !posePending && !posing;
        if (claim) st.posing = true;
    }
    const Context ctx = Classify(a);
    const bool    seen = StrangerWatching(a);
    // ★ 1.3.6: field 3 = the kneel will play (the 4th climax or later, arms free, OAR + DD NG's clip present,
    // not seated or mounted). The Controller says "knees" only on a 1 - a bound NPC keeps DD's own device clip
    // ("Kneel only if arms free") and must not be told she went down. ⚠ Ships with VRTEDD_Controller.pex 1.3.6.
    const bool kneel = ClimaxClipMode(a, nClimax) == 4 && !a->IsOnMount() &&
                       a->AsActorState()->GetSitSleepState() == RE::SIT_SLEEP_STATE::kNormal;
    char buf[128];
    std::snprintf(buf, sizeof buf, "climax|%d|%d|%d|0|%s",
                  nClimax, seen ? 1 : 0, kneel ? 1 : 0, ContextWord(ctx));
    // D5: credited (the count is hers) but not narrated into a scene.
    if (!SceneMutes(a, "climax")) {
        SKSE::ModCallbackEvent ev{};
        ev.eventName = "VRTE_DDZaZ_DeviceEffect";
        ev.strArg    = buf;
        ev.numArg    = static_cast<float>(nClimax);
        ev.sender    = a;
        if (auto* src = SKSE::GetModCallbackEventSource()) src->SendEvent(&ev);
        // ★★ THE POSE (the user, 2026-09-13, after seeing DD's floor clip): DD's ActorOrgasm plays
        // DDZazHornyE - a static SUPINE floor pose - for every NPC climax, and the standing override
        // the design had was pulled on 09-03 with the redistributed .hkx it rode on. Nothing replaced
        // it. VRTE_DDZaZ_Equip.ClimaxPose now sends DD's own standing event (DDZazHornyA) over the
        // floor clip for climaxes 1-3 and the edged/kneel event from the 4th, then sets arousal to 50
        // ("relief"). Works for EVERY source of a credited climax - our cast lane, the 100 lane, DD's
        // own loop - because it hangs off DD's orgasm event.
        // ⚠ CORRECTED 1.3.3: DDZazHornyA is an EVENT, not one clip. DD NG's OAR set picks at random
        // between ZazHornyA/B/C (the user rejected B in report 28) and swaps in device-specific clips
        // (front cuffs, elbow tie ...). What plays depends on the roll and on what she wears.
        // ★ 1.3.5: normally DD's orgasm SIGNAL (OnDDOrgasmSignal) has already dispatched the pose - early
        // enough that DD's floor clip never starts. This path now runs only when the signal missed.
        // 1.3.6: third Int 0 - the DLL added no faction on this path (DD's own clip may be holding it).
        if (claim)
            DispatchPapyrus("ClimaxPose", static_cast<std::int32_t>(fid), nClimax, 0);   // ⛔ THREE Ints - 1.3.6
    } else if (claim) {
        NotePoseEnd(fid);   // a scene owns her - the claim is released, no pose
    }
    logger::info("[WORN] CLIMAX #{} on 0x{:08X} (DD event) watched={} pose={}",
                 nClimax, fid, seen,
                 posePending ? "started by DD's orgasm signal"
                             : (posing ? "one already playing" : "started from the event (signal missed)"));
}

// ═══════════════════════════════════════════════════════════════════════════
// THE CLOTHING GATE — THE FOUR-LAYER MODEL (2026-08-29). The user's design:
//
//   "we gonna have to implement a layer system i think. So it make sense what
//    goes on what. I see four layer to this."
//
//   INTERNAL  plugs. "need to have the orifice open before installing/
//             removing, and the pelvis free of anything" — clothes AND a
//             device over the site both block, honouring DD's own zad_Permit*.
//   UNDER     goes under anything — latex/leather suits, piercings, harnesses,
//             chastity belts and bras, blindfolds, tight hoods, smaller gags.
//             Installing one needs the location BARE of clothes and armor.
//   MID       replaces clothing — corsets, boots, mittens, pet suits, the
//             binder family, posture collars, whole-head cages. Same install
//             rule as UNDER; the difference is what may go ON TOP, which has
//             no engine hook and is recorded in report 29 §0.6f, not enforced.
//   OUTER     goes over anything, armor included — yokes, prisoner shackles
//             and cuffs, ordinary collars. ⚠ ONE exception, the user's:
//             "Only Heavy armor block Ankle cuff... Same for Arms" — on the
//             limb regions a garment carrying ArmorHeavy blocks; light armor
//             and clothes never do. ⚠ And the CLOTH-GAG carve-out: an OUTER
//             FACE device goes over helmets but is refused by a hood, another
//             gag, or anything on the face slot — the ONE place a device
//             blocks a device, deliberately (report 29 §0.6e).
//
// ⚠ THIS IS THE QUERY, NOT THE ENFORCEMENT (unchanged): the NPC→NPC action
// consumes CanEquipDeviceOn as eligibility, PPB enforces the VR gesture
// (request filed), and the menu path stays open on purpose.
// ⚠ DEFAULT-DENY inside a resolved layer: a MID/UNDER device in a covered
// region refuses. A device resolving NO layer fails OPEN, loudly logged —
// refusing what we cannot classify would silently veto content mods.
// ⚠ VRTE_DDZaZ_ClothingGate.ini reclassifies by NAME substring ([internal]
// [under] [mid] [outer]) so the user extends the model without a rebuild.
// The pre-layer [alwaysallow] section is read as [outer].
// ═══════════════════════════════════════════════════════════════════════════

enum class Layer : int { None = -1, Internal = 0, Under = 1, Mid = 2, Outer = 3 };

// A device's REGION — the biped slots whose non-device occupant blocks it.
// slot n → bit (n-30), the engine's own encoding.
constexpr std::uint32_t kRegHead   = (1u << 0) | (1u << 1) | (1u << 12);  // 30 31 42
constexpr std::uint32_t kRegFace   = (1u << 14);                          // 44
constexpr std::uint32_t kRegNeck   = (1u << 15);                          // 45
constexpr std::uint32_t kRegTorso  = (1u << 2);                           // 32
constexpr std::uint32_t kRegArms   = (1u << 3) | (1u << 4);               // 33 34
constexpr std::uint32_t kRegFeet   = (1u << 7) | (1u << 8);               // 37 38
constexpr std::uint32_t kRegPelvis = (1u << 2) | (1u << 19) | (1u << 22); // 32 49 52

struct LayerInfo { Layer layer; std::uint32_t region; };

// ── the INI: the user's reclassification, by device-name substring ──────────
static std::vector<std::string> g_gateNeedles[4];   // internal under mid outer
static bool g_gateLoaded = false;

static void LoadGateIni()
{
    g_gateLoaded = true;
    for (auto& v : g_gateNeedles) v.clear();
    FILE* f = nullptr;
    if (fopen_s(&f, "Data/SKSE/Plugins/VRTE_DDZaZ_ClothingGate.ini", "r") != 0 || !f) {
        logger::info("[GATE] no VRTE_DDZaZ_ClothingGate.ini - class defaults only");
        return;
    }
    int sec = -1, added = 0;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p || *p == '#' || *p == ';' || *p == '\n' || *p == '\r') continue;
        if (*p == '[') {
            sec = -1;
            if      (_strnicmp(p, "[internal]",    10) == 0) sec = 0;
            else if (_strnicmp(p, "[under]",        7) == 0) sec = 1;
            else if (_strnicmp(p, "[mid]",          5) == 0) sec = 2;
            else if (_strnicmp(p, "[outer]",        7) == 0) sec = 3;
            else if (_strnicmp(p, "[alwaysallow]", 13) == 0) sec = 3;   // legacy
            continue;
        }
        if (sec < 0) continue;
        char key[32] = {}, val[400] = {};
        if (std::sscanf(p, " %31[A-Za-z] = %399[^\r\n]", key, val) != 2) continue;
        if (_stricmp(key, "name") != 0) continue;
        for (int k = (int)std::strlen(val) - 1;
             k >= 0 && (val[k] == ' ' || val[k] == '\t'); --k) val[k] = 0;
        if (val[0]) { g_gateNeedles[sec].push_back(val); ++added; }
    }
    fclose(f);
    logger::info("[GATE] layer INI: {} needle(s) - internal={} under={} mid={} outer={}",
                 added, g_gateNeedles[0].size(), g_gateNeedles[1].size(),
                 g_gateNeedles[2].size(), g_gateNeedles[3].size());
}

// ⚠ When one name matches several sections the MOST PERMISSIVE wins (outer
// first) — an INI line exists to open something the defaults closed.
static int IniLayerOf(const char* label)
{
    if (!label || !label[0]) return -1;
    for (int sec = 3; sec >= 0; --sec)
        for (const auto& n : g_gateNeedles[sec])
            if (ContainsNoCase(label, n.c_str())) return sec;
    return -1;
}


// A region derived from the device's OWN biped slots, for when no table row
// supplied one - the INI reclassifies layer only, and 43 devices were sailing
// through on a zero region (2026-08-30 audit, the silent half of the fail-open).
static std::uint32_t SlotRegionOf(RE::TESObjectARMO* w)
{
    if (!w) return 0;
    const auto m = static_cast<std::uint32_t>(w->GetSlotMask().underlying());
    std::uint32_t r = 0;
    if (m & ((1u << 0) | (1u << 1) | (1u << 12)))  r |= kRegHead;     // 30 31 42
    if (m & ((1u << 14) | (1u << 25)))             r |= kRegFace;     // 44 55
    // ★ SLOT 58 IS TORSO, NOT NECK (2026-08-30, PPB's measurement adopted):
    // 219 of its carriers in this order are 185 Harness + 28 Corset + 6
    // unclassified torso shells - DD convention uses 58 for the torso SHELL.
    // We had it as neck; PPB had it as wrist; the data says torso for both.
    if (m & (1u << 15))                            r |= kRegNeck;     // 45
    if (m & ((1u << 2) | (1u << 16) | (1u << 26) | (1u << 28)))
        r |= kRegTorso;                                               // 32 46 56 58
    if (m & ((1u << 3) | (1u << 4) | (1u << 29)))  r |= kRegArms;     // 33 34 59
    if (m & ((1u << 7) | (1u << 8) | (1u << 23)))  r |= kRegFeet;     // 37 38 53
    if (m & ((1u << 19) | (1u << 22)))             r |= kRegPelvis;   // 49 52
    return r;
}
// ── layer + region from the DD class ────────────────────────────────────────
static LayerInfo LayerOfClass(const char* cls, const char* label, RE::TESObjectARMO* w = nullptr)
{
    if (!cls || !cls[0]) return { Layer::None, 0 };
    // name-keyed rulings first — the same predicates the worn block uses
    if (IsTallCollar(cls, label)) return { Layer::Mid, kRegNeck };
    if (_strnicmp(cls, "Gag", 3) == 0 && ContainsNoCase(label, "Scold"))
        return { Layer::Mid, kRegFace | kRegHead };          // whole-head cage
    // ★ 2026-09-12: THE PONY HARNESS GAG IS MID (the user on #1449-1451, "gag harness
    // with pony ears. Layer is mid, not under"). It is a whole head harness with the
    // bit, not a strap across the mouth.
    // ⛔ KEYED ON THE MESH (pony_headharness), NOT THE NAME: the three Devious Lore
    // gags the user ruled are NAMELESS on both halves. The census shows that mesh on
    // ~21 devices - DD's 18 "Pony Harness Gag" variants (plain / with Ears / with
    // Blinders, every colour), both Thelia's Training Gags and Horse-play Headgear -
    // so they move together, exactly as the boot and hood rulings did.
    // ⚠ Needs the device: a call site without `w` falls through to the Gag row.
    if (_strnicmp(cls, "Gag", 3) == 0 && WornMeshHas(w, "pony_headharness"))
        return { Layer::Mid, kRegFace | kRegHead };
    // ★ 2026-09-12: THE CLOSED EXTREME HOOD IS A BLINDFOLD, not a hood (the user:
    // "957 is the blindfold itself, 958 is the hood with the eye open"). It takes
    // the Blindfold row's layer - tested BEFORE the every-real-hood rule below,
    // which would otherwise make it Mid.
    if (_stricmp(cls, "Hood") == 0 && IsExtremeHood(label) && !IsExtremeHoodOpen(label))
        return { Layer::Under, kRegFace };
    // ★★ EVERY REAL HOOD IS MID (the user, 2026-09-12): "those are all Mid Layer,
    // full hood, can't have anything over them". The ruling was given as a row
    // list (39 DD + 3 ZaZ), but nine UNLISTED hoods are exact colour siblings of
    // listed ones - three more "Oil Hood with Eye Slits", two "with Face
    // Opening", two "Leather Hood", two "with Eye and Mouth Slits". Keying on the
    // row list would have made the White one Mid and the Red one Under. So the
    // rule is the object: Hood class, minus the two families the ruling did not
    // touch -
    //   * Ash Masks (60) - masks, not enclosing hoods, ruled separately before
    //   * "Blocking Blindfold" (5) - a blindfold mis-tagged as a hood (09-08)
    // ⚠ "can't have anything over them" needs no new rule: the OUTER face
    // carve-out in ClothingBlockOn already refuses a face device over a hood.
    if (_stricmp(cls, "Hood") == 0 &&
        !ContainsNoCase(label, "Ash Mask") && !ContainsNoCase(label, "Blindfold"))
        return { Layer::Mid, kRegHead };
    if (_strnicmp(cls, "Plug", 4) == 0) return { Layer::Internal, kRegPelvis };

    struct Row { const char* c; Layer l; std::uint32_t r; };
    static const Row rows[] = {
        // UNDER — worn against skin, clothes may go over them later (§0.6f)
        { "Suit",             Layer::Under, kRegTorso },
        { "Harness",          Layer::Under, kRegTorso },
        { "Belt",             Layer::Under, kRegPelvis },
        { "Bra",              Layer::Under, kRegTorso },
        // ★ PIERCINGS ARE INTERNAL (user, 2026-08-30): "They are not really,
        // but they can only be equip/remove when those part are fully naked,
        // and they do go under some under gears." So they take the Internal
        // GATE (bare of clothes AND of covering devices to install/remove) -
        // but NOT a plug's invisibility: VisibleOn carves them out, visible
        // whenever nothing at all covers their region.
        { "PiercingsNipple",  Layer::Internal, kRegTorso },
        { "PiercingsVaginal", Layer::Internal, kRegPelvis },
        { "Butterfly",        Layer::Mid,   kRegArms },     // the ARM binder (2026-08-30)
        { "Blindfold",        Layer::Under, kRegFace },
        { "Hood",             Layer::Under, kRegHead },
        { "Gloves",           Layer::Under, kRegArms },
        // MID — replaces clothing outright
        { "Corset",             Layer::Mid, kRegTorso },
        { "Boots",              Layer::Mid, kRegFeet },
        { "BondageMittens",     Layer::Mid, kRegArms },
        { "PetSuit",            Layer::Mid, kRegTorso | kRegArms | kRegFeet },
        { "PonyGear",           Layer::Mid, kRegTorso | kRegFeet },
        { "HobbleSkirt",        Layer::Mid, kRegTorso | kRegFeet },
        { "HobbleSkirtRelaxed", Layer::Mid, kRegTorso | kRegFeet },
        { "StraitJacket",       Layer::Mid, kRegTorso | kRegArms },
        { "Armbinder",          Layer::Mid, kRegArms },
        { "ArmbinderElbow",     Layer::Mid, kRegArms },
        { "ElbowTie",           Layer::Mid, kRegArms },
        { "Boxbinder",          Layer::Mid, kRegArms },
        { "HeavyBondage",       Layer::Mid, kRegArms },
        // OUTER — prisoner gear and ornaments, over anything (heavy-armor
        // rule applies on the limb regions; INI [under] demotes the ones the
        // user says go against skin, when that list arrives)
        { "Yoke",          Layer::Outer, kRegNeck },
        { "YokeBB",        Layer::Outer, kRegNeck },
        { "YokeFront",     Layer::Outer, kRegNeck },
        { "Collar",        Layer::Outer, kRegNeck },
        { "ArmCuffs",      Layer::Outer, kRegArms },
        { "CuffsArms",     Layer::Outer, kRegArms },
        { "CuffsFront",    Layer::Outer, kRegArms },
        { "LegCuffs",      Layer::Outer, kRegFeet },
        { "CuffsLegs",     Layer::Outer, kRegFeet },
        { "AnkleShackles", Layer::Outer, kRegFeet },
    };
    for (auto& r : rows) if (_stricmp(cls, r.c) == 0) return { r.l, r.r };
    if (_strnicmp(cls, "Gag", 3) == 0)       return { Layer::Under, kRegFace };
    if (_strnicmp(cls, "Piercings", 9) == 0) return { Layer::Internal, kRegTorso };
    return { Layer::None, 0 };
}

// ── layer + region from ZaZ / DoM site keywords, when no DD class resolves ──
static LayerInfo LayerOfSites(RE::TESObjectARMO* w)
{
    if (!w) return { Layer::None, 0 };
    const auto m = static_cast<std::uint32_t>(w->GetSlotMask().underlying());
    // ⛔ THE BALL FAMILY: nine ZaZ genital devices carry zbfWornGag — ZaZ's own
    // data bug (report 29 §0.6b). Slot 52 separates them 9/9 with zero false
    // positives across all 222 records. Genital gear, not a gag.
    if (HasKw(w, "zbfWornGag") && (m & (1u << 22)))
        return { Layer::Under, kRegPelvis };
    // specific before generic: the binder poses before the plain wrist site
    // (an arm-rope carries zbfWornWrist too and must not read as a loose cuff)
    if (HasKw(w, "zbfWornElbows") || HasKw(w, "zbfAnimHandsBoxTied") ||
        HasKw(w, "zbfAnimHandsArmbinder") || HasKw(w, "zbfAnimHandsFullyRopedArms") ||
        HasKw(w, "zbfAnimHandsHandsTiedToNeck") ||
        // #1396 "Vertically Poled" - the user: "a special armbinder, but with a
        // log in the middle of the back". It carries zbfWornYoke too and read
        // Outer without this; a binder must resolve before the yoke row.
        HasKw(w, "zbfAnimHandsAndNeckVerticallyPoled"))
        return { Layer::Mid, kRegArms };
    // Wrists tied INTO a waist rope (#1397/98, #1403/04) - the user:
    // "Definitely an mid, won't be able to wear anything over that."
    if (HasKw(w, "zbfAnimHandsWaist"))
        return { Layer::Mid, kRegArms | kRegTorso };
    // ⚠ The waist/breast ROPE WEBS (#1399-#1402) also carry NoFighting on a
    // body slot and were caught by the straitjacket rule below - but they are
    // the rope-harness family (clothes go OVER them, the user's layering
    // ruling), so their SITE must answer first.
    if (HasKw(w, "zbfWornBreastRope") || HasKw(w, "zbfWornWaist"))
        return { Layer::Under, kRegTorso };
    // ⛔ THE ZaZ STRAITJACKET (#1395) HAS NO BINDER POSE KEYWORD - only
    // NoFighting + a body slot. Without this row it fell through to its
    // zbfWornCollar and resolved "collar -> over anything". Found 2026-08-29.
    if (HasKw(w, "zbfEffectNoFighting") && (m & (1u << 2)))
        return { Layer::Mid, kRegTorso | kRegArms };
    if (HasKw(w, "zbfWornYoke") || HasKw(w, "zbfWornCrossPole") ||
        HasKw(w, "zbfWornFiddle"))
        return { Layer::Outer, kRegNeck };
    if (HasKw(w, "zbfWornWrist") || HasKw(w, "zbfWornAnkles")) {
        std::uint32_t r = 0;                       // the ZaZ cuff SETS are both
        if (HasKw(w, "zbfWornWrist"))  r |= kRegArms;
        if (HasKw(w, "zbfWornAnkles")) r |= kRegFeet;
        return { Layer::Outer, r };
    }
    // ⛔ THE ZaZ BODY HARNESS (#1385) carries zbfWornCollar for its collar
    // piece and would read "over anything" - but it seals the vagina and
    // locks plugs, i.e. it is worn against skin. The body-function keywords
    // out-rank the collar piece. The user: "as like the other DD harness".
    if (HasKw(w, "zbfWornPreventVaginal") || HasKw(w, "zbfWornPreventAnal") ||
        HasKw(w, "zbfWornLockPlugs"))
        return { Layer::Under, kRegTorso | kRegPelvis };
    if (HasKw(w, "zbfWornPiercing") || HasKw(w, "zbfWornPiercingNipple"))
        return { Layer::Internal, kRegTorso };   // piercings ruling, 2026-08-30
    if (HasKw(w, "zbfWornCollar")) return { Layer::Outer, kRegNeck };
    // ⚠ Hood BEFORE gag/blindfold: the ZaZ full hoods carry all three site
    // keywords and must block on the HEAD (a helmet), not the face slot.
    if (HasKw(w, "zbfWornHood")) return { Layer::Under, kRegHead };
    if (HasKw(w, "zbfWornGag") || HasKw(w, "zbfWornBlindfold"))
        return { Layer::Under, kRegFace };
    if (HasKw(w, "zbfWornBelt")) return { Layer::Under, kRegPelvis };
    if (HasKw(w, "zbfWornBra") || HasKw(w, "zbfWornBreastRope") || HasKw(w, "zbfWornWaist"))
        return { Layer::Under, kRegTorso };
    // ── Diary of Mine, the full vocabulary (2026-08-29 round 9 - only 2 of
    // its 12 DOMWorn* keywords were mapped and nearly the whole DoM block
    // resolved no layer). Specific before generic, as everywhere.
    if (HasKw(w, "DOMWornArmbinder"))  return { Layer::Mid,      kRegArms };
    if (HasKw(w, "DOMWornYoke"))       return { Layer::Outer,    kRegNeck };
    if (HasKw(w, "DOMWornPlugAnal") || HasKw(w, "DOMWornPlugVaginal"))
        return { Layer::Internal, kRegPelvis };
    if (HasKw(w, "DOMWornCollar"))     return { Layer::Outer,    kRegNeck };
    if (HasKw(w, "DOMWornGag") || HasKw(w, "DOMWornBlindfold"))
        return { Layer::Under, kRegFace };
    if (HasKw(w, "DOMWornWrist") || HasKw(w, "DOMWornAnkle")) {
        std::uint32_t r = 0;
        if (HasKw(w, "DOMWornWrist")) r |= kRegArms;
        if (HasKw(w, "DOMWornAnkle")) r |= kRegFeet;
        return { Layer::Outer, r };
    }
    return { Layer::None, 0 };
}

// Is this worn piece a restraint-framework item (DD / ZaZ / DoM), i.e. exempt
// from blocking? Prefix scan, so it needs no keyword list to maintain.
bool IsFrameworkItem(RE::TESObjectARMO* w)
{
    auto* kwf = w ? w->As<RE::BGSKeywordForm>() : nullptr;
    if (!kwf) return false;
    for (std::uint32_t i = 0; i < kwf->GetNumKeywords(); ++i) {
        auto kw = kwf->GetKeywordAt(i);
        if (!kw.has_value() || !kw.value()) continue;
        const char* id = kw.value()->GetFormEditorID();
        if (id && (_strnicmp(id, "zad", 3) == 0 || _strnicmp(id, "zbf", 3) == 0 ||
                   _strnicmp(id, "DOM", 3) == 0))
            return true;
    }
    return false;
}

// "" = the device may go on; otherwise the name of what is in the way.
// ⚠ Takes EITHER half of a DD pair — the equipping side only ever holds the
// INVENTORY half, and both halves carry the class keywords.
// ═══════════════════════════════════════════════════════════════════════════
// ★★ CAN ANYONE ELSE SEE IT? (2026-08-30)
//
// The user: "what is currently visible versus not. SkyrimNet expose gears and
// what other NPC wears for awareness. An NPC with plugs, but wearing clothes or
// armors, won't have them seen by other NPC."
//
// ⛔ THIS IS A LEAK TO CLOSE, NOT A GAP TO FILL. Measured 2026-08-30:
// SkyrimNet's own `get_worn_equipment` renders in the LIVE dialogue prompt via
// 0410_equipment.prompt's `dialogue_target` branch, and its documentation says
// it returns every worn item WITH ITS KEYWORDS. So a plug under full plate is
// already being described to whoever she talks to. Report 30 §1 has the trace.
//
// ★ THE LAYER SYSTEM ALREADY ANSWERS THIS - it is ClothingBlockOn's question
// asked backwards. The gate asks "is the region clear, so this can go ON?";
// visibility asks "is the region clear, so this can be SEEN?" - the same layer,
// the same region mask, the same worn-armor walk.
//
//     INTERNAL  never visible - it is inside the body
//     UNDER     visible only while its region is bare
//     MID       always visible - it REPLACES clothing
//     OUTER     always visible - it goes over everything
//
// ⚠ VISIBILITY IS A PROPERTY OF THE WEARER'S OUTFIT, NOT OF THE OBSERVER, so
// one answer serves every watcher and there is nothing per-observer to compute.
// Line of sight and distance are the CALLER's business (the witness scans
// already do that); this answers only "is it covered".
// ⚠ FAILS VISIBLE. A device we cannot classify is reported, not hidden: an
// over-reported device is a wrong line, an under-reported one is a silent lie
// about her body, and the second is worse.
bool VisibleOn(RE::Actor* wearer, RE::TESObjectARMO* w)
{
    if (!wearer || !w) return true;
    if (!g_gateLoaded) LoadGateIni();

    char cls[64] = {};
    ClassOf(w, cls, sizeof cls);
    // ⛔ A DD RENDERED HALF HAS NO FULL NAME - measured 0 of 1221 (report 23
    // §30, and re-measured by the 2026-08-30 audit). Calling GetName() raw here
    // handed every name-keyed rule an empty string, so IsTallCollar, the
    // scold's-bridle rule and every ClothingGate INI needle were DEAD: 36
    // devices resolved the wrong layer, including 23 posture/restrictive
    // collars going OUTER against the user's own ruling. The pushed
    // inventory-half name is the only real one; Report() has used it all along.
    std::string nm = NameForDevice(wearer->GetFormID(), w, cls);
    const char* label = nm.c_str();
    ReclassByName(cls, sizeof cls, label);      // #299 collar etc., 2026-08-30

    // ★ TAIL PLUGS ARE ALWAYS VISIBLE (the user, 2026-08-30): the plug is
    // internal, but the TAIL it anchors hangs in plain sight regardless of
    // clothing. The observer line depicts the tail, not the plug (Report).
    if (IsTailPlug(cls, label, w)) return true;

    LayerInfo li = LayerOfClass(cls, label, w);
    if (li.layer == Layer::None) li = LayerOfSites(w);
    if (const int il = IniLayerOf(label); il >= 0)
        li.layer = static_cast<Layer>(il);

    if (li.layer == Layer::Internal) {
        // ★ THE PIERCING CARVE-OUT (user, 2026-08-30): piercings sit in the
        // Internal CATEGORY for the equip gate ("only equip/remove when those
        // part are fully naked"), but unlike a plug they are ON the skin -
        // visible whenever nothing at all covers their region, garment OR
        // device. The coverage test is region-space (SlotRegionOf of the
        // coverer), so a bra on slot 56 hides a nipple ring even though the
        // blocking constant kRegTorso is slot 32 alone. A bra or suit with
        // the breasts open (zad_ExposedBreasts) shows a nipple ring through.
        if (_strnicmp(cls, "Piercings", 9) == 0 ||
            HasKw(w, "zbfWornPiercing") || HasKw(w, "zbfWornPiercingNipple")) {
            if (!li.region) li.region = SlotRegionOf(w);
            if (!li.region) return true;
            bool covered = false;
            ForEachWorn(wearer, [&](RE::TESObjectARMO* g) {
                if (g == w) return true;
                if (!(SlotRegionOf(g) & li.region)) return true;
                if ((li.region & kRegTorso) && HasKw(g, "zad_ExposedBreasts"))
                    return true;                     // open-cup: the ring shows
                covered = true;
                return false;
            });
            return !covered;
        }
        return false;                                // true internals: plugs
    }
    if (li.layer != Layer::Under)    return true;           // Mid / Outer / None
    if (!li.region) li.region = SlotRegionOf(w);            // slot-derived fallback
    if (!li.region)                  return true;

    // UNDER: hidden the moment a non-device garment covers its region.
    bool covered = false;
    ForEachWorn(wearer, [&](RE::TESObjectARMO* g) {
        if (g == w) return true;
        const auto m = static_cast<std::uint32_t>(g->GetSlotMask().underlying());
        if (!(m & li.region) || IsFrameworkItem(g)) return true;
        covered = true;
        return false;
    });
    return !covered;
}

// ★★ THE SLOT GATE (2026-09-10, the user: "slot gating is the way to go").
// A device refuses to go on where a worn DEVICE already holds one of its biped slots, and
// names that device. The engine cannot keep two armors in one slot: the newcomer would push
// the worn device off, and a locked DD device pushes back. PPB measured exactly that on
// 2026-09-10 12:06 - a second gag vanished from the hand for 2.5 s, then dropped (their
// follow-up #4 §K4). Refused here, at release, it stays visibly in the hand instead.
// ⚠ DEVICES ONLY. A garment in the slot is the layer model's question, and the engine simply
// swaps a garment out - that path is unchanged.
// ⚠ NOT the whole device-vs-device model. Pieces on DIFFERENT slots that still have an order
// (a blindfold must go on before a full-face gag; with a half-face gag the order does not
// matter) are the full check, deliberately left for once this mechanism is proven.
// ⚠ Pass the RENDERED half. A DD inventory half carries no slots and never conflicts.
std::string DeviceSlotConflict(RE::Actor* target, RE::TESForm* device)
{
    auto* armo = device ? device->As<RE::TESObjectARMO>() : nullptr;
    if (!target || !armo) return {};
    const auto want = static_cast<std::uint32_t>(armo->GetSlotMask().underlying());
    if (!want) return {};
    std::string hit;
    ForEachWorn(target, [&](RE::TESObjectARMO* w) {
        if (w == armo || !IsFrameworkItem(w)) return true;
        const auto m = static_cast<std::uint32_t>(w->GetSlotMask().underlying());
        if (!(m & want)) return true;
        char wc[64] = {};
        ClassOf(w, wc, sizeof wc);
        std::string nm = NameForDevice(target->GetFormID(), w, wc);
        hit = nm.empty() ? std::string("the device already worn there") : nm;
        return false;
    });
    return hit;
}

// The whole equip question, in the order the answers matter: a slot another device holds,
// then the layer model. Every equip caller asks THIS - PPB's gesture through BlockedBy, and
// the Papyrus eligibility natives - so they can never disagree.
std::string EquipBlockedBy(RE::Actor* target, RE::TESForm* device)
{
    std::string why = DeviceSlotConflict(target, device);
    return why.empty() ? ClothingBlockOn(target, device) : why;
}

std::string ClothingBlockOn(RE::Actor* target, RE::TESForm* device)
{
    if (!target || !device) return {};
    if (!g_gateLoaded) LoadGateIni();

    auto* armo = device->As<RE::TESObjectARMO>();
    char cls[64] = {};
    if (armo) ClassOf(armo, cls, sizeof cls);
    // ⛔ FIXED 2026-09-03 - THIS READ device->GetName() RAW, AND A DD RENDERED
    // HALF HAS NO NAME (0 of 1221). Every name-keyed rule on the GATE path was
    // therefore dead: all 30 ClothingGate INI needles (11 outer / 11 mid /
    // 8 under), ReclassByName's #299 collar, IsTallCollar and the scold's
    // bridle. The identical fix was applied to VisibleOn (:3839) and Report()
    // on 2026-08-30 and never reached this function - so layers read correctly
    // on the page and the gate was blind. NameForClass returns the pushed
    // INVENTORY-half name, which is the only real one.
    // ★ D4 (2026-09-13): ...and that fix was live on the REMOVAL gate only. On an EQUIP the
    // device is not worn yet, so no per-actor push has happened and the label was still "".
    // NameForDevice adds the pair map - the inventory half's own name off the record - which
    // answers for a device nobody wears yet. All 30 INI needles, IsTallCollar, the Scold rule
    // and ReclassByName are live on this ask from here on.
    std::string nm = NameForDevice(target->GetFormID(), armo, cls);
    if (nm.empty()) { const char* n = device->GetName(); if (n) nm = n; }
    const char* label = nm.c_str();
    ReclassByName(cls, sizeof cls, label);      // #299 collar etc., 2026-08-30

    LayerInfo li = LayerOfClass(cls, label, armo);
    if (li.layer == Layer::None) li = LayerOfSites(armo);

    // The INI is the user's word and outranks the defaults — layer only; the
    // region keeps whatever the class/site resolution found.
    if (const int il = IniLayerOf(label); il >= 0)
        li.layer = static_cast<Layer>(il);

    if (li.layer == Layer::None) {
        // D17: only a DEVICE without a layer is worth a line - every ordinary garment PPB asks
        // about resolves none, and logging each one buried the real fail-opens.
        if (armo && IsCoveredDevice(armo))
            logger::info("[GATE] device 0x{:08X} '{}' resolves no layer - allowed (fail open)",
                         device->GetFormID(), label);
        return {};
    }
    // ⚠ region fallback (2026-08-30): an INI-reclassified device keeps no table
    // region - derive one from its own slots rather than silently allowing.
    if (!li.region && armo) li.region = SlotRegionOf(armo);
    if (!li.region) return {};

    // ★ D14 (2026-09-13): a blocking DEVICE is named through the same resolution as everything
    // else. Its rendered half's own GetName() is "" for every DD device, so the refusal read
    // "the device locked over the hips is in the way" where DeviceSlotConflict, one function up,
    // already said "the Iron Chastity Belt". Garments keep their own name.
    const std::uint32_t tfid = target->GetFormID();
    const auto nameOf = [tfid](RE::TESObjectARMO* w, const char* fb) -> std::string {
        if (!w) return std::string(fb);
        char wc[64] = {};
        ClassOf(w, wc, sizeof wc);
        std::string nm = NameForDevice(tfid, w, wc);
        return nm.empty() ? std::string(fb) : nm;
    };

    std::string hit;
    if (li.layer == Layer::Outer) {
        if (li.region & kRegFace) {
            // The cloth-gag rule: over helmets, refused by a hood, another gag,
            // or anything on the face slot — one of the FOUR device-blocks-device
            // rules (this, the collar below, INTERNAL, and DeviceSlotConflict).
            ForEachWorn(target, [&](RE::TESObjectARMO* w) {
                if (w == armo) return true;
                const auto m = static_cast<std::uint32_t>(w->GetSlotMask().underlying());
                const bool face = (m & kRegFace) != 0;
                const bool hood = HasKw(w, "zad_DeviousHood") || HasKw(w, "zbfWornHood") ||
                                  HasKw(w, "DOMWornHood");
                if (!face && !hood) return true;
                hit = nameOf(w, "the covering over the face");
                return false;
            });
            return hit;
        }
        if (li.region & (kRegArms | kRegFeet)) {
            // "Only Heavy armor block Ankle cuff... Same for Arms" — light
            // armor and clothes never block an OUTER cuff (2026-08-29).
            ForEachWorn(target, [&](RE::TESObjectARMO* w) {
                const auto m = static_cast<std::uint32_t>(w->GetSlotMask().underlying());
                if (!(m & li.region) || IsFrameworkItem(w) || !HasKw(w, "ArmorHeavy"))
                    return true;
                hit = nameOf(w, "the heavy armor worn there");
                return false;
            });
            return hit;
        }
        // ★ "A collar can go on anything, EXCEPT ANOTHER COLLAR" (the user,
        // 2026-08-30) - the second deliberate device-blocks-device carve-out,
        // after the cloth gag. Only when the INCOMING device is itself a
        // collar; yokes and everything else stay unblocked at the neck.
        if (_stricmp(cls, "Collar") == 0) {
            ForEachWorn(target, [&](RE::TESObjectARMO* g) {
                if (g == armo) return true;
                if (HasKw(g, "zad_DeviousCollar") || HasKw(g, "zbfWornCollar") ||
                    HasKw(g, "DOMWornCollar")) {
                    hit = nameOf(g, "the collar already worn there");
                    return false;
                }
                return true;
            });
            return hit;
        }
        return {};                           // neck / head OUTER: over anything
    }

    // UNDER and MID install identically: the location must be bare of clothes
    // and armor. They differ in what may go ON TOP, which nothing enforces —
    // no engine hook exists for garment equips (report 29 §0.6f).
    ForEachWorn(target, [&](RE::TESObjectARMO* w) {
        const auto m = static_cast<std::uint32_t>(w->GetSlotMask().underlying());
        if (!(m & li.region) || IsFrameworkItem(w)) return true;
        hit = nameOf(w, "the clothing worn there");
        return false;
    });
    if (!hit.empty() || li.layer != Layer::Internal) return hit;

    // INTERNAL only: "the orifice open... and the pelvis free of anything" —
    // a DEVICE over the site blocks too, honouring DD's own zad_Permit*
    // exactly as the access line does (§4.5).
    const int sites = armo ? PlugSitesOf(armo) : 0;
    RE::TESObjectARMO* block = nullptr;
    const auto coveredBy = [&](const char* coverKw, const char* permitKw)
        -> RE::TESObjectARMO* {
        auto* c = WornByKw(target, coverKw);
        return (c && c != armo && !HasKw(c, permitKw)) ? c : nullptr;
    };
    // ★ PIERCINGS (Internal since 2026-08-30): their covering DEVICE is not a
    // plug's. A nipple ring is sealed away by a bra or closed suit (a
    // zad_ExposedBreasts cup leaves it reachable); a clitoris/labia ring by a
    // belt or suit, honouring zad_PermitVaginal exactly like the access line.
    // The plug-site logic below must not run for them - a hip belt does not
    // block a NIPPLE ring, and PlugSitesOf answers 0 for any piercing, which
    // would have run both orifice branches.
    if (_strnicmp(cls, "Piercings", 9) == 0 ||
        (armo && (HasKw(armo, "zbfWornPiercing") || HasKw(armo, "zbfWornPiercingNipple")))) {
        const bool nipple = _stricmp(cls, "PiercingsVaginal") != 0;
        if (nipple) {
            for (const char* k : { "zad_DeviousBra", "zad_DeviousSuit" })
                if ((block = coveredBy(k, "zad_ExposedBreasts")) != nullptr) break;
        } else {
            for (const char* k : { "zad_DeviousBelt", "zad_DeviousSuit" })
                if ((block = coveredBy(k, "zad_PermitVaginal")) != nullptr) break;
        }
        if (block) return nameOf(block, "the device covering it");
        return {};
    }
    if (sites == 0 || (sites & 1)) {
        for (const char* k : { "zad_DeviousBelt", "zad_DeviousSuit",
                               "zad_DeviousPlugVaginal" })
            if ((block = coveredBy(k, "zad_PermitVaginal")) != nullptr) break;
    }
    if (!block && (sites == 0 || (sites & 2))) {
        for (const char* k : { "zad_DeviousBelt", "zad_DeviousSuit",
                               "zad_DeviousPlugAnal" })
            if ((block = coveredBy(k, "zad_PermitAnal")) != nullptr) break;
    }
    if (block) return nameOf(block, "the device locked over the hips");
    return {};
}

// Public wrapper so the gate interface can ask "is this ours?" without pulling
// the file-local predicate into the header (which made internal calls ambiguous).
bool IsKnownDevice(RE::TESObjectARMO* w)
{
    return w && IsCoveredDevice(w);
}

// ★ THE LAYER, as a plain int for the public gate interface (VrteDdzGateAPI.h).
// Same resolution order the gate uses, so a consumer reasoning from this can
// never disagree with a refusal we hand back.
int LayerIdOf(RE::TESObjectARMO* w)
{
    if (!w) return -1;
    if (!g_gateLoaded) LoadGateIni();
    char cls[64] = {};
    ClassOf(w, cls, sizeof cls);
    // ⚠ ACTOR-LESS BY DESIGN, and that has a consequence worth stating.
    // NameForClass is keyed on (actor, class) - NoteDeviceName only ever writes
    // it for a REAL actor (:3467) - so the old NameForClass(0, cls) here could
    // never hit and was removed 2026-09-03. A named device (ZaZ, DoM, an
    // inventory half) resolves fine through GetName(); a DD RENDERED half has
    // no name and no wearer context, so name-keyed needles cannot apply to this
    // query. ⛔ Callers wanting needle-accurate layers must use BlockedBy /
    // CanEquip, which take the actor. If an actor-keyed layer is ever needed,
    // APPEND GetLayerOn(actorFid, deviceFid) as a new vtable slot - never
    // change slot 3 (VrteDdzGateAPI.h is append-only, no virtual dtor).
    std::string nm;
    if (const char* n = w->GetName()) nm = n;
    ReclassByName(cls, sizeof cls, nm.c_str());
    LayerInfo li = LayerOfClass(cls, nm.c_str(), w);
    if (li.layer == Layer::None) li = LayerOfSites(w);
    if (const int il = IniLayerOf(nm.c_str()); il >= 0)
        li.layer = static_cast<Layer>(il);
    return static_cast<int>(li.layer);
}

// ★ ONE MECHANISM LINE PER DEVICE (lifted out of Report 2026-09-13, D1). The worn block and
// the hand-equip awareness event (FittedLines) must say the SAME thing about a device - every
// ruling below (the scold's bridle, socks, the pear, pointe boots, tails, the ZaZ squeeze family,
// the Blocking Blindfold, the Extreme Hood pair, the hood variants) lives here once. Moved
// VERBATIM; only the indentation and `label.c_str()` -> `label` changed.
// Empty = an unmapped class, which says nothing (Report returns before it ever asks).
//
// ★ ROPE IS KNOTTED, NOT LOCKED (2026-09-13, the user: "fix" - and the 08-27 ruling on #109 "Black
// Rope Neck Binds": "rope can't be lock ... sophisticated knot"). ClassLine has one row per class,
// and the Collar / ArmCuffs / LegCuffs / CuffsFront / Belt rows say "locked" - so 63 rope devices
// (28 neck binds, 21 arm binds, 10 leg binds, 4 crotch binds) read "locked around the throat, made
// of rope ..., and the knots are pulled tight". The rope form replaces the row BEFORE any other
// override; none of the overrides below touches those classes.
static const char* RopeClassLine(const char* cls)
{
    static const struct { const char* c; const char* s; } rows[] = {
        { "Collar",     "knotted around the throat" },
        { "ArmCuffs",   "knotted around the arms" },
        { "CuffsArms",  "knotted around the arms" },
        { "LegCuffs",   "knotted around the legs" },
        { "CuffsLegs",  "knotted around the legs" },
        { "CuffsFront", "ties the wrists together in front" },
        { "Belt",       "knotted over the hips and drawn tight between the legs" },
    };
    for (const auto& r : rows) if (_stricmp(cls, r.c) == 0) return r.s;
    return nullptr;
}

static std::string DeviceWhat(RE::TESObjectARMO* w, const char* cls, const char* label,
                              bool observerView, bool rope)
{
    const char* what = ClassLine(cls);
    if (!what || !w) return {};
    if (rope)
        if (const char* r = RopeClassLine(cls)) what = r;

    // ★ 2026-09-13 - three device-level rulings, keyed on the record's own keywords so a shared class
    // row never drags the wrong devices along (none of the overrides further down touches these classes).
    std::string ruled;
    // #1015 Iron Yoke (Fiddle) - the user: "a yoke with the hands in front instead of at the sides" /
    // "It's a yoke with hand up front. wire as such." Keyed on zadNG_DeviousYokeFront, NEVER on the
    // YokeFront row: ZaZ's two real fiddles (zbfWornFiddle) share that row and keep the board wording.
    if (HasKw(w, "zadNG_DeviousYokeFront"))
        what = "locks the neck and both wrists into a rigid yoke, with the hands held up together in front";
    // rows 1265/1266 - ZaZ's rope shackles tied to a neck rope ("a wrist rope shackle type").
    else if (_stricmp(cls, "CuffsFront") == 0 && HasKw(w, "zbfAnimHandsAroundNeck"))
        what = rope ? "ties the wrists together and binds them to a rope around the neck"
                    : "binds the wrists together and fastens them to the neck";
    // #299 / #322 - "a collar that adds nipple clamps chained to it" (the user, 2026-09-11): the line names
    // the chain. ClassOf now answers Collar for them; the piercing keyword is what the chain is.
    else if (_stricmp(cls, "Collar") == 0 && HasKw(w, "zad_DeviousPiercingsNipple")) {
        ruled = std::string(what) + (ContainsNoCase(label, "Clamp")
                                         ? ", with a chain running down from it to clamps on the nipples"
                                         : ", with a chain running down from it to the nipples");
        what = ruled.c_str();
    }
    const auto wslots = static_cast<std::uint32_t>(w->GetSlotMask().underlying());
    const bool zazBall    = HasKw(w, "zbfWornGag") && (wslots & (1u << 22));
    const bool breastRope = HasKw(w, "zbfWornBreastRope");

    // ★ A HOOD IS NOT ONE THING (user, 2026-08-27, on device #85): "it's a hood
    // with eye slit. so that one is 'not' restraining vision, just talk and
    // hearing. also all hood should restraint speech right? they are tight
    // around the jaw after all."
    //
    // ★ DD ALREADY ENCODES THE FIRST HALF and the census makes it plain: a
    // hood that blocks sight carries zad_DeviousBlindfold as well, and one
    // with eye slits simply does not. "White Oil Hood with Eye Slits" carries
    // Gag + Hood and NO Blindfold. The flat ClassLine row said "dulling sight
    // and sound" for all 113 hoods, so every open-faced one claimed she could
    // not see.
    //
    // ⚠ SPEECH IS ADDED FOR EVERY HOOD, and deliberately NOT through
    // BlockedOral. 60 of the 113 carry no gag keyword at all, so DD does not
    // consider them gags - and overriding that would contradict DD's own
    // IsBlockedOral, which is the authority on whether the mouth is usable.
    // Muffled speech and a blocked mouth are different facts: a hood tight
    // around the jaw muffles what comes out without sealing it. The first
    // belongs to the device's own line, the second to the access line.
    // ★ THE SCOLD'S BRIDLE (2026-08-27, the user on #275-280: "torture
    // equipment"). Six devices, censused by name, all class Gag - so they
    // read as "fills the mouth and holds the jaw open", which describes a
    // ball gag and not a hinged iron cage locked around the entire head.
    // Mechanism only, per the standing rule: the cage and what it forces
    // into the mouth. The pain lives in the strain tier - the tongue-grip
    // pair routes to the CLAMP curve via IsClampDevice - not in adjectives.
    if (_strnicmp(cls, "Gag", 3) == 0 && ContainsNoCase(label, "Scold")) {
        what = ContainsNoCase(label, "Tongue")
            ? "locks a hinged iron cage around the whole head, with a clamp screwed shut on the tongue inside it"
            : "locks a hinged iron cage around the whole head, forcing a bit between the teeth";
    }

    // ★ SOCKS, instead of the arch line they were borrowing from boots.
    if (IsSoftFootwear(cls, label))
        what = "sheathes the foot and lower leg in one seamless piece";

    // ★ THE PEAR (2026-08-28). 16 records - 14 plugs and 2 gags - named
    // "Pear of Anguish". A pear is a segmented iron body that OPENS under a
    // screw once it is inside, so "seated inside" describes a smooth plug
    // and misses the entire device. Mechanism only, per the standing rule:
    // what it is and what it does. Whether that is dreadful is the LLM's
    // call, and the name already tells it.
    // ⚠ The oral pair take their own wording - a pear in the mouth spreads
    // the jaw, not the body.
    if (ContainsNoCase(label, "Pear")) {
        if (_strnicmp(cls, "Gag", 3) == 0)
            what = "sits between the teeth as a segmented iron body that has been screwed open, holding the jaw spread from the inside";
        else if (_strnicmp(cls, "Plug", 4) == 0)
            what = "seated inside as a segmented iron body that has been screwed open, spreading outward from within and unable to be drawn out while it is";
    }

    // ★ THE POINTE BOOT, said instead of the generic arch line.
    if (IsPointeBoot(cls, label))
        what = "locks the feet at full pointe, so the whole weight rides on the toes";


    // ★ THE PONY-TAIL PLUG (the user, 2026-08-30): "not the plug, the
    // tails itself. Depict them as such." The WEARER knows both halves;
    // an OBSERVER sees only the tail - the plug is out of sight, so the
    // observer line never mentions where it is seated.
    if (IsTailPlug(cls, label, w)) {
        what = observerView
            ? "a long tail hangs from beneath the tailbone and sways with every step"
            : "seated inside, anchoring the long tail that hangs from beneath the tailbone and sways with every step";
    }

    // ★ THE ZaZ SQUEEZE FAMILY (2026-08-30) - their class token is Clamps
    // but "biting jaws" is the wrong picture for a cinched band. Mechanism
    // only, per the standing rule; the pain lives in the clamp tier.
    if (zazBall)
        what = "a band cinched at the base of the scrotum, holding it stretched and squeezed";
    else if (breastRope && HasKw(w, "zbfWornWaist"))
        what = "rope cinched at the base of the breasts, squeezing them out, with more rope bound around the waist";
    else if (breastRope)
        what = "rope cinched at the base of the breasts, squeezing them out and holding the blood in them";

    // Backing store for the hood's optional breathing clause - `what` is a
    // const char*, so an appended variant needs storage that outlives it.
    std::string hoodLine;

    // ⛔ A "BLOCKING BLINDFOLD" IS NOT A HOOD (2026-08-27, the user on #157):
    // "it say sound and voice too, it's just a normal blindfold."
    //
    // ★ 39 devices carry zad_DeviousHood AND zad_DeviousBlindfold, and the
    // rank-2 Hood pin makes Hood win all 39 - which is CORRECT for 33 of
    // them (they are named "Hood") and for "Mask of Shame", but wrong for the
    // FIVE named "Blocking Blindfold". Those were claiming to dull sound and
    // muffle speech, neither of which a blindfold does.
    //
    // ⚠ The keywords cannot separate them - the two sets are identical - so
    // this is name-keyed, the same argument as the posture collar and the
    // pointe boot. It is checked BEFORE the hood branch so the hood wording
    // is never composed for one.
    //
    // ⚠ KNOWN LIMIT, and it cannot be fixed here: the DEAF restraint
    // category keys on zad_DeviousHood, so these five still register as
    // hearing-dulled. That registry walks WORN armor, and a DD rendered half
    // carries no FULL name (report 23 §30) - the name only reaches this
    // function because the quest script pushes it in from the inventory
    // half. 5 of 113. Recorded rather than papered over.
    if (_stricmp(cls, "Hood") == 0 && ContainsNoCase(label, "Blindfold"))
        what = ClassLine("Blindfold");
    // ★ 2026-09-12: the CLOSED Extreme Hood is "just a blindfold, not hearing or
    // speech muffled" (the user) - the same treatment as the Blocking Blindfold
    // row above. WearsRealHood drops it from the deaf state to match.
    else if (_stricmp(cls, "Hood") == 0 && IsExtremeHood(label) &&
             !IsExtremeHoodOpen(label))
        what = ClassLine("Blindfold");
    // ★ 2026-09-12: the Extreme Hood (Open) - "hole for the eye, can see". It
    // still encloses the head (so it dulls sound) and every one of the six
    // carries zad_DeviousGagPanel, so the mouth is covered unless the record
    // says otherwise. Only the sight half changes; WearsSightBlocker drops it
    // from the blind state to match.
    else if (_stricmp(cls, "Hood") == 0 && IsExtremeHoodOpen(label))
        what = HasKw(w, "zad_PermitOral")
            ? "encloses the head, dulling sound, with holes left for the eyes and the mouth left clear"
            : "encloses the head, dulling sound and muffling speech, with holes left for the eyes";
    else if (_stricmp(cls, "Hood") == 0) {
        // ⛔ AND THE SPEECH HALF IS NOT UNCONDITIONAL EITHER (2026-08-28,
        // the user on #86 "Red Oil Hood with Eye and Mouth Slits": "that one
        // got a mouth hole too, so no gag state"). It carries
        // zad_PermitOral - DD's own "the mouth still works" marker, the same
        // one BlockedOral has always honoured. A hood with a mouth slit
        // muffles nothing, so claiming it does was the sight defect over
        // again, on the other sense.
        const char* breath = "";
        const bool sight = !HasKw(w, "zad_PermitOral");   // speech blocked?
        const bool blind = HasKw(w, "zad_DeviousBlindfold");
        // ★ A BREATHING TUBE IS THE POINT OF THE DEVICE (2026-08-28). 30
        // hoods are named for a Tube or a Rebreather and all 30 read as
        // ordinary hoods - the one fact that distinguishes them, that every
        // breath is routed through fixed hardware, went unsaid. Appended
        // rather than replacing, because everything the hood row says is
        // still true of them.
        if (ContainsNoCase(label, "Rebreather"))
            breath = ", and every breath is drawn back through the same closed loop it was let out into";
        else if (ContainsNoCase(label, "Tube"))
            breath = ", and every breath has to be drawn through the one tube fitted into it";
        if (blind && sight)       what = "encloses the head, dulling sight and sound and muffling speech";
        else if (blind)           what = "encloses the head, dulling sight and sound, with the mouth left clear";
        else if (sight)           what = "encloses the head, dulling sound and muffling speech, with the eyes left clear";
        else                      what = "encloses the head, dulling sound, with the eyes and mouth left clear";
        if (breath[0]) { hoodLine = what; hoodLine += breath; what = hoodLine.c_str(); }
    }
    return what;
}

// ★ HOW HARD IT IS TO GET OUT OF (lifted out of Report 2026-09-13, D1) - DD's removal contract
// and ZaZ's on-record removability, as clauses. Shared by the worn block and FittedLines.
// Moved VERBATIM; only the indentation changed.
static std::string RemovalClauses(RE::TESObjectARMO* w)
{
    std::string s;
    if (!w) return s;
    // ★ NO PASSER-BY WILL SIMPLY TAKE IT OFF (2026-08-28). zad_BlockGeneric
    // is not an effect at all - it is DD's REMOVAL CONTRACT, written out in
    // zadLibs.psc:11-12: an unmarked device "can be removed by any DD mod at
    // any time for any reason, including helpful blacksmiths, mercyful NPCs",
    // while a marked one "should NOT be removed by third party mods via
    // trivial means of escape (e.g. blacksmith dialogues)". DD enforces it in
    // five places in zadEquipScript. 54 devices carry it.
    //
    // ★ It belongs beside zad_QuestItem because it answers the same question
    // an NPC would actually act on - how hard is this to get out of - and it
    // is the softer half of that pair: a quest item answers no key at all,
    // this one simply will not come off by asking someone nicely.
    // ⚠ Suppressed when zad_QuestItem is also present, which is strictly
    // stronger and already says so.
    if (HasKw(w, "zad_BlockGeneric") && !HasKw(w, "zad_QuestItem"))
        s += ", and it is not the sort of thing a blacksmith or a passing stranger will take off";

    // ★ A LOCK THAT ANSWERS NO KEY. zad_QuestItem marks a device DD will not
    // release to any key at all. Without it the LLM can have an NPC promise an
    // unlock that DD then silently refuses - the same enforced-but-invisible
    // failure the belt rule had.
    if (HasKw(w, "zad_QuestItem"))
        s += ", and its lock answers no key";

    // ═══ THE ZaZ / DoM CLAUSES (2026-08-30, the incorporation) ═══════
    // ★ ZaZ states removability ON THE RECORD - richer than DD (§0.6 step
    // 3) - and it is exactly what an NPC would act on. One clause, by the
    // strongest marker present. DD records carry none of these; nothing
    // changes for them.
    if (HasKw(w, "zbfWornScrewed"))
        s += ", and it is screwed shut - tools would open it, not a key";
    else if (HasKw(w, "zbfWornLockable"))
        s += ", and it is locked on";
    else if (HasKw(w, "zbfWornManualRemovable"))
        s += ", and it could be worked off by hand";
    else if (HasKw(w, "zbfWornSliceable"))
        s += ", and it would have to be cut away";
    return s;
}

// ★★ WHAT AN ONLOOKER SEES (the user, 2026-09-12). The observer view used to be
// the wearer's FULL composition with only the access line dropped - so a bystander
// was told "the ribs have begun to ache", "the skin under it has stayed damp and
// has not dried", "hearing and touch have taken over the work of seeing": things
// only the WEARER can feel. The user: something simple, "(name) hook to her
// Nipple/Clitoris. That simple. All gears should have a similar simple
// description too."
// ★ THE PRINCIPLE: AN OBSERVER SEES, NEVER FEELS. Name + where + the visible
// posture; no strain, no damp, no slow/sensation line, no wear time (a bystander
// cannot know how long she has worn it), no material register.
// ⚠ NO PRONOUNS, per the standing rule - the phrases are written with "the".
// ⚠ This renders under "### What you can see they are wearing" in the WATCHING
// NPC's own prompt, so a bullet must never open with a name: the reader is the
// observer, and naming them would describe them to themselves.
static const char* ObserverLine(const char* cls, const char* label, RE::TESObjectARMO* w,
                                const char* fallback, bool rope)
{
    if (IsTailPlug(cls, label, w))      return "a long tail hanging from beneath the tailbone";
    if (_stricmp(cls, "Hood") == 0) {
        if (IsExtremeHoodOpen(label))    return "enclosing the head, with holes left for the eyes";
        if (IsExtremeHood(label) || ContainsNoCase(label, "Blindfold"))
                                         return "covering the eyes";
    }
    // ★ 2026-09-13 (the user: "fix"): the six Scold's Bridles are class Gag, so the Gag row told an
    // onlooker "filling the mouth" - what shows is a hinged iron cage locked around the whole head.
    // The bit or tongue grip inside it is not what a bystander sees first.
    if (_strnicmp(cls, "Gag", 3) == 0 && ContainsNoCase(label, "Scold"))
        return "a hinged iron cage locked around the whole head";
    // ★ 2026-09-13: the three device-level rulings DeviceWhat applies, as an onlooker sees them.
    if (HasKw(w, "zadNG_DeviousYokeFront"))
        return "a yoke locking the neck and wrists, the hands held up in front";
    if (HasKw(w, "zbfAnimHandsAroundNeck"))
        return "the wrists tied to a rope around the neck";
    if (_stricmp(cls, "Collar") == 0 && HasKw(w, "zad_DeviousPiercingsNipple"))
        return "locked around the neck, a chain running down to the nipples";
    // ★ 2026-09-13 (the user on row 1545 Hand Cuffs Backside: "shackles, behind the back - add info as
    // such"). The cuff rows only said where the band sits, so an onlooker was never told that the hands
    // are held behind the back - which is the most visible thing about them. Posture from RestraintOf,
    // the same answer the wearer's restraint clause uses (1545, DoM's Wrist Rope and Cuffs Rope, ZaZ's
    // irons and rope cuffs behind the back or in front).
    if (_stricmp(cls, "ArmCuffs") == 0 || _stricmp(cls, "CuffsArms") == 0) {
        const Restraint r = RestraintOf(w);
        if (r == Restraint::HandsBack)
            return rope ? "tying the hands behind the back" : "locking the hands behind the back";
        if (r == Restraint::HandsFront)
            return rope ? "tying the wrists together in front" : "locking the wrists together in front";
    }
    // ★ 2026-09-13: rope is KNOTTED - the same rows RopeClassLine corrects for the wearer.
    if (rope) {
        static const struct { const char* c; const char* seen; } ropeRows[] = {
            { "Collar",     "knotted around the neck" },
            { "ArmCuffs",   "knotted around the arms" },
            { "CuffsArms",  "knotted around the arms" },
            { "LegCuffs",   "knotted around the legs" },
            { "CuffsLegs",  "knotted around the legs" },
            { "CuffsFront", "tying the wrists together in front" },
            { "Belt",       "knotted over the hips and between the legs" },
        };
        for (const auto& r : ropeRows) if (_stricmp(cls, r.c) == 0) return r.seen;
    }
    static const struct { const char* c; const char* seen; } rows[] = {
        { "PiercingsNipple",    "hooked through the nipples" },
        { "PiercingsVaginal",   "hooked through the clitoris" },
        { "Gag",                "filling the mouth" },
        { "GagInflatable",      "filling the mouth" },
        { "GagLarge",           "filling the mouth, the jaw held wide" },
        { "GagRing",            "holding the mouth open around a ring" },
        { "GagBit",             "a bar held between the teeth" },
        { "GagPanel",           "a panel over the mouth" },
        { "GagTape",            "tape across the lips" },
        { "Blindfold",          "covering the eyes" },
        { "Hood",               "enclosing the head" },
        { "Armbinder",          "binding the arms behind the back" },
        { "Butterfly",          "binding the arms behind the back" },
        { "Boxbinder",          "binding the arms behind the back" },
        { "ArmbinderElbow",     "drawing the elbows together behind the back" },
        { "ElbowTie",           "binding the elbows together" },
        { "StraitJacket",       "wrapping the arms across the body" },
        { "Yoke",               "holding the arms out on a bar" },
        { "YokeBB",             "holding the wrists apart on a bar in front of the chest" },
        { "YokeFront",          "locking the neck and wrists in one board" },
        { "HeavyBondage",       "binding the arms" },
        { "CuffsFront",         "locking the wrists together in front" },
        { "BondageMittens",     "encasing the hands" },
        { "Collar",             "locked around the neck" },
        // ⛔ CORRECTED 2026-09-13: these said "around the wrists" / "around the ankles" -
        // the user's 08-30 ruling is that ARM and LEG cuffs are the decorative bands on the
        // mid upper/lower arm and leg (ClassLine already says so); the wrist binder is
        // CuffsFront and the ankle one AnkleShackles, both with their own rows.
        { "ArmCuffs",           "locked around the arms" },
        { "CuffsArms",          "locked around the arms" },
        { "LegCuffs",           "locked around the legs" },
        { "CuffsLegs",          "locked around the legs" },
        { "AnkleShackles",      "linking the ankles" },
        { "HobbleSkirt",        "binding the legs together" },
        { "HobbleSkirtRelaxed", "binding the legs together" },
        { "PetSuit",            "holding the limbs folded, on all fours" },
        { "PonyGear",           "pony gear" },
        { "Corset",             "cinched around the waist" },
        { "Suit",               "covering the body" },
        { "Bra",                "enclosing the breasts" },
        { "Belt",               "locked over the hips" },
        { "Harness",            "straps crossing the body" },
        { "Boots",              "on the feet" },
        { "Gloves",             "on the hands" },
        { "Clamps",             "clamped onto the flesh" },
    };
    for (const auto& r : rows) if (_stricmp(cls, r.c) == 0) return r.seen;
    // ⚠ A class with no row falls back to the wearer's class line rather than
    // saying nothing - but every class ClassLine knows has a row above, and a
    // plug never reaches here (VisibleOn hides internals).
    return fallback;
}

// ★★ D1 - THE DEVICE DETAIL FOR A HAND EQUIP (2026-09-13). Since VRTouchEvents narrates the
// ACT of every hand equip itself ("Telord firmly put Iron Collar on Carmella's neck."), the
// device-specific part - what it does, how it is held on, and what the people nearby saw - is
// ours again (VRTE request UPDATE 3 D1; the user: "accept the gap, and pass it to the AddOn").
// This composes it; VRTEDD_Narrate.DeviceFitted delivers it as AWARENESS (a persistent event for
// her, short-lived events for onlookers), never a second DirectNarration - VRTE's line already
// asks her for the one spoken reaction.
// ★ ONE VOCABULARY: `detail` is DeviceWhat (the worn block's own mechanism line, every ruling
// included) + the removal clauses the worn block uses; `seen` is ObserverLine. Nothing here is a
// third copy of a device table.
// `locked` is PPB's (it reads BOTH halves of a DD pair); the worn block never states a plain DD
// lock, which is the one fact this event adds - as VRTE's old wearer line did.
// False = nothing to say: no covered device on those slots, a plug (its own lane), or an
// unmapped class (the worn block is silent on those too).
bool FittedLines(RE::Actor* a, std::uint32_t slotMask, const char* heldName, bool locked,
                 std::string& label, std::string& detail, std::string& seen)
{
    label.clear();
    detail.clear();
    seen.clear();
    if (!a || !slotMask) return false;

    // The record PPB just confirmed worn. PPB reports the RENDERED half's mask for a DD pair and
    // the record's own for ZaZ/DoM, so an exact mask match is the device; overlap is a fallback.
    // ⚠ not "near" - windows.h defines near/far as empty macros.
    RE::TESObjectARMO* dev     = nullptr;
    RE::TESObjectARMO* overlap = nullptr;
    ForEachWorn(a, [&](RE::TESObjectARMO* w) {
        if (!IsCoveredDevice(w)) return true;
        const auto m = static_cast<std::uint32_t>(w->GetSlotMask().underlying());
        if (m == slotMask) { dev = w; return false; }
        if (!overlap && (m & slotMask)) overlap = w;
        return true;
    });
    if (!dev) dev = overlap;
    if (!dev || IsPlugClass(dev)) return false;

    char cls[64];
    ClassOf(dev, cls, sizeof cls);
    // The held (inventory) half's real name from PPB first - the rendered half is nameless.
    if (heldName && heldName[0]) label = heldName;
    if (label.empty()) label = NameForDevice(a->GetFormID(), dev, cls);
    if (label.empty()) label = NounOf(cls, "");
    CleanDeviceName(label);
    ReclassByName(cls, sizeof cls, label.c_str());

    const bool rope = IsRopeDevice(dev, label.c_str());
    std::string what = DeviceWhat(dev, cls, label.c_str(), false, rope);
    if (what.empty()) return false;
    // ★ 1.3.5 WRIST CUFFS SAY WRISTS, AND WHERE THE HANDS ARE (the user, 2026-09-14, on Copper Wrist Cuffs
    // Front that she treated as if her hands were behind her: "it's something she said, the device were at
    // the right location"). This detail came from the ArmCuffs class row alone - "locked around the arms" -
    // with no posture; the worn block adds its posture clause separately, but THIS line is the one SkyrimNet
    // keeps in her history. The posture is RestraintOf's, the same answer the worn block and ObserverLine use.
    if (_stricmp(cls, "ArmCuffs") == 0 || _stricmp(cls, "CuffsArms") == 0) {
        const Restraint r = RestraintOf(dev);
        if (r == Restraint::HandsFront)
            what = rope ? "tied around the wrists, holding them together in front"
                        : "locked around the wrists, holding them together in front";
        else if (r == Restraint::HandsBack)
            what = rope ? "tied around the wrists, holding them behind the back"
                        : "locked around the wrists, holding them behind the back";
    }

    detail = what;
    if (rope) detail += ", made of rope rather than leather or metal";
    if (locked) {
        // ⚠ A knot is not a lock (the user on "Black Rope Neck Binds", 2026-08-27) - VRTE's
        // V3DDHeldOn wording. And a line that already says "locked"/"locks" is not told twice.
        if (rope)
            detail += ", and the knots are pulled tight enough that they will not come undone by hand";
        else if (!ContainsNoCase(what.c_str(), "lock"))
            detail += ", and it is locked on";
    }
    detail += RemovalClauses(dev);

    // An onlooker is told only what could be seen (the observer principle) - same VisibleOn test
    // as the 0950 block, so a watcher never learns more from the event than from looking.
    if (VisibleOn(a, dev)) seen = ObserverLine(cls, label.c_str(), dev, what.c_str(), rope);
    return true;
}

// ⚠ ONE composition, two views - never a forked copy. `observerView` filters
// the device walk through VisibleOn and drops the access line; tiers, grouping
// and the cap are the same code, so the two can never drift the way a duplicate
// would. ★ EXCEPT THE BULLET ITSELF (2026-09-12): an observer's bullet is built
// by ObserverLine above and carries no effect sentences - see why there.
std::string Report(RE::Actor* a, bool observerView)
{
    if (!a) return {};
    const float now  = GameDaysNow();
    const std::uint32_t afid = a->GetFormID();

    const bool vibrating = VibratorFaction() && a->IsInFaction(VibratorFaction());

    // ⚠ Computed ONCE per actor, not per device: it walks every worn piece, so
    // doing it inside the per-device loop would make the report quadratic in the
    // number of devices on a fully-kitted NPC.
    const Covers cov = CoversOf(a);

    std::vector<Entry> out;
    std::vector<std::uint32_t> seen;

    ForEachWorn(a, [&](RE::TESObjectARMO* w) {
        // ★ WIDENED 2026-08-30 (the incorporation): DD, ZaZ and DoM all pass.
        if (!IsCoveredDevice(w)) return true;

        // ★ THE OBSERVER VIEW (2026-08-30): what someone ELSE can see on her.
        // The wearer's own report is unfiltered - she knows what is inside her.
        if (observerView && !VisibleOn(a, w)) return true;

        char cls[64];
        ClassOf(w, cls, sizeof cls);
        const char* what = ClassLine(cls);
        if (!what) return true;                     // an unmapped class says nothing

        const std::uint32_t dfid = w->GetFormID();
        // de-dupe on the FORM. ⚠ One ARMO answers on every biped slot it
        // occupies, so a belt covering three slots arrives three times. The
        // first version of this compared against `rank` - which is overwritten
        // below with the strain score - so exactly the devices that matter most
        // were the ones that could list twice.
        if (std::find(seen.begin(), seen.end(), dfid) != seen.end()) return true;
        seen.push_back(dfid);

        bool backfilled = false;
        const float hrs = (std::max)(0.0f, SinceHours(afid, dfid, now));

        // NAME: the worn half of a DD pair has no FULL record, so fall back to
        // the class noun. Never print an empty name (report 23 §33 §4).
        // Pushed inventory-half name first; the worn record's own name only as
        // a fallback for non-DD gear, which does carry one.
        std::string label = NameForDevice(afid, w, cls);
        if (label.empty()) label = cls;
        CleanDeviceName(label);    // "zbf Ankle Iron 2 Black" must not print raw

        // ★ name-keyed class overrides (2026-08-30) - #299 reads as a collar.
        // The line, the tier and the layer all follow the corrected class, so
        // re-derive `what` when the class moved.
        {
            char was[64]; std::snprintf(was, sizeof was, "%s", cls);
            ReclassByName(cls, sizeof cls, label.c_str());
            if (_stricmp(was, cls) != 0) {
                what = ClassLine(cls);
                if (!what) return true;
            }
        }

        // ⚠ HOISTED above the tier selection (2026-08-27): rope is a MATERIAL that
        // overrides the class's strain tier, so it has to be known before the
        // tier is chosen rather than at the point the clause is written.
        // ⚠ And it goes THROUGH the wear record, so the answer survives a reload
        // even when the inventory-half name that produced it does not.
        bool rope = false;
        EnsureStamped(afid, dfid, now, backfilled, IsRopeDevice(w, label.c_str()), rope);

        // ★ 2026-08-30: the ZaZ squeeze family joins the clamp mechanism the
        // user ruled for it - the ball devices ("Definitely for pain") and the
        // breast ropes ("Same as the scrotum one... Pain scale for sure"; the
        // §0.6c note reuses the clamp tier unchanged rather than inventing a
        // rate). Both resolve class "Clamps" in the second resolver.
        const auto wslots = static_cast<std::uint32_t>(w->GetSlotMask().underlying());
        const bool zazBall   = HasKw(w, "zbfWornGag") && (wslots & (1u << 22));
        const bool breastRope = HasKw(w, "zbfWornBreastRope");
        const bool clamp = IsClampDevice(cls, label.c_str()) || zazBall || breastRope;
        const StrainTier t  = TierOfDevice(w, cls, rope, clamp,
                                          IsSoftFootwear(cls, label.c_str()));
        const int       ph  = PhaseOf(t, hrs, CeilingFor(cls, t, label.c_str()));

        bool magical = false;
        const char* mat  = MaterialLine(label.c_str(), magical);
        const char* pain = StrainLine(t, ph);
        const char* slow = SlowLine(cls, hrs);

        // The mechanism line and every override on it - one home, see DeviceWhat.
        const std::string whatLine = DeviceWhat(w, cls, label.c_str(), observerView, rope);
        what = whatLine.c_str();

        // ★★ THE OBSERVER BULLET (2026-09-12) - name + what is visible, nothing felt.
        // Built here and pushed with EMPTY pain/damp, so the effect grouping below
        // skips it and the nerve footnote can never fire on a bystander's list. The
        // rank is the same formula as the wearer's, so the cap keeps the same order.
        if (observerView) {
            std::string so = "- " + label + ", " +
                             ObserverLine(cls, label.c_str(), w, what, rope);
            const int orank = ph * 100 + (magical ? 10 : 0) + static_cast<int>(dfid & 0x07);
            out.push_back({ so, "", "", NounOf(cls, label.c_str()), orank, false });
            return true;
        }

        std::string s = "- " + label + " (" + HoursText(hrs, backfilled) + "): " + what;
        if (mat)  { s += " "; s += mat; }

        // ★ WHAT IT DOES, for the classes whose name does not say (2026-08-27).
        // Behaviour is read from DD's own keywords, never from the name or the
        // material: gold cuffs that bind say so, rope cuffs that do not bind say
        // that instead. See RestraintOf for the evidence.
        // ★ ROPE, said plainly (2026-08-27). "the rope classifier so the LLM know
        // it's not some leather or iron cuff, but some rope put on them". A rope
        // wrist tie and an iron manacle restrain identically and read to an NPC
        // completely differently, and nothing else in the block distinguishes
        // them - MaterialLine only knows the soulgem ladder.
        bool ropeSurface = false;
        if (rope) {
            s += ", made of rope rather than leather or metal";
            // ★ THE ROPE REGISTER, and it is a MECHANISM rather than a mood
            // (user, 2026-08-27: "the tone for all rope gears should be less
            // toward pain and more toward fetish ... we don't dictate feeling,
            // mood or want, just describe fact and physical effect").
            //
            // The fact that carries it: a wide wrap laid in many turns spreads
            // its load over a broad band of skin, where a rigid band concentrates
            // the same tension on one narrow line. That is a real difference in
            // contact pressure, and it is why a torso rope can be worn for hours
            // that a metal frame could not.
            //
            // ⚠ NARROW ON PURPOSE - "don't over do it". Only for a rope that is
            // NOT binding the hands, and only on the two whole-body classes. A
            // rope armbinder still binds and keeps the ordinary curve; the user
            // exempted the wrist family explicitly. Without the class test this
            // clause would also land on every rope gag, collar and blindfold,
            // which it has no business describing.
            if (RestraintOf(w) == Restraint::None &&
                (_stricmp(cls, "Harness") == 0 || _stricmp(cls, "Suit") == 0)) {
                s += ", laid in many turns so the load is spread over a wide band of skin";
                ropeSurface = true;
            }
        }

        // ★ THE BELT IS WHAT LOCKS A PLUG IN. Said on the PLUG's line rather than
        // the belt's, because the plug is the thing that cannot come out - that is
        // the fact an NPC would act on.
        //
        // ⛔ AND THE FIRST VERSION OF THIS, WRITTEN AN HOUR EARLIER, WAS TOO BROAD.
        // It claimed the belt held ANY plug. DD's own condition, verbatim from
        // zadEquipScript.psc:436:
        //     npc.WornHasKeyword(zad_DeviousBelt) && (
        //          Rendered.HasKeyword(zad_DeviousPlugVaginal)
        //       || (Rendered.HasKeyword(zad_DeviousPlugAnal)
        //           && !npc.WornHasKeyword(zad_PermitAnal)))
        // So a VAGINAL plug is always held, but an ANAL one comes out freely if
        // anything she wears carries zad_PermitAnal - and 19 belts in this load
        // order do. The clause was therefore stating, as fact, that a plug could
        // not be removed when DD would hand it over without complaint.
        //
        // ⚠ A DOUBLE plug is still held by its vaginal half even under a
        // PermitAnal belt, which falls out of the condition rather than needing a
        // case of its own.
        const int plugSites = PlugSitesOf(w);
        if (plugSites != 0 && cov.belt) {
            const bool heldVag = (plugSites & 1) != 0;
            const bool heldAnl = (plugSites & 2) != 0 && !cov.permitAnal;
            if (heldVag || heldAnl)
                s += ", and the belt locked over it is holding it in";
        }

        // ★ THE SAME RULE ON THE CHEST. A worn chastity bra covers the nipples, so
        // a nipple piercing can be neither put in nor taken out while it is on -
        // the exact structural twin of the belt rule, and leaving it out meant the
        // block told the truth about the hips and stayed silent about the chest on
        // the same body.
        //
        // ⚠ SELF-BLOCKING GUARD. Two devices - "Iron Chain Harness" and its rusty
        // twin - carry zad_DeviousBra AND zad_DeviousPiercingsNipple on the SAME
        // rendered record. Without this test the line would say the device is
        // holding itself in place, which is nonsense; a piece that IS both is not
        // covered by anything.
        if (cov.bra && HasKw(w, "zad_DeviousPiercingsNipple") &&
            !HasKw(w, "zad_DeviousBra"))
            s += ", and the bra locked over them is holding them in";

        // ★ IT IS ALSO A CHASTITY BELT (2026-08-27, the user on #129-139): "corset
        // with chastity belt. it need to have the chastity property, so locking
        // plug in place."
        //
        // ★ THE LOCKING HALF ALREADY WORKED - verified before changing anything.
        // CoversOf walks every worn piece for zad_DeviousBelt, and these records
        // carry it, so a plug under one was already being held and SealedLine
        // already reported the hips sealed. What was missing is that the DEVICE'S
        // OWN LINE never said so: ClassOf picks ONE class, Corset (rank 1) beats
        // Belt (rank 5), and ClassLine("Corset") talks only about the ribs. The
        // LLM was told she wore a corset and had to infer the rest from a
        // separate line.
        //
        // ⚠ 102 DEVICES, not the 14 the user spotted: 88 harnesses ("Bondage
        // Harness with Crotch Plate") carry the same keyword under class Harness.
        // ⚠ Suppressed when the class IS Belt - ClassLine already says it there.
        // ⚠ AND IT SUPPRESSES THE ORNAMENT CLAUSE, third member of that family
        // after ropeSurface and posture. Without this #129 read "...cinched
        // tight around the ribs ... and it locks closed over the hips as well,
        // covering everything beneath, and it fastens to nothing and leaves the
        // body free to move" - a device cannot lock over the hips AND fasten to
        // nothing. Caught by re-reading the composed output rather than by
        // reasoning about the change.
        bool beltComposite = false;
        if (_stricmp(cls, "Belt") != 0 && HasKw(w, "zad_DeviousBelt")) {
            beltComposite = true;
            // ★ THE ROPE VARIANT (2026-08-30, the user): "rope are not lock but
            // can be tightly knot, and the rope is passing inside the loop each
            // plug have, making them not removable, so it is binding. 'Lock
            // closed over the hips' make no sense thou, change it for knot
            // tight." 72 rope harnesses said "locks closed" about knots.
            if (rope)
                s += ", and the ropes knot tight over the hips as well, threaded through the loop of any plug so it cannot be drawn out";
            else
                s += ", and it locks closed over the hips as well, covering everything beneath";
        }

        // ★ IT BINDS THE LEGS TOO (2026-08-28). 58 straitjackets carry
        // zad_DeviousHobbleSkirt / zad_BoundCombatDisableKick /
        // zad_EffectForcedWalk alongside their arm keywords - DD says the legs
        // are bound - but ClassOf picks ONE class, StraitJacket wins, and the
        // line described only the arms. StraitJacket is not in ClassIsAmbiguous
        // either, so RestraintOf's Movement answer never printed: the leg half
        // was invisible on every one of them.
        // ⚠ Gated to ARM classes so it cannot double up on a hobble skirt, whose
        // own class line already says it.
        {
            static const char* armCls[] = { "StraitJacket", "Armbinder",
                "ArmbinderElbow", "ElbowTie", "Boxbinder", "HeavyBondage",
                "Yoke", "YokeBB", "YokeFront" };
            bool isArm = false;
            for (auto* a2 : armCls) if (_stricmp(cls, a2) == 0) { isArm = true; break; }
            if (isArm && (HasKw(w, "zad_DeviousHobbleSkirt") ||
                          HasKw(w, "zad_BoundCombatDisableKick") ||
                          HasKw(w, "zad_EffectForcedWalk")))
                s += ", and it binds the legs as well, cutting every step short";
        }

        // ★ 2026-09-12: THE LEG-GRADE BOOT'S GAIT (the user's 09-11 ruling: "feet forced
        // into position, careful unsteady steps, pace not reduced"). These boots left the
        // legs restraint STATE today, because that state's prompt says she moves "at a slow
        // shuffle" - which contradicts "pace not reduced" - so without this the leg grade
        // would lose its movement fact altogether.
        // ⛔ A CLAUSE, NOT AN APPEND TO `what`. It was first joined onto the mechanism line,
        // and the replay caught the result: "rides on the toes, SO every step ... though
        // the pace is not cut AND the iron is cold" - a double "so", and MaterialLine's
        // "and ..." welded onto the gait. Every static fact here reads ", and it <does X>",
        // after the material, so it now follows that pattern like its legs sibling above.
        if (BootGradeOf(cls, label.c_str()) == BootGrade::Leg)
            s += ", and it forces every step to be careful and unsteady, though the pace is not cut";

        // ★ IT LEAVES THE BREASTS BARE (2026-08-28). `zad_ExposedBreasts` is
        // DD's own marker for a garment cut to leave the chest out, and it is
        // on 171 records - 129 harnesses, 33 straitjackets, 6 suits, 2 hobble
        // dresses and the Iron Breast Yoke - while the model used it NOWHERE.
        // Found on #452, which the user asked about because it looked strange;
        // the strangeness was a real keyword nobody was reading.
        //
        // ⚠ NOT redundant with SealedLine's breast test, which is its exact
        // OPPOSITE: that reports a bra or suit sealing the chest OFF, this
        // reports a garment deliberately leaving it OUT. A device can be neither
        // and most are.
        // ⚠ Physical fact only - what the garment does and does not cover. What
        // being uncovered means is the LLM's to decide.
        if (HasKw(w, "zad_ExposedBreasts"))
            s += ", and it is cut to leave the breasts bare";

        // ★ IT IS SET NEVER TO LET HER FINISH (2026-08-28). DD documents the
        // keyword itself at zadLibs.psc:359 - "Will never let the player cum for
        // stimulating events (Vibration)" - and USES it in ShouldEdgeActor()
        // (:2191), which every stimulating event consults. So the device
        // deliberately stops short, every time.
        //
        // ⛔ THE MODEL READ NEITHER OF THESE, and that is worse than a missing
        // detail: our own climax lane could narrate a finish that DD would have
        // refused. Found because the user asked what was special about #1130
        // "Laura's Teaser". 4 devices carry EdgeOnly (both Black Soulgem plugs
        // and Laura's Teaser among them); EdgeRandom is the 25%-of-the-time
        // version.
        // ⚠ Mechanism only - what the device is set to do. What being denied is
        // like is the LLM's to write.
        if (HasKw(w, "zad_EffectEdgeOnly"))
            s += ", and it is set to stop short of a finish every time";
        else if (HasKw(w, "zad_EffectEdgeRandom"))
            s += ", and it stops short of a finish more often than not";

        // DD's removal contract and ZaZ's removability - one home, see RemovalClauses.
        s += RemovalClauses(w);

        // ★ A DEVICE THAT STOPS CASTING (§0.6 step 4) - the State DD has no
        // equivalent for, and ZaZ genuinely delivers it (ApplySilenceEffect
        // unequips her spells). The user: "yes, gag prevent casting."
        if (HasKw(w, "zbfEffectNoMagic"))
            s += ", and it smothers casting - no spell will form while it is worn";

        // ★ THE LEASH COLLAR (round 9, the user: the enchantment "mostly to
        // prevent slave from escaping. This should be in the description").
        if (HasKw(w, "DOMWornCollarLeash") || HasKw(w, "DOMLeash"))
            s += ", and the enchantment worked into it is made to keep a slave from straying";

        // ★ THE POSTURE COLLAR (user, 2026-08-27). There is no
        // zad_DeviousPostureCollar - it is class Collar like any other, and only
        // the NAME distinguishes it (17 devices here). The plain collar line
        // describes the wrong mechanism: a posture collar is not about closing on
        // the throat, it holds the head up and stops it turning.
        // ⚠ It also suppresses the ornament clause below, which would otherwise
        // say it "leaves the body free to move" of a device whose entire purpose
        // is that it does not.
        // ★ EXTENDED from "Posture" to every TALL collar (2026-08-27): the user
        // on #251/#255, "they restric the head movement". A Restrictive Collar
        // is the same device by another name - 8 records, all on
        // restrictiveCollar_go.nif - and it was getting the ornament clause
        // ("fastens to nothing and leaves the body free to move") instead.
        bool posture = false;
        if (IsTallCollar(cls, label.c_str())) {
            posture = true;
            s += ", holding the head up and stopping it turning to either side";
        }

        const Restraint rst = RestraintOf(w);
        // ⛔ AND NOT WHEN THE ROPE SURFACE CLAUSE ALREADY FIRED. Both are gated on
        // RestraintOf == None, so on a rope harness they ALWAYS both fired - and
        // they contradict: one says the load is spread over a wide band of skin,
        // the next says it "fastens to nothing and leaves her free to move". The
        // rope clause already describes what the thing does, so the ornament line
        // is redundant as well as wrong there.
        if (ClassIsAmbiguous(cls) && !ropeSurface && !posture && !beltComposite) {
            const char* rl = RestraintLine(rst);
            if (rl[0]) { s += ", and "; s += rl; }
            else       { s += ", and "; s += OrnamentLine(); }
        }
        // ⚠ These tables are written lowercase so they can be appended mid
        // sentence, so the first letter has to be raised when one STARTS a
        // sentence. Without this the block reads "...behind the back. a deep
        // ache has settled..." on every strained device.
        // ★ THE EFFECT SENTENCES ARE HELD OUT OF THE LINE (2026-08-28) so the
        // grouping pass below the sort can decide whether each is unique (it
        // rejoins its own bullet, output unchanged) or shared (printed once,
        // enumerating its wearers). See the Entry struct for the design.
        // ⚠ pain-or-slow stays an either/or, exactly as appendSentence had it.
        std::string fxPain = pain ? pain : (slow ? slow : "");

        // ★ THE DAMP AXIS, INDEPENDENT of the strain one - a leather posture
        // collar chafes AND seals, and it should say both. Its own sentence, so
        // it cannot collide with whatever the strain clause said.
        // ⚠ FULL coverage reuses the SUIT TIER'S OWN WORDING rather than a
        // parallel table, so a sealed garment reads identically whether its
        // class token happened to be Suit or StraitJacket. One source of truth
        // for the thermal scale; DampLine keeps the shorter partial register.
        std::string fxDamp;
        if (const int occl = OcclusionOf(w, cls, label.c_str())) {
            const int dph = DampPhaseOf(hrs, occl);
            const char* dl = (occl >= 2) ? StrainLine(kStrainSuit, dph)
                                         : DampLine(dph);
            if (dl) fxDamp = dl;
        }
        if (vibrating && (HasKw(w, "zad_EffectVibrating") ||
                          HasKw(w, "zad_EffectVibratingWeak") ||
                          HasKw(w, "zad_EffectVibratingVeryWeak") ||
                          HasKw(w, "zad_EffectVibratingStrong") ||
                          HasKw(w, "zad_EffectVibratingVeryStrong") ||
                          HasKw(w, "zad_EffectVibratingRandom")))
            s += ". It is running right now";

        // Sort key: strain phase dominates, then anything magical/active, then
        // plain gear. A capped list must drop the boring rows, not the urgent one.
        //
        // ⛔ AND IT DID EXACTLY THE OPPOSITE UNTIL 2026-08-27. The fallback for an
        // unstrained device was `dfid & 0x7FFFFFF` - a FormID, so tens of
        // millions - while a maximally strained device caps at 5*100 + 10 = 510.
        // The list sorts DESCENDING, so every plug, gag, piercing and blindfold
        // outranked a phase-5 armbinder. On the fully-kitted NPC the kMax cap
        // exists for, the six lines printed were ALL untiered filler and every
        // strain line was truncated away - the precise inversion of the sentence
        // directly above this one.
        //
        // ★ The fallback is now 0-7, so it can only ever break ties WITHIN an
        // equal phase, never cross the magical bump (10) or the phase band (100).
        int rank = ph * 100 + (magical ? 10 : 0) + static_cast<int>(dfid & 0x07);
        out.push_back({ s, fxPain, fxDamp, NounOf(cls, label.c_str()), rank,
                        ph >= 5 && (t == kStrainArms || t == kStrainLimb ||
                                    t == kStrainClamp || t == kStrainMitt) });
        return true;
    });

    if (out.empty()) return {};

    std::stable_sort(out.begin(), out.end(),
                     [](const Entry& x, const Entry& y) { return x.rank > y.rank; });

    // ⚠ TOKEN CEILING. Six lines is roughly 90 tokens; an NPC in a full set can
    // wear a dozen pieces and there is no value in listing every strap. The tail
    // is counted, never silently dropped.
    constexpr std::size_t kMax = 6;
    const std::size_t nShow = (std::min)(out.size(), kMax);

    // ── the grouping pass: count each effect sentence across the SURVIVORS ──
    std::unordered_map<std::string, int> fxCount;
    for (std::size_t i = 0; i < nShow; ++i) {
        if (!out[i].pain.empty()) ++fxCount[out[i].pain];
        if (!out[i].damp.empty()) ++fxCount[out[i].damp];
    }
    auto appendFx = [](std::string& line, const std::string& fx) {
        line += ". ";
        const std::size_t at = line.size();
        line += fx;
        line[at] = static_cast<char>(std::toupper(
            static_cast<unsigned char>(line[at])));
    };

    std::string body = "### Devices worn\n";
    bool nerveBlock = false;
    for (std::size_t i = 0; i < nShow; ++i) {
        std::string line = out[i].line;
        // A sentence only this device produces goes back on its own bullet -
        // byte-identical to the pre-grouping output.
        if (!out[i].pain.empty() && fxCount[out[i].pain] == 1) appendFx(line, out[i].pain);
        if (!out[i].damp.empty() && fxCount[out[i].damp] == 1) appendFx(line, out[i].damp);
        body += line + ".\n";
        if (out[i].nerve) nerveBlock = true;
    }

    // Shared sentences, once each, in first-appearance order, enumerating the
    // devices that produce them. ⚠ Nouns are de-duplicated: two devices with
    // the same handle ("the arm cuffs" twice) are one physical statement.
    std::vector<std::string> done;
    for (std::size_t i = 0; i < nShow; ++i) {
        for (const std::string* fx : { &out[i].pain, &out[i].damp }) {
            if (fx->empty() || fxCount[*fx] < 2) continue;
            if (std::find(done.begin(), done.end(), *fx) != done.end()) continue;
            done.push_back(*fx);
            std::vector<std::string> nouns;
            for (std::size_t k = 0; k < nShow; ++k)
                if ((out[k].pain == *fx || out[k].damp == *fx) &&
                    std::find(nouns.begin(), nouns.end(), out[k].noun) == nouns.end())
                    nouns.push_back(out[k].noun);
            std::string who;
            for (std::size_t k = 0; k < nouns.size(); ++k) {
                if (k) who += (k + 1 == nouns.size()) ? " and " : ", ";
                who += nouns[k];
            }
            if (!who.empty()) who[0] = static_cast<char>(std::toupper(
                static_cast<unsigned char>(who[0])));
            // ⚠ "alike" needs at least two enumerated handles. When the noun
            // dedupe collapses a pair of same-class devices to one handle
            // ("the arm cuffs" twice), the plain colon reads correctly and
            // "alike" would not. Caught by the pre-ship simulation, test 4.
            body += "- " + who + (nouns.size() >= 2 ? " alike: " : ": ")
                  + *fx + ".\n";
        }
    }

    if (out.size() > kMax) {
        char t[64];
        std::snprintf(t, sizeof t, "- and %zu more piece(s) of locked gear.\n",
                      out.size() - kMax);
        body += t;
    }

    // ★ THE NON-MONOTONE RULE. Stated once, only when it can apply, because it
    // ⚠ TRUE ONLY of the two pain tiers that actually reach nerve conduction
    // block. The suit tier also reaches phase 5, but its last phase is skin
    // softening under prolonged wet - no numbness, no failing sensation - so
    // appending this for a catsuit would be a flat false statement about her
    // body. Yoke and rope cap at 4 and never reach it at all.
    // ⚠ And it is decided from the SURVIVING lines, above, not from every worn
    // device: a footnote saying "the most serious state on this list" must not
    // outlive the line it points at.
    // is a rule about how to READ phase 5 and not a fact about her body. Phase 5
    // reports LESS than phase 4 while the damage is worst — a reader who takes
    // the drop at face value concludes she is recovering at the exact moment
    // that is most wrong.
    // ★ The access line goes AFTER the device list: it is a fact about the BODY
    // rather than about any one device, and it is the summary a reader wants once
    // they have seen what is on her.
    // ⚠ NOT in the observer view. "Sealed by what is worn: the mouth, the vagina
    // and the anus" is a statement about ACCESS TO HER BODY - the wearer knows
    // it; a bystander who can see a belt has not thereby been told what it
    // seals. The visible devices' own lines already say what is on her.
    if (!observerView) body += SealedLine(a);

    if (nerveBlock)
        // ⚠ THE RANKING CLAUSE IS GONE. It used to end "and it is the most serious
        // state on this list", which was defensible while only the arms and limb
        // tiers could reach phase 5. A numbed nipple clamp beside a phase-4
        // armbinder makes that ranking flatly false, and the rule does not need
        // it: the load-bearing half is that a FALLING complaint is not an
        // improving one.
        body += "(Where a report has gone numb or quiet, sensation is failing there "
                "rather than easing.)\n";

    return body;
}


// ─────────────────────────────────────────────────────────────────────────────
// SKSE co-save. Without this the clock resets on every load and every device
// reads as freshly applied — which would make the whole feature lie after the
// first reload rather than fail loudly.
// ─────────────────────────────────────────────────────────────────────────────
void OnSave(SKSE::SerializationInterface* intf)
{
    if (!intf->OpenRecord(kRecordType, kVersion)) {
        logger::warn("[WORN] could not open the save record - wear times will be lost.");
        return;
    }
    std::scoped_lock lk(g_mtx);
    // 1.2.7: skip a device whose removal is still held (RemovalHeld) - it is physically off. Counts
    // are computed first because the record format writes them ahead of the entries.
    std::uint32_t nActors = 0;
    for (auto& [afid, devs] : g_on)
        for (auto& [dfid, w] : devs)
            if (!RemovalHeld(afid, dfid)) { ++nActors; break; }
    intf->WriteRecordData(&nActors, sizeof nActors);
    for (auto& [afid, devs] : g_on) {
        std::uint32_t n = 0;
        for (auto& [dfid, w] : devs)
            if (!RemovalHeld(afid, dfid)) ++n;
        if (n == 0) continue;
        intf->WriteRecordData(&afid, sizeof afid);
        intf->WriteRecordData(&n, sizeof n);
        for (auto& [dfid, w] : devs) {
            if (RemovalHeld(afid, dfid)) continue;
            intf->WriteRecordData(&dfid, sizeof dfid);
            intf->WriteRecordData(&w.when, sizeof w.when);
            std::uint8_t est = w.estimated ? 1 : 0;
            intf->WriteRecordData(&est, sizeof est);
            std::uint8_t rp = w.rope ? 1 : 0;          // v2
            intf->WriteRecordData(&rp, sizeof rp);
        }
    }
    logger::info("[WORN] saved wear times for {} actor(s).", nActors);
}

void OnLoad(SKSE::SerializationInterface* intf)
{
    std::uint32_t type = 0, version = 0, length = 0;
    while (intf->GetNextRecordInfo(type, version, length)) {
        if (type != kRecordType) continue;
        // ⚠ v1 IS STILL READ. Discarding it would throw away every wear time in
        // an existing playthrough to gain one bit; a v1 record simply has no
        // rope flag, and the devices in it read as non-rope until they are
        // re-equipped. Losing a material is recoverable; losing the clock is not.
        if (version != 1 && version != kVersion) {
            logger::warn("[WORN] save record version {} is not 1 or {} - discarding.",
                         version, kVersion);
            continue;
        }
        std::scoped_lock lk(g_mtx);
        g_on.clear();
        std::uint32_t nActors = 0;
        intf->ReadRecordData(&nActors, sizeof nActors);
        for (std::uint32_t i = 0; i < nActors; ++i) {
            std::uint32_t afid = 0, n = 0;
            intf->ReadRecordData(&afid, sizeof afid);
            intf->ReadRecordData(&n, sizeof n);
            // ⚠ FormIDs are per-playthrough: a load order change moves them.
            // ResolveFormID is what stops a stamp landing on a different actor.
            std::uint32_t newA = 0;
            const bool okA = intf->ResolveFormID(afid, newA);
            for (std::uint32_t j = 0; j < n; ++j) {
                std::uint32_t dfid = 0; float when = 0.0f;
                std::uint8_t est = 0, rp = 0;
                intf->ReadRecordData(&dfid, sizeof dfid);
                intf->ReadRecordData(&when, sizeof when);
                intf->ReadRecordData(&est, sizeof est);
                if (version >= 2) intf->ReadRecordData(&rp, sizeof rp);
                std::uint32_t newD = 0;
                if (okA && intf->ResolveFormID(dfid, newD))
                    g_on[newA][newD] = Wear{ when, est != 0, rp != 0 };
            }
        }
        logger::info("[WORN] loaded wear times for {} actor(s).", g_on.size());
    }
}

void OnRevert(SKSE::SerializationInterface*)
{
    // ⚠ EVERY per-playthrough map, not just the two the clock uses. g_names is
    // now read by the equip sink's emit path, so a stale name from the previous
    // save would be attached to a device in the new one; g_gestureClaim holds
    // steady_clock deadlines that mean nothing after a load.
    std::scoped_lock lk(g_mtx);
    g_arousal.clear();          // ⛔ was cleared BEFORE the lock on the next line
    g_names.clear();
    g_gestureClaim.clear();
    g_menuClaim.clear();
    g_menuOpenOn = 0;
    // 1.2.7: held removals belong to the timeline that saw them - drained after a load they would
    // narrate a removal (and close a clock) against a save where it never happened.
    g_pendingOff.clear();
    g_on.clear();
    g_fx.clear();
    g_blindTick.clear();
    g_struggle.clear();         // 1.2.9: a struggle belongs to the timeline that saw it
    g_release.clear();          // 1.3.0: so does a release countdown (the file itself is reset by main.cpp)
    // ★ D6 (2026-09-13): the removal residuals behind 0790/0950 were neither serialized nor
    // cleared, so a quick-load carried "it is off now, and her body is still coming back from
    // it" beside the worn block, and a different save inherited residuals by FormID. Session
    // state, like the claims. g_devNames likewise (the pair map is record data and stays).
    g_after.clear();
    g_devNames.clear();
    // ⛔ MISSED until 2026-08-27 although the comment above says "EVERY
    // per-playthrough map". These are steady_clock deadlines keyed by FormID:
    // loading a different save within 45 s could leave an actor under a
    // leftover cooldown and silently eat one cast trigger.
    // ⚠ Not a race - only CastSink touches it, on one thread - so it is
    // cleared here under the lock purely for uniformity with its neighbours.
    g_castCd.clear();
}

} // namespace WornDevices
