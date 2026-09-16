#include "PCH.h"
#include "DeviceEquip.h"
#include "WornDevices.h"
#include "TaskRelay.h"   // 1.3.6: every task queued from a sink path goes through the relay

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace logger = SKSE::log;

// ========================================================================================
//  VRTE DD-ZaZ ADD-ON - the NARRATION half.
//
//  * THE SPLIT (2026-08-27). The hand-gesture device layer that used to live in this file -
//  equip by pressing a device to a body site, the two-hand undress, the fingertip plug
//  extraction, the HIGGS hand-off - MOVED INTO PPB, whole. It is PPB/src/DeviceGesture.cpp
//  now, and PPB_DeviceEquip.psc is its Papyrus half.
//
//  WHY: the gestures are built entirely on PPB's contact data. Detection belongs with the
//  plugin that owns the sensor; two copies of a gesture detector WILL drift, and silently.
//  The user's framing: the layer "is enabled by PPB and it's quite fitting".
//
//  WHAT IS LEFT HERE, and why each piece stayed:
//    1. THE TRANSLATION SINK. PPB emits generic PPB_Gesture* events. This add-on renames them
//       into the VRTE_DDZaZ_* vocabulary. That indirection is the whole point of the
//       architecture the user set: base VRTouchEvents never learns DD or NSFW concepts, so it
//       stays SFW, and everything that knows what a plug is lives on this side of the line.
//    2. THE RESTRAINT REGISTRY. Which worn gear blocks speech / sight / arms / legs. Pure DD
//       domain knowledge with its own INI, consumed by the SkyrimNet decorators.
//    3. THE DECORATOR REGISTRATION.
//    4. (THE MASTURBATION RELAY - removed 2026-09-13: the user moved the event into base VRTE,
//       "masturbation event are more a general thing than DD specific". Do not re-add it.)
//
//  There is NO dormant copy of the gesture code below. An earlier step of this move left the
//  old implementation in place behind a runtime PpbOwnsGestures() switch; that was 2,400 lines
//  of unreachable duplicate and it is gone, along with the switch.
// ========================================================================================

namespace {

std::atomic<bool> g_armed{ false };
std::atomic<bool> g_paused{ false };

void SendVrteEvent(const char* name, const std::string& strArg, float numArg,
                   RE::TESForm* sender)
{
    auto* src = SKSE::GetModCallbackEventSource();
    if (!src) return;
    SKSE::ModCallbackEvent ev{};
    ev.eventName = name;
    ev.strArg    = strArg.c_str();
    ev.numArg    = numArg;
    ev.sender    = sender;
    src->SendEvent(&ev);
}

// ★★ THE UNDRESS HOLD (2026-09-10, the user: "that's bad, fix that").
// PPB sends PPB_GestureUndressEnd done=1 from FireUndress and only THEN queues the rip, whose
// first act is to ask the Database's removal gate (DeviceGesture.cpp:2545 -> RipPiece). VRTE
// narrates the moment it hears done=1 - so in the 2026-09-10 key test a REFUSED pull told
// SkyrimNet "Telord pulled The Gag off Carmella" 8 ms before the refusal (18:32:20.942 /
// .950), and she kept the gag.
// So a done=1 is HELD until the pull is known to have happened:
//   gate REFUSED or UNKNOWN          -> forward done=0 - nothing came off, VRTE un-mutes
//   gate ALLOWED, not a DD device    -> forward done=1 as sent - a plain unequip cannot fail
//   a DD device                      -> forward done=1 only on PPB_GestureUnlocked, which PPB
//                                       raises once DD has REALLY unlocked it, carrying the
//                                       inventory half's name ("Black Leather Ball Strap Gag"
//                                       instead of "The Gag" - the rendered half has no name)
//   a DD device, no unlock in 4 s    -> forward done=0 - PPB's Papyrus half refused
//   not DD, no verdict in 4 s        -> forward as sent - the gate was never asked (PPB
//                                       without it), which is the old behaviour
// ⚠ ORDER-INDEPENDENT ON PURPOSE. PPB has been asked to send done=1 after the gate; then the
// verdict lands FIRST. Each signal is stamped per actor and resolved whenever enough of them
// are in, so either order ends the same way. A verdict or an unlock counts for a pull only
// if it arrived no more than 1 s before that pull's announcement.
constexpr double kHoldWindowS = 4.0;
constexpr double kHoldLeadS   = 1.0;

double HoldNow()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

struct UndressHold {
    bool          hasEnd      = false;
    std::string   strArg;              // PPB's payload: name|slotMask|done|isDD|part|cls
    float         numArg      = 0.f;
    bool          isDd        = false;
    double        endAt       = 0.0;
    int           verdict     = -1;    // 0 ALLOW / 1 BLOCKED / 2 UNKNOWN; -1 none yet
    double        verdictAt   = 0.0;
    std::uint32_t unlockedInv = 0;     // PPB_GestureUnlocked's INVENTORY FormID
    double        unlockAt    = 0.0;
};
std::mutex                                     g_holdMtx;
std::unordered_map<std::uint32_t, UndressHold> g_holds;     // actor FormID -> the pull in flight

struct HoldForward {
    bool          any      = false;
    std::uint32_t actorFid = 0;
    std::string   strArg;
    float         numArg   = 0.f;
};

std::vector<std::string> SplitBar(const std::string& s)
{
    std::vector<std::string> f;
    std::size_t start = 0;
    for (;;) {
        const std::size_t bar = s.find('|', start);
        f.emplace_back(s.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    return f;
}

std::string JoinBar(const std::vector<std::string>& f)
{
    std::string out;
    for (std::size_t i = 0; i < f.size(); ++i) {
        if (i) out += '|';
        out += f[i];
    }
    return out;
}

// The inventory half's display name, made safe for a |-separated payload.
std::string InvName(std::uint32_t invFid)
{
    auto* form = invFid ? RE::TESForm::LookupByID(invFid) : nullptr;
    const char* n = form ? form->GetName() : nullptr;
    std::string out = (n && n[0]) ? n : "";
    for (char& c : out) if (c == '|') c = '/';
    return out;
}

// Decide under g_holdMtx. `force` resolves now, as if the window had closed.
HoldForward ResolveHoldLocked(std::uint32_t afid, const UndressHold& h, double now, bool force)
{
    HoldForward out;
    if (!h.hasEnd) return out;
    const bool expired     = force || (now - h.endAt) > kHoldWindowS;
    const bool haveVerdict = h.verdict >= 0 && h.verdictAt >= h.endAt - kHoldLeadS;
    const bool haveUnlock  = h.unlockedInv != 0 && h.unlockAt >= h.endAt - kHoldLeadS;
    const auto emit = [&](bool done, const std::string& nameOverride) {
        auto f = SplitBar(h.strArg);
        if (f.size() > 2) f[2] = done ? "1" : "0";
        if (!nameOverride.empty() && !f.empty()) f[0] = nameOverride;
        out.any      = true;
        out.actorFid = afid;
        out.strArg   = JoinBar(f);
        out.numArg   = h.numArg;
    };
    if (haveVerdict && h.verdict != 0) { emit(false, {}); return out; }  // refused: nothing came off
    if (h.isDd) {
        if (haveUnlock) { emit(true, InvName(h.unlockedInv)); return out; } // DD really unlocked it
        if (expired)    { emit(false, {}); return out; }                    // DD never let go
        return out;                                                          // wait for the unlock
    }
    if (haveVerdict) { emit(true, {}); return out; }                        // allowed: plain unequip
    if (expired)     { emit(true, {}); return out; }                        // the gate was never asked
    return out;
}

void SendHeld(const HoldForward& fw)
{
    if (!fw.any) return;
    if (g_paused.load(std::memory_order_relaxed)) return;
    logger::info("[UNDRESS-HOLD] 0x{:08X} -> VRTE '{}'", fw.actorFid, fw.strArg);
    TaskRelay::Add([fw]() {   // 1.3.6: reached from GestureSink (HoldEnd/HoldVerdict/HoldUnlock) - see TaskRelay.h
        RE::TESForm* sender = fw.actorFid ? RE::TESForm::LookupByID(fw.actorFid) : nullptr;
        SendVrteEvent("VRTE_DDZaZ_UndressEnd", fw.strArg, fw.numArg, sender);
    });
}

void HoldEnd(std::uint32_t afid, const std::string& strArg, float numArg, bool isDd)
{
    HoldForward flush, done;
    const double now = HoldNow();
    {
        std::scoped_lock lk(g_holdMtx);
        UndressHold& h = g_holds[afid];
        if (h.hasEnd) {                      // an earlier pull never resolved - close it first
            flush = ResolveHoldLocked(afid, h, now, true);
            h = UndressHold{};
        }
        h.hasEnd = true;
        h.strArg = strArg;
        h.numArg = numArg;
        h.isDd   = isDd;
        h.endAt  = now;
        done = ResolveHoldLocked(afid, h, now, false);
        if (done.any) g_holds.erase(afid);
    }
    if (!done.any)
        logger::info("[UNDRESS-HOLD] 0x{:08X} pull finished ({}) - narration held until the gate{} answers",
                     afid, isDd ? "DD device" : "plain piece", isDd ? " and DD" : "");
    SendHeld(flush);
    SendHeld(done);
}

void HoldVerdict(std::uint32_t afid, int state)
{
    HoldForward fw;
    const double now = HoldNow();
    {
        std::scoped_lock lk(g_holdMtx);
        UndressHold& h = g_holds[afid];
        h.verdict   = state;
        h.verdictAt = now;
        fw = ResolveHoldLocked(afid, h, now, false);
        if (fw.any) g_holds.erase(afid);
    }
    SendHeld(fw);
}

void HoldUnlock(std::uint32_t afid, std::uint32_t invFid)
{
    HoldForward fw;
    const double now = HoldNow();
    {
        std::scoped_lock lk(g_holdMtx);
        UndressHold& h = g_holds[afid];
        h.unlockedInv = invFid;
        h.unlockAt    = now;
        fw = ResolveHoldLocked(afid, h, now, false);
        if (fw.any) g_holds.erase(afid);
    }
    SendHeld(fw);
}

// ★ SCENE + CHOKE, watched here since 2026-09-10 - they moved with the plug narration.
// A plug going in or out during a SexLab/OStim scene is the scene's moment, not ours, and
// a plug on someone being choked goes out at the quiet persistent tier. Both used to be the
// Database Controller's; the narration they gate is the AddOn's now, and so is this.
std::atomic<bool>          g_sceneOn{ false };
std::atomic<double>        g_sceneAt{ 0.0 };
std::atomic<double>        g_sceneEndAt{ 0.0 };   // D2: when the last scene ENDED (0 = never)
std::atomic<std::uint32_t> g_chokeFid{ 0 };

// ★ D2 (2026-09-13): a post-scene GRACE. Devious Devices' zadBQ00 stores every un-belted plug at
// AnimationStart and re-equips it after AnimationEnd. Our flag rose synchronously at the start
// edge (the removal was swallowed) and fell synchronously at the end edge - BEFORE DD's queued
// Papyrus handler re-fitted the plug - so the re-fit arrived with the flag down and the 5 s echo
// long expired: a GLOBAL interrupt and "P pushed the X into her vagina" for a plug the LLM never
// heard leave. The scene still owns the moment for this long after it ends. Papyrus latency at a
// scene end is seconds on a heavy load order, hence 15 and not 10.
constexpr double kSceneGraceS = 15.0;

// The Controller's own rule, moved with it: a missed end event must not mute plug narration
// forever, so a scene flag older than 20 minutes is dropped.
bool SceneOnNow()
{
    if (!g_sceneOn.load(std::memory_order_relaxed)) {
        const double endAt = g_sceneEndAt.load(std::memory_order_relaxed);
        return endAt > 0.0 && (HoldNow() - endAt) < kSceneGraceS;
    }
    if (HoldNow() - g_sceneAt.load(std::memory_order_relaxed) < 1200.0) return true;
    g_sceneOn.store(false, std::memory_order_relaxed);
    return false;
}

// ★★ D1 - THE DEVICE DETAIL FOR A HAND EQUIP (2026-09-13). VRTouchEvents narrates the act of every
// hand equip from PPB's own PPB_GestureDeviceEquipped / PPB_GestureGearEquipped, so our TESEquipEvent
// sink stays quiet for it (the gesture claim) - and since VRTE deleted its DD wearer and onlooker
// lines that day, the device-specific part went unsaid (VRTE request UPDATE 3 D1, the user: "accept
// the gap, and pass it to the AddOn"). This puts it back as AWARENESS: WornDevices::FittedLines
// composes it from the worn block's own vocabulary, VRTEDD_Narrate.DeviceFitted sends a persistent
// event to her and short-lived ones to onlookers. Never a second DirectNarration - VRTE's line is
// the one that asks her to react.
// ⚠ Triggered by PPB's confirmation, not by the equip sink: it fires ~1.2 s after the equip, once
// PPB has seen the piece really worn, and it carries the held half's REAL name and PPB's lock read
// of both halves - neither of which the sink has at that moment (the rendered half is nameless).
// ⚠ A static call on the main thread, the NarrateDeviceChange pattern (no listener to lose on load).
void QueueFitted(std::uint32_t actorFid, std::string heldName, std::uint32_t slotMask, bool locked)
{
    if (!actorFid || actorFid == 0x14 || !slotMask) return;
    TaskRelay::Add([actorFid, heldName = std::move(heldName), slotMask, locked]() {   // 1.3.6: called from GestureSink
        auto* af = RE::TESForm::LookupByID(actorFid);
        auto* a  = af ? af->As<RE::Actor>() : nullptr;
        if (!a || a->IsDead() || a->IsDisabled()) return;
        if (SceneOnNow()) {                               // the scene owns the moment
            logger::info("[FITTED] 0x{:08X} '{}' - scene running, nothing sent", actorFid, heldName);
            return;
        }
        std::string label, detail, seen;
        if (!WornDevices::FittedLines(a, slotMask, heldName.c_str(), locked, label, detail, seen)) {
            logger::info("[FITTED] 0x{:08X} '{}' slot {} - nothing to add (no covered device there, "
                         "a plug, or an unmapped class)", actorFid, heldName, slotMask);
            return;
        }
        logger::info("[FITTED] 0x{:08X} '{}': {} | seen: {}", actorFid, label, detail,
                     seen.empty() ? "(not visible - no onlooker line)" : seen);
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) return;
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb;
        auto* args = RE::MakeFunctionArguments(static_cast<std::int32_t>(actorFid),
                                               RE::BSFixedString(label.c_str()),
                                               RE::BSFixedString(detail.c_str()),
                                               RE::BSFixedString(seen.c_str()));
        vm->DispatchStaticCall("VRTEDD_Narrate", "DeviceFitted", args, cb);
        delete args;
    });
}

// -----------------------------------------------------------------------------------------
//  THE TRANSLATION TABLE - PPB's generic gesture names -> this add-on's VRTE vocabulary.
//
//  Payloads pass through UNCHANGED. PPB's field layouts were designed here and moved with the
//  code, so a rename is genuinely all that is needed - and keeping the payload byte-identical
//  means VRTE's existing handlers did not have to change at all for the move.
//
//  ! PPB_GesturePlug is NEW and has NO VRTE_DDZaZ_ twin, deliberately. WornDevices already
//  emits VRTE_DDZaZ_PlugInserted / PlugRemoved from its TESEquipEvent sink, which catches
//  EVERY route a plug can take (menu, key, DD's own RemoveDevice, a third-party script);
//  translating PPB's gesture-only event into those names would double-fire the two routes
//  that ARE gestures. The gesture event is instead CLAIMED, exactly like a removal: it tells
//  the WornDevices sink that the change it is about to see came from the player's hands.
// -----------------------------------------------------------------------------------------
//
//  ⛔ THREE ROWS DROPPED 2026-09-13 (v1.2.3, the VRTE request's A1): UndressArm, DeviceEquipped and
//  GearEquipped. VRTouchEvents built 2026-09-13 or later hears PPB's own PPB_Gesture* events for
//  those and UNREGISTERS the renamed ones - nothing listened, so these rows emitted into nothing.
//  ⚠ VERSION PAIRING: an OLDER VRTouchEvents still listens only to the renamed names and loses its
//  hand-equip lines with this build. Both READMEs say so. Do not re-add a row "for compatibility" -
//  the new VRTouchEvents narrates the raw event, so a restored rename would be ignored at best.
//  UndressEnd stays: the undress hold (below) forwards the real DD name / the refused unlock under
//  that name, and VRTouchEvents still reads it for isDD 1.
// -----------------------------------------------------------------------------------------
struct Xlate { const char* from; const char* to; };
constexpr Xlate kXlate[] = {
    { "PPB_GestureUndressEnd",     "VRTE_DDZaZ_UndressEnd"     },
};

// Queued work, drained on the main thread. The sink runs on whatever thread sent the event
// (the VM's, for anything Papyrus raised), and re-entering the mod-event source from inside
// its own dispatch is not a thing to do - so nothing is emitted from ProcessEvent directly.
struct Queued {
    const char*   to = nullptr;      // static string from kXlate, safe to hold
    std::string   strArg;
    float         numArg = 0.f;
    std::uint32_t senderFid = 0;
};

class GestureSink : public RE::BSTEventSink<SKSE::ModCallbackEvent> {
public:
    RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* ev,
                                          RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
    {
        if (!ev || !ev->eventName.c_str()) return RE::BSEventNotifyControl::kContinue;
        const char* name = ev->eventName.c_str();

        // -- SCENE + CHOKE EDGES (2026-09-10) -------------------------------------------
        // For the AddOn's plug narration (see g_sceneOn): the same six scene edges and VRTE's
        // choke state the Database Controller used to register for.
        if (_stricmp(name, "ostim_start") == 0 || _stricmp(name, "StartSexLabAnimation") == 0 ||
            _stricmp(name, "AnimationStart") == 0) {
            g_sceneAt.store(HoldNow(), std::memory_order_relaxed);
            g_sceneOn.store(true, std::memory_order_relaxed);
            return RE::BSEventNotifyControl::kContinue;
        }
        if (_stricmp(name, "ostim_end") == 0 || _stricmp(name, "EndSexLabAnimation") == 0 ||
            _stricmp(name, "AnimationEnd") == 0) {
            g_sceneOn.store(false, std::memory_order_relaxed);
            g_sceneEndAt.store(HoldNow(), std::memory_order_relaxed);   // D2: the grace starts
            return RE::BSEventNotifyControl::kContinue;
        }
        if (_stricmp(name, "VRTE_ChokeState") == 0) {
            const long long fid = ev->strArg.empty() ? 0 : std::atoll(ev->strArg.c_str());
            g_chokeFid.store((ev->numArg > 0.5f && fid > 0) ? static_cast<std::uint32_t>(fid) : 0u,
                             std::memory_order_relaxed);
            return RE::BSEventNotifyControl::kContinue;
        }

        // -- THE CLAIM ------------------------------------------------------------------
        // PPB's own words in DeviceGesture.cpp: "The add-on's WornDevices sink needs to know
        // this removal was OUR gesture and not a menu, so it can attribute it. PPB does not
        // own that bookkeeping - it announces, the add-on claims." This is the claiming half.
        //
        // ! strArg is the actor FormID as DECIMAL text (std::to_string of a uint32), not hex.
        // atoll, not strtoul base 16.
        if (_stricmp(name, "PPB_GestureClaim") == 0) {
            const long long fid = ev->strArg.empty() ? 0 : std::atoll(ev->strArg.c_str());
            if (fid > 0) WornDevices::ClaimGesture(static_cast<std::uint32_t>(fid));
            return RE::BSEventNotifyControl::kContinue;
        }

        // A plug gesture claims the actor the same way, so the PlugRemoved the WornDevices sink
        // is about to raise can say the player's hands did it.
        //
        // ⛔ THE "out" EDGE ONLY, AND THAT IS NOT AN OVERSIGHT (2026-08-27, audit).
        // The two edges sit at opposite ends of their events. "out" is emitted BEFORE the rip, so
        // claiming here lands ahead of the unequip it annotates — correct. "in" is emitted from
        // PendingVerify's success path, which by definition runs only once the device is ALREADY
        // worn, so its TESEquipEvent has been and gone; claiming there annotates nothing and
        // instead OVERWRITES the claim deadline with a fresh one (WornDevices' g_gestureClaim is
        // an assignment, not a max), stretching a window deliberately cut to 3.0 s out to ~5.5 s.
        // The "in" case needs no claim from us: DoEquip already claimed the actor on its way in.
        if (_stricmp(name, "PPB_GesturePlug") == 0) {
            const char* p = ev->strArg.c_str();
            const bool  isOut = p && _strnicmp(p, "out|", 4) == 0;
            if (isOut) {
                if (auto* a = ev->sender ? ev->sender->As<RE::Actor>() : nullptr)
                    WornDevices::ClaimGesture(a->GetFormID());
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        // -- D1: THE DEVICE DETAIL FOR A HAND EQUIP (2026-09-13) - see QueueFitted ----------
        // Read only: since v1.2.3 (A1) neither event is renamed any more - VRTouchEvents hears both
        // straight from PPB.
        //   PPB_GestureDeviceEquipped  "<name>|<class>|<locked>|<quest>|<siteMask>|<slotMask>|<force>"
        //   PPB_GestureGearEquipped    "<name>|<slotMask>|<force>|<ordinary>"  - ordinary 0 = ZaZ/DoM device
        // ⚠ SplitBar, which keeps empty fields positionally - a nameless held half cannot shift them.
        // ⚠ An older PPB without <ordinary> sends plain gear the same shape, so no field = no line.
        if (!g_paused.load(std::memory_order_relaxed) &&
            (_stricmp(name, "PPB_GestureDeviceEquipped") == 0 || _stricmp(name, "PPB_GestureGearEquipped") == 0)) {
            const bool ddEvent = _stricmp(name, "PPB_GestureDeviceEquipped") == 0;
            const auto f = SplitBar(std::string(ev->strArg.c_str()));
            const std::uint32_t afid = ev->sender ? ev->sender->GetFormID() : 0u;
            if (ddEvent && f.size() > 5)
                QueueFitted(afid, f[0], static_cast<std::uint32_t>(std::strtoul(f[5].c_str(), nullptr, 10)),
                            f[2] == "1");
            else if (!ddEvent && f.size() > 3 && f[3] == "0")
                QueueFitted(afid, f[0], static_cast<std::uint32_t>(std::strtoul(f[1].c_str(), nullptr, 10)),
                            false);
        }

        // -- THE UNDRESS HOLD (2026-09-10) ----------------------------------------------
        // A finished pull (done=1) waits for proof - see the banner over UndressHold. A
        // cancelled one (done=0) changes nothing and falls through to the renames as before.
        if (_stricmp(name, "PPB_GestureUndressEnd") == 0 && !g_paused.load(std::memory_order_relaxed)) {
            const std::string payload(ev->strArg.c_str());
            const auto f = SplitBar(payload);
            const std::uint32_t afid = ev->sender ? ev->sender->GetFormID() : 0u;
            if (afid && f.size() > 3 && f[2] == "1") {
                HoldEnd(afid, payload, ev->numArg, f[3] == "1");
                return RE::BSEventNotifyControl::kContinue;
            }
        }

        // -- THE UNLOCK, and DD's KEY RULE (2026-09-10) --------------------------------
        // PPB raises PPB_GestureUnlocked (strArg = the INVENTORY device FormID as decimal
        // text, sender = the actor) only once DD has really unlocked a device. Two things
        // hang on it: the held undress narration above, and - when the removal gate approved
        // this pull BECAUSE a key was held - DD's own key consumption, which a hand unlock
        // otherwise skips. The rule is the AddOn's Papyrus (VRTEDD_Keys.ConsumeAfterUnlock),
        // read from DD's own data at that moment; this side only knows the pull was keyed.
        if (_stricmp(name, "PPB_GestureUnlocked") == 0) {
            const long long inv = ev->strArg.empty() ? 0 : std::atoll(ev->strArg.c_str());
            auto* a = ev->sender ? ev->sender->As<RE::Actor>() : nullptr;
            if (inv > 0 && a) {
                const std::uint32_t afid   = a->GetFormID();
                const std::uint32_t invFid = static_cast<std::uint32_t>(inv);
                std::uint32_t rend = 0, key = 0;
                int needed = 1;
                if (WornDevices::TakeKeyedAllow(afid, 5.0, rend, key, needed)) {
                    logger::info("[KEY] unlock confirmed 0x{:08X} device 0x{:08X} (inventory 0x{:08X}) - "
                                 "DD's key rule handed to VRTEDD_Keys (key 0x{:08X} x{})",
                                 afid, rend, invFid, key, needed);
                    {   // 1.3.6: inside GestureSink::ProcessEvent - through the relay (TaskRelay.h)
                        TaskRelay::Add([afid, invFid, key, needed]() {
                            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
                            if (!vm) return;
                            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb;
                            auto* args = RE::MakeFunctionArguments(
                                static_cast<std::int32_t>(afid), static_cast<std::int32_t>(invFid),
                                static_cast<std::int32_t>(key), static_cast<std::int32_t>(needed));
                            vm->DispatchStaticCall("VRTEDD_Keys", "ConsumeAfterUnlock", args, cb);
                            delete args;
                        });
                    }
                }
                HoldUnlock(afid, invFid);
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        // -- (THE MASTURBATION RELAY - REMOVED 2026-09-13) ------------------------------
        // ⛔ The user moved it into base VRTouchEvents: "just cut it away from the AddOn, it's a VRTE
        // by product ... the AddOn will be DD specific action and VR integration, masturbation event
        // are more a general thing than DD specific". VRTE's VRTouch_MainScript now consumes
        // PPB_PlayerMasturbation directly and tells the onlookers. Do not re-add a relay here - both
        // would narrate it. (VRTE_DDZaZ_Masturbation and VRTEDD_Narrate.Masturbation are gone with it.)

        // -- THE RENAMES ----------------------------------------------------------------
        for (const Xlate& x : kXlate) {
            if (_stricmp(name, x.from) != 0) continue;
            if (g_paused.load(std::memory_order_relaxed)) break;
            Push(Queued{ x.to, std::string(ev->strArg.c_str()), ev->numArg,
                         ev->sender ? ev->sender->GetFormID() : 0u });
            break;
        }
        return RE::BSEventNotifyControl::kContinue;
    }

private:
    static void Push(Queued q)
    {
        TaskRelay::Add([q = std::move(q)]() {   // 1.3.6: called from ProcessEvent - see TaskRelay.h
            RE::TESForm* sender = q.senderFid ? RE::TESForm::LookupByID(q.senderFid) : nullptr;
            SendVrteEvent(q.to, q.strArg, q.numArg, sender);
        });
    }
};
GestureSink g_gestureSink;

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
// ⚠ APPEND ONLY, NEVER REORDER. The integer is a CONTRACT with the Papyrus
// decorators and with anything a user wrote against it - renumbering would
// silently point every existing caller at a different body part.
// ★ kCatDeaf added 2026-08-27 (the user: "the hearing one is a weird one").
enum RestraintCat : int { kCatGag = 0, kCatBlind, kCatArms, kCatLegs, kCatAll,
                          kCatDeaf, kCatCount };

const char* CatName(int c)
{
    switch (c) {
    case kCatGag:   return "gag";
    case kCatBlind: return "blind";
    case kCatArms:  return "arms";
    case kCatLegs:  return "legs";
    case kCatAll:   return "all";
    case kCatDeaf:  return "deaf";
    default:        return "?";
    }
}

// DD (and the two other frameworks this load order carries) — always active.
// A keyword EditorID that does not exist in a given load order simply never
// matches, so listing NG-only and ZaZ-only names here costs nothing.
// ⚠ SECOND DIMENSION RAISED 12 -> 20 (2026-08-27), then 20 -> 24 (2026-08-30
// for the DoM rows below - the arms row reached 15 of 20 and the audit
// flagged that adding 3 more would have left it one short of the bound). The arms row already held 9
// entries plus its nullptr terminator, i.e. two spare, and every consumer walks
// to the nullptr - so overrunning a row would READ PAST IT into the next row
// rather than failing to compile, silently making one category answer another
// category's keywords. Raised before adding anything, not after.
const char* const kBuiltinKw[kCatCount][24] = {
    /* gag   */ { "zad_DeviousGag", "zad_DeviousGagLarge", "zad_DeviousGagPanel",
                  "zad_DeviousGagBit", "zad_DeviousGagRing", "zad_DeviousGagTape",
                  "zad_DeviousGagInflatable", "zbfWornGag", "DOMWornGag", nullptr },
    // ⚠ DOMWornBlindfold ADDED 2026-08-29: DoM's Moth Priest Blindfold (and
    // kin) carried it and registered as nothing - a blindfolded DoM captive
    // read as sighted.
    /* blind */ { "zad_DeviousBlindfold", "zad_DeviousHood", "zbfWornBlindfold",
                  "DOMWornBlindfold", nullptr },
    /* arms  */ { "zad_DeviousHeavyBondage", "zad_DeviousArmbinder", "zad_DeviousArmbinderElbow",
                  "zad_DeviousYoke", "zad_DeviousYokeBB", "zad_DeviousElbowTie",
                  // ⛔ zad_DeviousGloves REMOVED from the flat row 2026-08-30:
                  // 47 plain latex gloves were action-locking their wearers.
                  // The user: "some glove are restrictive, the cover the hand
                  // and go almost up to the shoulder" - so the keyword counts
                  // only when the pushed NAME says Restrictive/Seer's; see the
                  // conditional in ActorRestrained below.
                  "zad_DeviousBondageMittens", "zad_DeviousCuffsFront",
                  // DD NG classes that were missing here entirely (2026-08-27).
                  "zadNG_DeviousBoxbinder", "zadNG_DeviousYokeFront",
                  "zad_DeviousStraitJacket",
                  // ⛔ ZaZ HAD NO ARMS ROW AT ALL until 2026-08-27. The gag row
                  // carried zbfWornGag and the blind row zbfWornBlindfold, but an
                  // NPC in ZaZ yokes or wrist irons registered as not restrained -
                  // so the restraint decorators and the 0910 prompt said nothing.
                  // zbfEffectNoFighting is the behavioural one (she cannot fight),
                  // which is the same standard RestraintOf uses.
                  "zbfWornYoke", "zbfEffectNoFighting",
                  // DoM (2026-08-29): its armbinder and behind-back rope had
                  // no row - a DoM-bound captive registered as unrestrained.
                  "DOMWornArmbinder", "DOMWornCuffsBack",
                  // ⚠ COMPLETED 2026-08-30 (audit). Round 9 wired these four
                  // into the LAYER and POSE resolvers and forgot the registry,
                  // so DoM's Wooden Yoke, the three Prisoner's Cuffs and the
                  // Cuffs Rope still registered as unrestrained - no 0910 block
                  // at all for a DoM captive wearing them.
                  "DOMWornWrist", "DOMWornCuffsCrossed", "DOMWornYoke",
                  nullptr },
    /* legs  */ { "zad_DeviousHobbleSkirt", "zad_DeviousHobbleSkirtRelaxed",
                  "zad_DeviousAnkleShackles", "zad_DeviousPonyGear", "zad_DeviousBoots",
                  // ⚠ zbfEffectNoSprint is deliberately NOT here: it has zero
                  // implementation hits anywhere in ZaZ's own scripts - declared
                  // intent, not a working effect.
                  "zbfWornAnkles", "zbfEffectSlowMove",
                  // DoM's ankle shackle (2026-08-30, audit) - same omission.
                  "DOMWornAnkle",
                  nullptr },
    /* all   */ { "zad_DeviousStraitJacket", "zad_DeviousPetSuit", nullptr },
    // ★ HEARING (2026-08-27). ⛔ THERE IS NO HEARING KEYWORD IN DD OR ZaZ -
    // verified by scanning every keyword on every rendered and inventory half in
    // this load order for ear/hear/deaf/sound/muffl. The only hits were
    // `zbfEffectGagSound` (27 records - that is the gag's OUTPUT, not the
    // wearer's hearing) and two substring false positives, `zadNG_HideArms` and
    // `zad_DeviousPonyGear`, which merely contain the letters "ear".
    //
    // ★ So the ONE thing in this load order that covers the ears is the HOOD -
    // 134 records - and this AddOn already says so in the worn-device block,
    // where every hood reports "dulling sound" unconditionally while the SIGHT
    // half is gated on zad_DeviousBlindfold. This row is that same fact,
    // promoted to a state the prompts can act on.
    //
    // ⚠ A HOOD DULLS, IT DOES NOT DEAFEN, and the prompt is written for that
    // rather than for silence - which is both what the gear does and the only
    // honest instruction available, since SkyrimNet feeds the NPC the player's
    // words no matter what we say here.
    // ⚠ zad_DeviousBlindfold is deliberately ABSENT: a blindfold covers the eyes
    // and leaves the ears open. Only the enclosing hood reaches the ears.
    // ⚠ Users can add earplugs or a deafness spell through
    // VRTE_DDZaZ_Restraints.ini's new [deaf] section; the INI can only ADD.
    /* deaf  */ { "zad_DeviousHood", nullptr },
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
    for (int i = 0; i < 24 && kBuiltinKw[cat][i]; ++i) {
        const char* kw = kBuiltinKw[cat][i];
        // ⛔ THE BALL FAMILY (2026-08-29). Nine ZaZ GENITAL devices carry
        // zbfWornGag - ZaZ's own data bug (report 29 §0.6b) - so a ball
        // stretcher registered its wearer as speech-blocked and 0900 told the
        // LLM the mouth was sealed. Slot 52 separates the nine from the 27
        // real gags with zero false positives and zero misses across all 222
        // ZaZ records: no real gag touches the genital slot.
        // ⛔ A HOOD IS NOT AUTOMATICALLY A BLINDFOLD (2026-08-30, audit).
        // The WORN BLOCK has gated the sight half on zad_DeviousBlindfold since
        // report 29 §3n - "encloses the head, dulling sound, with the eyes and
        // mouth left clear" - while this row counted every zad_DeviousHood as
        // blind. So 76 of 134 hoods had the two halves of our own model saying
        // opposite things about the same eyes, and the blind MODULE then
        // stripped their bows and rolled trips on them. Same shape as the
        // ball-family guard below: the broad keyword counts only with its
        // corroborating one.
        // ⚠ The DEAF row keeps bare zad_DeviousHood on purpose - a hood muffles
        // sound whether or not it covers the eyes. That asymmetry is the point.
        if (cat == kCatBlind && _stricmp(kw, "zad_DeviousHood") == 0) {
            if (WornDevices::WearsSightBlocker(a)) return true;
            continue;
        }
        // ★ 2026-09-12: the SAME corroboration for the blindfold keyword itself.
        // The Extreme Hood (Open) carries zad_DeviousBlindfold directly and has
        // holes for the eyes (the user: "can see"), so the bare keyword would mark
        // its wearer blind while the worn line said the eyes were clear. Asking
        // per device keeps a genuine blindfold worn alongside it counting.
        if (cat == kCatBlind && _stricmp(kw, "zad_DeviousBlindfold") == 0) {
            if (WornDevices::WearsSightBlocker(a)) return true;
            continue;
        }
        // ★ 2026-09-12: ONLY A FULL-GRADE BOOT HOBBLES. zad_DeviousBoots is on all
        // 108 DD boots, so this row made every one of them - socks included - emit
        // the legs state, whose prompt says she "moves at a slow shuffle". The
        // user's ruling: leg-grade boots do NOT reduce the pace, and socks / plain
        // Oil Boots are not a restraint. Hobble skirts and shackles are unaffected;
        // they come through their own keywords in this row.
        if (cat == kCatLegs && _stricmp(kw, "zad_DeviousBoots") == 0) {
            if (WornDevices::WearsFullGradeBoot(a)) return true;
            continue;
        }
        // ⛔ THE MIS-TAGGED BLINDFOLDS (2026-09-08, caught in VR). Five DD
        // records named "... Blocking Blindfold" render a blindfold mesh and
        // still carry zad_DeviousHood, so this row was deafening their wearer.
        // Measured on one session: 30 renders told Sofia she could not hear,
        // wearing nothing but a blindfold. Ask the MESH, per device - a genuine
        // hood worn at the same time still answers yes.
        // ⚠ The asymmetry with the blind row above is still the point: a hood
        // muffles sound whether or not it covers the eyes. This does not soften
        // that; it only stops a BLINDFOLD pretending to be a hood.
        if (cat == kCatDeaf && _stricmp(kw, "zad_DeviousHood") == 0) {
            if (WornDevices::WearsRealHood(a)) return true;
            continue;
        }
        // ★ THE RESTRICTIVE-GLOVE RULE (2026-08-30, the user's ruling): a
        // glove restrains only when it is the full-sleeve kind, and only the
        // NAME says which - on the inventory half, pushed in by the quest
        // script. No name yet -> not restrained (the safe direction: 50 plain
        // gloves stop lying; the 8 restrictive ones join within one poll).
        // (Count corrected 2026-08-31: 58 records carry zad_DeviousGloves
        //  - 41 class Gloves + 15 BondageMittens + 2 Harness - of which 8 are
        //  named "Restrictive Gloves". 58-8 = 50. The comment said 47, which
        //  no slicing of the census yields. Comment only; no code change.)
        if (cat == kCatArms && WornHasKw(a, "zad_DeviousGloves")) {
            const std::string nm =
                WornDevices::PushedName(a->GetFormID(), "Gloves");
            if (!nm.empty()) {
                const std::string lo = LowerOf(nm.c_str());
                if (lo.find("restrictive") != std::string::npos ||
                    lo.find("seer's") != std::string::npos)
                    return true;
            }
        }
        if (cat == kCatGag && _stricmp(kw, "zbfWornGag") == 0) {
            bool hit = false;
            ForEachWorn(a, [&](RE::TESObjectARMO* w) {
                auto* kwf = w->As<RE::BGSKeywordForm>();
                bool has = false;
                if (kwf) {
                    for (std::uint32_t k = 0; k < kwf->GetNumKeywords(); ++k) {
                        auto kk = kwf->GetKeywordAt(k);
                        if (!kk.has_value() || !kk.value()) continue;
                        const char* id = kk.value()->GetFormEditorID();
                        if (id && _stricmp(id, "zbfWornGag") == 0) { has = true; break; }
                    }
                }
                if (!has) return true;
                const auto m = static_cast<std::uint32_t>(w->GetSlotMask().underlying());
                if (m & (1u << 22)) return true;   // slot 52: genital gear, not a gag
                hit = true;
                return false;
            });
            if (hit) return true;
            continue;
        }
        if (WornHasKw(a, kw)) return true;
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
                    // ⛔ GENERATED FROM CatName, not hand-written. The literal
                    // here said "gag, blind, arms, legs, all" and had done since
                    // before kCatDeaf was appended on 2026-08-27 - so the parser
                    // accepted [deaf], the INI documented [deaf], and the only
                    // message a user saw after mistyping the header told them
                    // deaf was invalid. Built from the enum, it cannot go stale
                    // when a seventh category is appended.
                    std::string valid;
                    for (int c = 0; c < kCatCount; ++c) {
                        if (!valid.empty()) valid += ", ";
                        valid += CatName(c);
                    }
                    logger::info("[RESTRAINT] unknown section '[{}]' - lines under it are ignored "
                                 "(valid: {})", name, valid);
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

}   // namespace (anonymous)

namespace DeviceEquip {

// ── the restraint registry, for main.cpp's Papyrus natives ─────────────────
void LoadRestraints() { ::LoadRestraintIni(); }

// The public face of the file-local scene flag (DeviceEquip.h). The definition above has
// internal linkage - the LNK2019 this project has paid for before (WearsRealHood).
bool SceneOnNow() { return ::SceneOnNow(); }

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

// ─────────────────────────────────────────────────────────────────────────────
// SkyrimNet decorators.
//
// Nothing in the load order ever told an NPC she was gagged — verified 08-23:
// zero decorators anywhere feed DD state to SkyrimNet, and no prompt component
// reads a zad_* keyword. SkyrimNet's own `get_worn_equipment` cannot substitute:
// it renders only inside dynamic_character_bio.prompt, never in the live
// dialogue prompt, and it reports item NAMES — which are empty for every DD
// rendered device (report 23 §6.2).
//
// The Papyrus side does the DD lookup and SkyrimNet registration; the wording
// lives in the shipped drop-in prompt so it can be tuned without a rebuild:
//   SKSE/Plugins/SkyrimNet/prompts/submodules/user_final_instructions/
//       0900_vrtedd_gag.prompt
// ─────────────────────────────────────────────────────────────────────────────
void RegisterDecorators()
{
    auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
    if (!vm) return;
    RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb;
    auto* args = RE::MakeFunctionArguments();
    vm->DispatchStaticCall("VRTE_DDZaZ_Decorators", "Register", args, cb);
    delete args;
    logger::info("[SKYRIMNET] dispatched VRTE_DDZaZ_Decorators.Register "
                 "(gag awareness; the script logs whether SkyrimNet accepted it)");
}


// ★★ THE ADDON GATE (2026-09-03). This whole sink is the VR/gesture half of the
// mod, and the mod now ships as TWO: the DATABASE (device knowledge, state,
// effects, narration, visibility — this DLL and everything else in it) and the
// ADDON (the SkyrimNet actions and this gesture bridge).
//
// The user's reason for splitting: *"the intention is for other people that want
// to use this as a base for their mod, but don't like the way i implement the
// equip and action for them."* So the database must not impose our equip layer.
//
// ⚠ WHY A PRESENCE TEST AND NOT A SECOND DLL. The translation itself is 4 renames,
// but the sink around it carries a deliberate threading design — it runs on
// whatever thread raised the event and QUEUES rather than re-entering the
// mod-event source from inside its own dispatch. Re-implementing that in Papyrus
// to satisfy a folder boundary would trade a documented, working design for an
// organisational one. Gating it on the AddOn's own plugin keeps the code where
// its reasoning lives and still makes it genuinely absent for anyone who does
// not install the AddOn: no sink, no claims, no translated events.
//
// ⚠ IT IS ALSO INERT BY CONSTRUCTION WITHOUT PPB — PPB_Gesture* simply never
// fires — so this gate is about the SPLIT, not about VR detection. Both halves
// hold: no AddOn, no sink; no PPB, no events.
std::atomic<bool> g_gateLogged{ false };

void Install()
{
    if (g_armed.load(std::memory_order_relaxed)) return;   // already installed

    // ⛔ DO NOT LATCH g_armed BEFORE THE ANSWER IS TRUSTWORTHY. Install() runs
    // FIRST at kPostPostLoad, where TESDataHandler is not populated yet - the
    // same reason LoadRestraints waits for kDataLoaded (see main.cpp). The
    // original `exchange(true)` at the top latched on that first call, so a
    // presence test here would have answered "absent", armed the flag, and made
    // the kDataLoaded retry a no-op: the bridge would NEVER arm, with a log line
    // cheerfully saying the AddOn was not installed. Return unlatched and let
    // the retry decide.
    auto* dh = RE::TESDataHandler::GetSingleton();
    if (!dh) return;                                        // too early to answer

    if (!dh->LookupModByName("DD SN AddOn.esp")) {
        if (!g_gateLogged.exchange(true, std::memory_order_relaxed))
            logger::info("[DDZaZ] 'DD SN AddOn.esp' is not loaded - the PPB gesture bridge "
                         "stays OFF. This is the DATABASE half running standalone: device "
                         "knowledge, state, effects, narration and visibility all work; the "
                         "VR equip gestures and the SkyrimNet actions are the AddOn's.");
        return;                                             // still unlatched
    }

    g_armed.store(true, std::memory_order_relaxed);
    if (auto* src = SKSE::GetModCallbackEventSource()) src->AddEventSink(&g_gestureSink);
    logger::info("[DDZaZ] AddOn present - gesture bridge armed: PPB_Gesture* -> VRTE_DDZaZ_* "
                 "translation and gesture claims (masturbation is VRTE's since 2026-09-13). The GESTURES "
                 "themselves are PPB's (PPB/src/DeviceGesture.cpp + PPB_DeviceEquip.psc) "
                 "since 2026-08-27.");
}

// Nothing to reset any more: this file holds no gesture state, and the translation queue is
// drained by the task interface within a frame. Kept because main.cpp calls it on every load
// boundary, and a no-op with a reason beats a call site quietly deleted.
// ★ The removal gate's verdict for a non-finger pull (WornDevices::RemovalCheck). Inert when
// the AddOn is absent: no bridge, so nothing is ever held.
void OnRemovalVerdict(std::uint32_t actorFid, int state)
{
    if (!actorFid || !g_armed.load(std::memory_order_relaxed)) return;
    HoldVerdict(actorFid, state);
}

// Close holds whose signals never all came, and drop stray verdicts/unlocks that no pull
// claimed. Rides WornDevices::PollStates (3 s), so a hold lives 4-7 s at most.
void TickHolds()
{
    std::vector<HoldForward> out;
    const double now = HoldNow();
    {
        std::scoped_lock lk(g_holdMtx);
        for (auto it = g_holds.begin(); it != g_holds.end();) {
            const UndressHold& h = it->second;
            if (h.hasEnd) {
                HoldForward fw = ResolveHoldLocked(it->first, h, now, false);
                if (fw.any) {
                    out.push_back(std::move(fw));
                    it = g_holds.erase(it);
                    continue;
                }
            } else if (now - (h.verdictAt > h.unlockAt ? h.verdictAt : h.unlockAt) > kHoldWindowS) {
                it = g_holds.erase(it);
                continue;
            }
            ++it;
        }
    }
    for (const auto& fw : out) SendHeld(fw);
}

// ★ Is DD SN AddOn.esp loaded? The same test that arms the bridge, answerable before Install
// has latched: false until the load order is readable, so an early asker simply asks again.
bool AddOnPresent()
{
    if (g_armed.load(std::memory_order_relaxed)) return true;
    auto* dh = RE::TESDataHandler::GetSingleton();
    return dh && dh->LookupModByName("DD SN AddOn.esp") != nullptr;
}

// ★★ THE DEVICE NARRATION IS THE ADDON'S (2026-09-10). The user: "The database is pretty much no
// event or trigger, just awareness. So state, effect, climax, animation and sound, but no
// 'action' really." WornDevices still NOTICES every device change and still broadcasts it as a
// mod event; the line to SkyrimNet comes from the AddOn's VRTEDD_Narrate, called here, and
// only while DD SN AddOn.esp is loaded. Payloads are EmitDeviceChange's, unchanged.
// A static call rather than a listener: a quest's mod-event registration dies on every save
// load without a player alias, and the AddOn's quest has none (the VRTEDD_Keys pattern).
void NarrateDeviceChange(std::uint32_t actorFid, bool plug, bool on, const char* payload)
{
    if (!actorFid || !payload || !g_armed.load(std::memory_order_relaxed)) return;
    // The scene owns the moment. ★ D5 (2026-09-13): for EVERY device change, not plugs only -
    // DD's own animation filter and the OStim add-ons strip and re-fit devices around a scene,
    // and each one narrated "has come off" at the start and "is now wearing" (DirectNarration
    // on the 15 s ring) at the end. The D2 grace covers the re-fits after the end edge.
    if (SceneOnNow()) {
        logger::info("[NARRATE] {} 0x{:08X} - scene running (or just ended), not narrated",
                     plug ? (on ? "PlugInserted" : "PlugRemoved") : (on ? "DeviceOn" : "DeviceOff"), actorFid);
        return;
    }
    const bool  choked = plug && g_chokeFid.load(std::memory_order_relaxed) == actorFid;
    const char* fn     = plug ? (on ? "PlugInserted" : "PlugRemoved") : (on ? "DeviceOn" : "DeviceOff");
    std::string p(payload);
    TaskRelay::Add([actorFid, fn, p, choked]() {   // 1.3.6: reached from the equip sink - see TaskRelay.h
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) return;
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb;
        auto* args = RE::MakeFunctionArguments(static_cast<std::int32_t>(actorFid),
                                               RE::BSFixedString(p.c_str()), static_cast<bool>(choked));
        vm->DispatchStaticCall("VRTEDD_Narrate", fn, args, cb);
        delete args;
    });
}

void Reset() {}

// ! LEGACY ENTRY POINT. VRTE_DDZaZ_Native.SetScenePaused lands here - and NOTHING CALLS IT
// today (main.cpp says so; the scene gating that exists is the mod-event edges above, D17
// 2026-09-13). Kept because it is a published native: pausing means two things - this add-on
// stops relaying, AND PPB's gesture layer stands down - so the request is forwarded across
// the split. New callers should use PPB_Native.SetGesturePaused(bool) directly.
void SetPaused(bool paused)
{
    g_paused.store(paused, std::memory_order_relaxed);
    SendVrteEvent("PPB_GestureSetPaused", "", paused ? 1.f : 0.f, nullptr);
}

}   // namespace DeviceEquip
