#include "PCH.h"
#include <chrono>
#include <mutex>
#include <atomic>
#include "DeviceEquip.h"
#include "WornDevices.h"
#include "PromptState.h"
#include "VrteDdzGateAPI.h"

namespace logger = SKSE::log;

// =============================================================================
// VRTE DD/ZaZ AddOn — SKSE entry point.
//
// ★ BOUNDARY (the user's, 2026-08-21): this AddOn is its own mod and its own
// plugin. It does NOT live inside VRTouchEvents, and it does not modify PPB.
// Both are complex mods owned by their own agents; this consumes their published
// APIs and nothing else. Splitting one mod's code across two agents is exactly
// what this layout avoids.
//
// Runtime dependencies, all optional-and-inert-if-absent:
//   PPB   — capsule geometry (ReadCapsule) + the IsDriven coverage gate
//   HIGGS — the per-frame callback and the held object
//   DD    — driven through the engine (ActorEquipManager), never patched
// =============================================================================

SKSEPluginInfo(
    // ⛔ BUMPED 2026-09-03. This said {1,0,0} while the package shipped as
    // V1.1-beta, and the DLL carries NO VS_FIXEDFILEINFO - so the banner below
    // is the ONLY version a user or the author can see. Every beta report cited
    // the wrong build, and "is the new DLL actually loaded?" was unanswerable
    // on a mod whose entire purpose is collecting SE/AE logs.
    // 1.2.0 on 2026-09-10: the beta with the removal gate, key capture and the
    // gag/blindfold enforcement. The banner says "Database" because this DLL
    // ships in DD SN Database - the AddOn is a separate mod with no DLL.
    // 1.2.2 on 2026-09-10: device narration moved to the AddOn, the after-state became a
    // 3-prompt block, and the gate is handed to PPB only with the AddOn loaded.
    // 1.2.1 on 2026-09-10: the key-test fixes - the undress hold, keyed approvals for the
    // AddOn key rule, the slot gate, mannequins excluded, refusals logged.
    // 1.2.3 on 2026-09-13: the observer view, boot grades, hood layers + the Extreme Hood pair,
    // pony gag + Shibari, DoM restraints; masturbation relay gone (VRTouchEvents'); the three
    // dead gesture renames dropped; the hand-equip device detail (DeviceFitted) and hand-moved
    // unclassed plugs left to VRTouchEvents. ⚠ Pairs with VRTouchEvents built 2026-09-13 or later.
    // 1.2.4 on 2026-09-13: the integration review's fix list - climaxes credited only while a
    // device runs (D1), nothing done to an unconscious NPC (D3), the scene gate on every effect
    // and device line + a 15 s post-scene grace (D2/D5), names filed by rendered FormID + the
    // pair map so the EQUIP gate finally reads a name (D4/D9/D10), quest flags on the inventory
    // half (D7), the cast-vibration stop edge quiet (D12), residuals cleared on revert (D6), the
    // pacing event on the hand-plug route (D16), leash collars refused (F2), shock edge with its
    // sites (D14). ⚠ Ships with the matching VRTE_DDZaZ_Native.pex (NoteDeviceRecord).
    // 1.2.5 on 2026-09-13 (the first VR session on 1.2.4): DD's off->on re-asserts no longer
    // narrate as real removals/equips (deferred removals, echo for every class, already-worn =
    // no change); the shock-marker effect no longer reads as a jolt; the equip gate reads names
    // from the shipped pair table (VRTE_DDZaZ_Pairs.ini); a woken KO gets her bound-arm pose back.
    // 1.2.6 on 2026-09-13: the climax POSE (DD's standing clip over its floor clip for climaxes
    // 1-3, the edged/kneel clip from the 4th, arousal to 50 after), the 100 lane (a vibrating device
    // at full arousal goes off by itself), and the capitalised-pronoun strings gone from the
    // Controller's lines (Papyrus's case-insensitive string cache handed back "she").
    // 1.2.7 on 2026-09-14 (VRTouchEvents' request "PlugRefitEcho" part A): the WHOLE removal is held,
    // not just its line - DD's off->on re-fit no longer restarts a device's wear clock, writes a false
    // after-state, or erases her climax count / 100-lane cooldown; the churn echo is retired (it could
    // only swallow a real removal now); held removals are cleared at a load and never saved.
    // 1.2.8 on 2026-09-14 (the user: "cell load and gears equipping don't affect our state and
    // effect"): a device off her body but still in her inventory (a DD strip, outfit re-apply,
    // cell-load re-fit) or off during a scene is HELD, not removed; her last device coming off keeps
    // her climax count, cooldowns and arousal reading; the TIRED KNEEL (bleedout 3 s) when an arm
    // binder / yoke / leg restraint / rope bind at phase 4+ comes off. ⚠ Pairs with the matching
    // VRTE_DDZaZ_Equip.pex (TiredKneel).
    // 1.2.9 on 2026-09-14 (the user's four rulings): the Black Soulgem plug is a SHOCK plug again
    // (zad_EffectShockOnFullArousal discharges at full arousal - DD 5.2's own zadEventVibrate does
    // the same; the 100 lane yields to it); the STRUGGLE (Better NPC Support's escape attempts) is
    // told as a short-lived event, its removals folded into one spoken result, the tired kneel at its
    // end; DD's Horny event and the site-graded shock wording live in the Controller (pairs with
    // VRTEDD_Controller.pex of the same date).
    // 1.3.0 on 2026-09-14 — THE PROMPT STATE FILE (the user, 2026-09-13: "this is how we're going to
    // do all our state and worn-device prompts from now on"): PromptState.cpp writes
    // SKSE/Plugins/VRTE_DDZaZ/prompt_state.json on every change (equip, real removal, pushed name,
    // load, the 3 s poll when the text moved); the 12 prompts read it with read_json + the random
    // cache-bust (VRTouchEvents' recipe, VR-verified, report 45) and fall back to the 14 decorators,
    // which stay registered. The release countdown is time-based in the DLL for the file.
    // 1.3.1 on 2026-09-14: the WornDeviceName(actor, slotMask) native for VRTouchEvents' refused-pull
    // line (the real name behind "The Gag"). ⚠ Ships with the matching VRTE_DDZaZ_Native.pex.
    // 1.3.2 on 2026-09-14: THE STRUGGLE CTD - StruggleTick (and DrainPendingOff's struggle fold) read
    // device names while holding g_mtx, and the name lookup takes g_mtx again: std::system_error
    // "resource deadlock would occur" on the first Better NPC Support escape attempt. Plus the
    // dispatcher now sends each Papyrus function its exact Int count: ClimaxPose (2) and
    // RefreshBoundPose (1) had been refused by the VM on every call since 1.2.6. ⚠ Ships with the
    // matching VRTE_DDZaZ_Equip.pex (ClimaxPose ends DD's vibration at the credited climax).
    // 1.3.3 on 2026-09-14, from the independent review of 1.3.2 and the user's rulings: the player is
    // never climax-credited or posed ("Nothing"); every credited climax rests the 100 lane for its
    // cooldown (the early vibration stop let it re-fire on a stale arousal); a FULL escape now reaches
    // its struggle END (PollStates ticks struggle-only actors); the END tells unlocked / taken off /
    // forced apart ("Say she unlocked it" - the key route is read at the start). ⚠ Ships with the
    // matching VRTEDD_Controller.pex (6-field END) and VRTE_DDZaZ_Equip.pex (ClimaxPose guards).
    // 1.3.4 on 2026-09-14, the user in VR: "5 min guard? Where? Why? I never ask that?" - the 300 s
    // full-arousal cooldown (added unasked in 1.2.6, extended in 1.3.3) is GONE; the 100 lane now fires
    // once each time arousal reaches full and re-arms on a real reading below 99, and a credited climax
    // disarms it (the stale pre-climax arousal can no longer fire it). The struggle payloads carry English
    // lists ("the A, the B and the C") plus a capitalised still-on list as END field 7. ⚠ Ships with the
    // matching VRTEDD_Controller.pex (two-sentence END).
    // 1.3.5 on 2026-09-14, the user's VR rulings: the climax pose starts from DD's orgasm SIGNAL (the
    // plain DeviceActorOrgasm mod event, synchronous to C++) so DD's floor clip is held off instead of
    // jumping into the pose; climaxes 1-3 play DD's edged event, the 4th on the Belt Edged kneel through
    // a DD SN OAR submod; the pose lasts exactly the clip DD's replacer picked (read from the behaviour
    // graph) and no lane of ours starts anything on her until it ends. Shock: a plug ragdolls her,
    // piercings kneel her in bleedout. Wrist-cuff equip lines say wrists + front/behind. The claim window
    // is 3.5 s (PPB's equip check). ⚠ Ships with the matching VRTE_DDZaZ_Equip.pex, VRTE_DDZaZ_Native.pex
    // and the OAR config (meshes/.../OpenAnimationReplacer/DD to OAR/DDSN_ClimaxKneel/config.json).
    // 1.3.6 on 2026-09-15, from the independent review of 1.3.5 and the user's answers: tasks queued from
    // event sinks go through TaskRelay (no AddTask inside a sink - a possible lock-order freeze); the DLL
    // tells ClimaxPose when it added zadAnimatingFaction so every exit removes it; the signal poses only a
    // unique name; the kneel only with arms free ("Kneel only if arms free") and the climax payload says
    // whether it will play; belted NPCs stand on climaxes 1-3 (a second config-only OAR submod). ⚠ Ships
    // with VRTE_DDZaZ_Equip.pex (ClimaxPose takes THREE Ints), VRTEDD_Controller.pex (climax field 3, shock
    // wording) and both OAR configs.
    // 1.3.7 on 2026-09-15 (the user: "make sure it's our own variable" / ZaZ and DoM "working along fine with
    // the DD device"): the climax pose marks her with DD SN's OWN faction DDSN_ClimaxPoseFaction (DD SN
    // Database.esp 0x801, rank 4 kneel / 1 standing) instead of the shared vanilla actor value Variable03;
    // ZaZ/DoM arm restraints get the standing clip on every climax (DD NG's belt kneel knew only DD devices).
    // ⚠ Ships with DD SN Database.esp 1.3.7 (the faction), VRTE_DDZaZ_Equip.pex and both OAR configs.
    // 1.3.8 on 2026-09-15, after the first VR session of 1.3.7 (climax pose, signal, relief and the plug-shock
    // ragdoll all seen working): at full arousal the device goes off AGAIN when DD's vibration ran out with no
    // climax and no edge ("Device goes off again"); the blind trip rolls on the game's own walking/running
    // flags instead of a 150-unit travel floor that a slowed or idling NPC never crossed, with a receipt when
    // the watch opens. ⚠ Ships with VRTE_DDZaZ_Equip.pex + VRTE_DDZaZ_Native.pex (NoteVibrateResult).
    // 1.3.9 on 2026-09-15, after the second VR session: a gag or hood going on the NPC who is TALKING cuts her line
    // (SkyrimNet speech/audio signals tracked per speaker); a climax, a trip and a shock are said OUT LOUD by her
    // (DirectNarration) and cut her own line first if she is talking; while the device keeps going off again at
    // full arousal only the first start and the last stop are told. ⚠ Ships with VRTE_DDZaZ_Equip.pex (CutSpeech),
    // VRTE_DDZaZ_Native.pex (IsTalking) and VRTEDD_Controller.pex.
    // 1.3.10 on 2026-09-15, from the independent review of 1.3.9: a reply whose voice is slow to start (13 % take
    // 5 s or more after the text is written) still counts as her talking, so a gag in that gap cuts her; and her
    // last device coming off clears a held vibration stop instead of telling it on her next equip. The AddOn's
    // plug lines (VRTEDD_Narrate) now cut only the NPC who is talking - they call IsTalking (a 1.3.9 native), so
    // that AddOn needs a Database of 1.3.9 or later.
    // 1.3.11 on 2026-09-15, from the user's VR session: a removal now rewrites the prompt state file AT THE UNEQUIP
    // instead of at the drain up to 4.5 s later - a hand pull is narrated by VRTouchEvents the moment our gate says
    // 'done', and her reply was rendering a block that still listed the device (measured 2.97 s / 4.06 s late).
    // The re-assert branch mirrors it. Prompt `0770_vrtedd_worn` is renumbered **0490** so the worn block renders
    // BEFORE SkyrimNet's own `0500_response_format` ("so as to not over ride the way a LLM is supposed to respond").
    // ★ 1.0.0 = THE FIRST PUBLIC RELEASE, as "SkyrimNet Devious Awareness Base" (2026-09-15). The 1.2.x/1.3.x
    // numbering below and in the ReadMe's changelog is this mod's development history under its old name.
    .Version              = { 1, 0, 0 },
    .Name                 = "VRTE_DDZaZ",
    .Author               = "mad72",
    .StructCompatibility  = SKSE::StructCompatibility::Independent,
    .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary
)

static void InitLogging()
{
    auto path = SKSE::log::log_directory();
    if (!path) return;
    *path /= "VRTE_DDZaZ.log";

    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
    auto log  = std::make_shared<spdlog::logger>("VRTE_DDZaZ", sink);
    log->set_level(spdlog::level::info);
    log->flush_on(spdlog::level::info);
    spdlog::set_default_logger(std::move(log));
    spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
}

// ═══════════════════════════════════════════════════════════════════════════
//  THE PUBLIC CLOTHING-GATE INTERFACE (2026-08-30)
//
//  PPB asked for this in its own source: "Their doc asks us to call their
//  natives (VRTE_DDZaZ_Equip.CanEquipOn) and to WAIT for their green light;
//  this is the local stand-in." ⛔ The Papyrus natives are the WRONG shape for
//  them - PPB decides at the release frame inside a gesture tick, and a VM
//  round-trip there is both slow and asynchronous. So the gate is exposed as a
//  synchronous C++ vtable, the same request/reply pattern PPB publishes for its
//  own touch API. Contract and the pair-half trap: VrteDdzGateAPI.h.
//
//  ⚠ Returned strings live in a THREAD-LOCAL buffer, stable until this thread's
//  next call - the header says so. Never hand out a std::string's interior.
// ═══════════════════════════════════════════════════════════════════════════
namespace
{
    RE::Actor* ActorFrom(std::uint32_t fid)
    {
        auto* f = RE::TESForm::LookupByID(fid);
        return f ? f->As<RE::Actor>() : nullptr;
    }
    RE::TESObjectARMO* ArmoFrom(std::uint32_t fid)
    {
        auto* f = RE::TESForm::LookupByID(fid);
        return f ? f->As<RE::TESObjectARMO>() : nullptr;
    }

    class GateInterface : public VRTEDDZ::IVrteGateInterface1
    {
    public:
        // ⛔ 3 SINCE 2026-09-08. Build 2 appended RemovalBlockedBy / CanRemove;
        // build 3 appends RemovalStateOf, which PPB asked for so that "we have
        // never seen this device" stops being reported as a refusal. Slots below
        // are only ever added to - a consumer built against 1 or 2 keeps working
        // untouched. Gate on >=, never on ==.
        // ★ 4 SINCE 2026-09-10 - and no new slot. BlockedBy now also answers the SLOT GATE
        // (a worn device already holding one of the newcomer biped slots), so the PPB release
        // refusal covers a second gag on a gagged mouth without a line of PPB changing.
        std::uint32_t GetBuildNumber() override { return 4; }

        const char* BlockedBy(std::uint32_t actorFid, std::uint32_t deviceFid) override
        {
            static thread_local std::string out;
            out.clear();
            auto* a = ActorFrom(actorFid);
            auto* d = RE::TESForm::LookupByID(deviceFid);
            if (!a || !d) return "";          // unknown -> no opinion -> ALLOW
            out = WornDevices::EquipBlockedBy(a, d);
            if (!out.empty()) {
                // "log both" (2026-09-10): the refusal pairs with PPB's own [EQUIP] REFUSED
                // line. Once per actor/device/answer inside 2 s.
                static std::mutex    s_m;
                static std::uint32_t s_a = 0, s_d = 0;
                static std::string   s_why;
                static std::chrono::steady_clock::time_point s_at{};
                const auto now = std::chrono::steady_clock::now();
                bool fresh = true;
                {
                    std::scoped_lock lk(s_m);
                    fresh = !(s_a == actorFid && s_d == deviceFid && s_why == out &&
                              now - s_at < std::chrono::seconds(2));
                    s_a = actorFid; s_d = deviceFid; s_why = out; s_at = now;
                }
                if (fresh)
                    logger::info("[GATE] EQUIP REFUSED 0x{:08X} device 0x{:08X} - '{}' is in the way",
                                 actorFid, deviceFid, out);
            }
            return out.c_str();
        }

        bool CanEquip(std::uint32_t actorFid, std::uint32_t deviceFid) override
        {
            return BlockedBy(actorFid, deviceFid)[0] == '\0';
        }

        int GetLayer(std::uint32_t deviceFid) override
        {
            return WornDevices::LayerIdOf(ArmoFrom(deviceFid));
        }

        bool IsDevice(std::uint32_t deviceFid) override
        {
            auto* w = ArmoFrom(deviceFid);
            return WornDevices::IsKnownDevice(w);
        }

        bool VisibleOn(std::uint32_t actorFid, std::uint32_t deviceFid) override
        {
            auto* a = ActorFrom(actorFid);
            auto* w = ArmoFrom(deviceFid);
            if (!a || !w) return true;        // unknown -> fail VISIBLE, never a silent lie
            return WornDevices::VisibleOn(a, w);
        }

        // ── APPENDED AT BUILD 2 ─────────────────────────────────────────────
        // The removal twin of BlockedBy. "" = allow. See VrteDdzGateAPI.h for
        // the contract and WornDevices::RemovalBlockedBy for the rule.
        // ⚠ UNKNOWN FAILS CLOSED HERE, unlike BlockedBy. Equipping something we
        // cannot classify should not be vetoed - that would silently block
        // content from mods we have never seen. REMOVING something we cannot
        // classify is the opposite: letting a locked device fall off because we
        // had no record is the destructive direction.
        const char* RemovalBlockedBy(std::uint32_t actorFid, std::uint32_t deviceFid,
                                     bool byHand) override
        {
            static thread_local std::string out;
            out.clear();
            auto* a = ActorFrom(actorFid);
            auto* w = ArmoFrom(deviceFid);
            if (!a || !w) { out = "This device stays on."; return out.c_str(); }
            out = WornDevices::RemovalBlockedBy(a, w, byHand);
            return out.c_str();
        }

        bool CanRemove(std::uint32_t actorFid, std::uint32_t deviceFid,
                       bool byHand) override
        {
            return RemovalBlockedBy(actorFid, deviceFid, byHand)[0] == '\0';
        }

        // ── APPENDED AT BUILD 3 ─────────────────────────────────────────────
        // 0 ALLOW / 1 BLOCKED / 2 UNKNOWN. See VrteDdzGateAPI.h for why UNKNOWN
        // is its own state rather than a sentinel, and why it is NOT "fail open".
        // ⚠ A form we cannot RESOLVE is BLOCKED, not UNKNOWN. UNKNOWN means "we
        // never saw this device equipped" - a statement about our records. A bad
        // FormID is a statement about the call, and answering "we don't know"
        // to it would invite the caller to proceed on a device that may not
        // exist.
        int RemovalStateOf(std::uint32_t actorFid, std::uint32_t deviceFid,
                           bool byHand) override
        {
            auto* a = ActorFrom(actorFid);
            auto* w = ArmoFrom(deviceFid);
            if (!a || !w) return 1;
            return WornDevices::RemovalStateOf(a, w, byHand);
        }
    };

    GateInterface g_gate;

    void* GetGateApi(unsigned int revision)
    {
        if (revision == 1) return &g_gate;
        logger::info("[GATE-API] a consumer asked for revision {} - we speak 1", revision);
        return nullptr;
    }
}

// ★★ THE GATE HANDSHAKE — and it MUST have its own listener. ★★
//
// ⛔ FIXED 2026-09-04. This block used to live inside OnSKSEMessage, which is
// registered with the ONE-ARG RegisterListener — and CommonLibSSE implements that
// as RegisterListener("SKSE", cb) (Interfaces.cpp:295). That enrols us in SKSE's
// listener list and NOWHERE ELSE, so a Dispatch(..., receiver = "VRTE_DDZaZ") sent
// BY PPB could never reach us. The handler was correct and unreachable.
//
// Measured on the 2026-09-03 session log: PPB asked ONCE at 19:38:51, got nothing,
// logged "DD/ZaZ clothing gate not available (AddOn absent or older)" and used its
// own region gate for the whole 3.5 h — which has no layer model, so it REFUSED
// 'Copper Wrist Cuffs' over gloves at 22:46:36, a case our four-layer gate allows.
// Our [GATE-API] count for that session: 0. The 30 ClothingGate.ini needles have
// therefore never been consulted by PPB at all.
//
// To hear another plugin we must register with nullptr — SKSE loops every loaded
// plugin and inserts us into each one's list. See PPB's own main.cpp:1127, which
// paid two days for exactly this lesson on the FSMP handshake.
//
// ⛔ AND IT NEEDS ITS OWN FUNCTION, never a second registration of OnSKSEMessage:
// SKSE lifecycle types are small integers (kPostPostLoad == 1), so another plugin
// dispatching type 1 would be misread as a lifecycle event and re-run Install().
// kGetGateInterface is 0x56445A47, which cannot collide with any of them.
static void OnAnyPluginMessage(SKSE::MessagingInterface::Message* msg)
{
    if (msg->type != VRTEDDZ::GateMessage::kGetGateInterface) return;   // not ours
    // A consumer may ask at any time from kPostLoad onward, and PPB's own doc says
    // a null reply must mean "not installed", never a crash.
    if (msg->data && msg->dataLen >= sizeof(VRTEDDZ::GateMessage)) {
        // ★ THE GATE IS THE ADDON'S (2026-09-10, the user: "The AddOn decide what can be equip
        // due to layer and slot location, we do the same for removal"). Which device may go on or
        // come off by hand is an ACTION rule, and the Database is awareness only - so the
        // interface is handed over only while DD SN AddOn.esp is loaded. Without it the reply
        // stays null, which PPB's contract reads as "not installed": it keeps its own
        // ClothingBlocker and lock rule, and asks again every 5 s - so an ask that lands before
        // the load order is readable is simply answered on a later one.
        if (!DeviceEquip::AddOnPresent()) {
            static std::atomic<bool> s_told{ false };
            if (!s_told.exchange(true))
                logger::info("[GATE-API] '{}' asked for the equip/removal gate - withheld: "
                             "'DD SN AddOn.esp' is not loaded, or the load order is not readable "
                             "yet. The consumer keeps its own rules and may ask again.",
                             msg->sender ? msg->sender : "(unnamed)");
            return;
        }
        static_cast<VRTEDDZ::GateMessage*>(msg->data)->GetApiFunction = GetGateApi;
        logger::info("[GATE-API] handed the clothing-gate interface to '{}'",
                     msg->sender ? msg->sender : "(unnamed)");
    }
}

static void OnSKSEMessage(SKSE::MessagingInterface::Message* msg)
{
    switch (msg->type) {
    case SKSE::MessagingInterface::kPostPostLoad:
        // First attempt at the PPB + HIGGS handshakes. Install() is idempotent
        // and is retried at kDataLoaded (the PPB/AIHands pattern) because plugin
        // load order decides whether they are ready yet.
        DeviceEquip::Install();
        break;

    case SKSE::MessagingInterface::kDataLoaded:
        DeviceEquip::Install();   // retry; no-op if already armed
        // ⚠ HERE AND NOT EARLIER. VRTE_DDZaZ_Restraints.ini resolves
        // `0x12345~Plugin.esp` through the data handler, which does not exist
        // at plugin-load: parsing sooner turns every form line into a silent
        // nullptr and the user's INI looks ignored.
        DeviceEquip::LoadRestraints();
        // The wear clock and the effect edges. Engine sinks, so no ESP and no
        // Papyrus timer — see WornDevices.h for why that mattered.
        WornDevices::Install();
        // ★ 1.3.0: the prompt state file - SkyrimNet's UUID export resolved, the file written empty.
        PromptState::Install();
        break;

    case SKSE::MessagingInterface::kPreLoadGame:
        DeviceEquip::Reset();     // gestures never survive a load boundary
        PromptState::Reset();     // nor does the prompt state file (rebuilt from the co-save below)
        break;

    case SKSE::MessagingInterface::kNewGame:
        DeviceEquip::Reset();
        PromptState::Reset();
        DeviceEquip::RegisterDecorators();
        break;

    // Decorators do not survive a session and this AddOn has no quest script,
    // so re-register every time a game comes up.
    case SKSE::MessagingInterface::kPostLoadGame:
        DeviceEquip::RegisterDecorators();
        PromptState::RequestRefresh("load");   // 1.3.0: the co-save's wear records are in - publish them
        break;

    default:
        break;
    }
}

// Papyrus-callable: VRTE_DDZaZ_Native.SetScenePaused(bool).
// Optional — nothing calls it yet. It exists so a scene manager (or VRTouchEvents,
// through its OWN script, without either mod including the other's code) can put
// this AddOn to sleep during a SexLab/OStim scene.
static void Papyrus_SetScenePaused(RE::StaticFunctionTag*, bool paused)
{
    DeviceEquip::SetPaused(paused);
}

// Papyrus-callable restraint registry (2026-08-24). The SkyrimNet decorator
// script asks these; they are the ONE place that knows about DD's keywords, the
// user's VRTE_DDZaZ_Restraints.ini, and spells. cat: 0 gag, 1 blind, 2 arms,
// 3 legs, 4 all, 5 deaf. ⚠ APPEND ONLY - the numbers are a contract with the
// decorators and with anything a user wrote against them.
static bool Papyrus_IsRestrained(RE::StaticFunctionTag*, RE::Actor* a, std::int32_t cat)
{
    return DeviceEquip::IsRestrained(a, static_cast<int>(cat));
}

static RE::BSFixedString Papyrus_RestraintName(RE::StaticFunctionTag*, RE::Actor* a,
                                               std::int32_t cat)
{
    return RE::BSFixedString(DeviceEquip::RestraintName(a, static_cast<int>(cat)).c_str());
}

// ★ 1.3.1 - for VRTouchEvents (behind its ddAddOnLoaded): the real display name of the device worn on
// any bit of the slot mask, "" when none / unknown. See WornDevices::WornDeviceName.
static RE::BSFixedString Papyrus_WornDeviceName(RE::StaticFunctionTag*, RE::Actor* a,
                                                std::int32_t slotMask)
{
    return RE::BSFixedString(WornDevices::WornDeviceName(a, static_cast<std::uint32_t>(slotMask)).c_str());
}

// ── worn devices: duration + strain (2026-08-26) ─────────────────────────────
// The whole 0917 block in one string. Built in C++ on purpose: SkyrimNet
// pre-computes every Papyrus decorator for every nearby entity on every render
// cycle, so walking worn armor in Papyrus would multiply by actors AND by
// cycles. Same reasoning that moved the restraint registry in report 23 §36 §2.
//
// Everything behind it — the wear clock and the effect edges — runs off engine
// event sinks and needs no Papyrus at all. This is the only entry point.
static RE::BSFixedString Papyrus_WornDeviceReport(RE::StaticFunctionTag*, RE::Actor* a)
{
    return RE::BSFixedString(WornDevices::Report(a).c_str());
}

// ── the quest script's three feeds ───────────────────────────────────────────
// FormIDs of every actor we are tracking, so the quest script can poll arousal
// for exactly those and nobody else.
// ── what OTHERS can see on her (2026-08-30) ──────────────────────────────────
// The same block as WornDeviceReport, filtered through the four-layer
// visibility rule and with the access line dropped. Rendered for the DIALOGUE
// TARGET, i.e. into the prompt of whoever she is talking to - which is exactly
// where SkyrimNet's own get_worn_equipment leaks the unfiltered truth today
// (report 30 §1).
static RE::BSFixedString Papyrus_WornDeviceReportVisible(RE::StaticFunctionTag*,
                                                         RE::Actor* a)
{
    return RE::BSFixedString(WornDevices::Report(a, true).c_str());
}

// ★ THE AFTER-STATE (2026-09-10). The wearer's text ADVANCES a three-prompt countdown (it is
// the decorator's clock); the onlooker's never does.
static RE::BSFixedString Papyrus_AftermathState(RE::StaticFunctionTag*, RE::Actor* a)
{
    return RE::BSFixedString(WornDevices::AftermathState(a).c_str());
}

static RE::BSFixedString Papyrus_AftermathVisible(RE::StaticFunctionTag*, RE::Actor* a)
{
    return RE::BSFixedString(WornDevices::AftermathVisible(a).c_str());
}

static std::vector<std::int32_t> Papyrus_TrackedActors(RE::StaticFunctionTag*)
{
    std::vector<std::int32_t> out;
    for (auto fid : WornDevices::TrackedActors())
        out.push_back(static_cast<std::int32_t>(fid));
    return out;
}

static void Papyrus_NoteArousal(RE::StaticFunctionTag*, RE::Actor* a, std::int32_t v)
{
    WornDevices::NoteArousal(a, static_cast<int>(v));
}

static void Papyrus_NoteDeviceName(RE::StaticFunctionTag*, RE::Actor* a,
                                   RE::BSFixedString cls, RE::BSFixedString name)
{
    WornDevices::NoteDeviceName(a, cls.c_str(), name.c_str());
}

// ★ The key registry's only writer (2026-09-08). DD keeps the unlocking key as a
// Papyrus PROPERTY on the inventory half (zadEquipScript.deviceKey), which C++
// cannot read - and at removal time we only ever hold the rendered half, which
// carries neither the script nor a name. The controller sees both halves at
// once in OnDeviceEquipped, so it pushes the pair here.
// ⚠ Int, not Form: passing an object through the VM boundary packs it as its
// most-derived attached script and the external type check refuses the upcast
// (report 23 §18). The FormID is resolved on this side.
// ⛔ SIGNATURE CHANGED 2026-09-10: the class string became the RENDERED FormID.
// This native and its Papyrus declaration must ship TOGETHER - an old .pex
// calling this with a String would fail to bind against the new DLL.
static void Papyrus_NoteDeviceKey(RE::StaticFunctionTag*, RE::Actor* a,
                                  std::int32_t renderedFid, std::int32_t keyFid,
                                  std::int32_t needed)
{
    WornDevices::NoteDeviceKey(a, static_cast<std::uint32_t>(renderedFid),
                               static_cast<std::uint32_t>(keyFid),
                               static_cast<int>(needed));
}

// ★ The exact-FormID name push (2026-09-13). Rides the same sweep as the key: the Controller
// resolves every worn inventory device's rendered half and pushes its name here, so the DLL
// never has to agree with a second class resolver to find it. Int FormID, as NoteDeviceKey.
static void Papyrus_NoteDeviceRecord(RE::StaticFunctionTag*, RE::Actor* a,
                                     std::int32_t renderedFid, RE::BSFixedString name)
{
    WornDevices::NoteDeviceRecord(a, static_cast<std::uint32_t>(renderedFid), name.c_str());
}

static void Papyrus_NoteClimax(RE::StaticFunctionTag*, RE::Actor* a)
{
    WornDevices::NoteClimax(a);
}

// ★ 1.3.5 the climax pose. Both registered WITHOUT callableFromTasklets (no NoWait flag), like any vanilla
// native. ⚠ CORRECTED 1.3.6: which thread that is in VR was not verified - the clip read does not rely on it;
// it takes the graph manager's own update lock and is SEH-guarded (WornDevices::ClipSecondsLeft).
static float Papyrus_ClipSecondsLeft(RE::StaticFunctionTag*, RE::Actor* a, RE::BSFixedString needle)
{
    return WornDevices::ClipSecondsLeft(a, needle.c_str());
}

static void Papyrus_NotePoseEnd(RE::StaticFunctionTag*, std::int32_t actorId)
{
    WornDevices::NotePoseEnd(static_cast<std::uint32_t>(actorId));
}

// ★ 1.3.8: DD's answer for the vibration DoVibrate ran (see WornDevices::NoteVibrateResult).
static void Papyrus_NoteVibrateResult(RE::StaticFunctionTag*, std::int32_t actorId, std::int32_t came)
{
    WornDevices::NoteVibrateResult(static_cast<std::uint32_t>(actorId), came);
}

// ★ 1.3.6/1.3.7: the DDSN_ClimaxPoseFaction rank for climax n - 4 kneel, 1 standing, 0 DD NG's own clip.
static std::int32_t Papyrus_ClimaxClipMode(RE::StaticFunctionTag*, RE::Actor* a, std::int32_t n)
{
    return WornDevices::ClimaxClipMode(a, n);
}

// ★ 1.3.9: is she talking in SkyrimNet right now (see WornDevices::IsTalking).
static bool Papyrus_IsTalking(RE::StaticFunctionTag*, RE::Actor* a)
{
    return a && WornDevices::IsTalking(a->GetFormID());
}

// The vibration/shock state heartbeat, called from the same 3 s poll that feeds
// arousal. Without it a vibration edge is only seen when an unrelated magic
// effect happens to wake the sink - see WornDevices.h::PollStates.
static void Papyrus_PollStates(RE::StaticFunctionTag*)
{
    WornDevices::PollStates();
}

// ── the clothing gate (2026-08-28) ───────────────────────────────────────────
// The eligibility primitive for every equip path that wants realism: true when
// nothing non-device covers the region the device needs. The future NPC→NPC
// SkyrimNet action calls this as its eligibility function; any mod may too.
static bool Papyrus_CanEquipDeviceOn(RE::StaticFunctionTag*, RE::Actor* target,
                                     RE::TESForm* device)
{
    return WornDevices::EquipBlockedBy(target, device).empty();
}

// The name of what is in the way, or "" - so a refusal can SAY why ("she is
// wearing an Iron Cuirass over that"), which is what makes the rule readable
// to an LLM instead of a silent veto.
static RE::BSFixedString Papyrus_EquipBlockedBy(RE::StaticFunctionTag*, RE::Actor* target,
                                                RE::TESForm* device)
{
    return RE::BSFixedString(WornDevices::EquipBlockedBy(target, device).c_str());
}

// A line into VRTE_DDZaZ.log from the AddOn's Papyrus (2026-09-10), so the key rule's
// outcome pairs with the gate's approval in ONE file instead of half of it sitting in the
// Papyrus log. The caller writes its own tag.
static void Papyrus_LogLine(RE::StaticFunctionTag*, RE::BSFixedString line)
{
    logger::info("{}", line.c_str());
}

static bool RegisterPapyrus(RE::BSScript::IVirtualMachine* vm)
{
    vm->RegisterFunction("SetScenePaused", "VRTE_DDZaZ_Native", Papyrus_SetScenePaused);
    vm->RegisterFunction("IsRestrained",   "VRTE_DDZaZ_Native", Papyrus_IsRestrained);
    vm->RegisterFunction("RestraintName",  "VRTE_DDZaZ_Native", Papyrus_RestraintName);
    vm->RegisterFunction("WornDeviceName", "VRTE_DDZaZ_Native", Papyrus_WornDeviceName);   // 1.3.1
    vm->RegisterFunction("WornDeviceReport", "VRTE_DDZaZ_Native", Papyrus_WornDeviceReport);
    vm->RegisterFunction("WornDeviceReportVisible", "VRTE_DDZaZ_Native",
                         Papyrus_WornDeviceReportVisible);
    vm->RegisterFunction("TrackedActors",   "VRTE_DDZaZ_Native", Papyrus_TrackedActors);
    vm->RegisterFunction("NoteArousal",     "VRTE_DDZaZ_Native", Papyrus_NoteArousal);
    vm->RegisterFunction("NoteDeviceName",  "VRTE_DDZaZ_Native", Papyrus_NoteDeviceName);
    vm->RegisterFunction("NoteDeviceKey",   "VRTE_DDZaZ_Native", Papyrus_NoteDeviceKey);
    vm->RegisterFunction("NoteDeviceRecord", "VRTE_DDZaZ_Native", Papyrus_NoteDeviceRecord);
    vm->RegisterFunction("NoteClimax",      "VRTE_DDZaZ_Native", Papyrus_NoteClimax);
    vm->RegisterFunction("ClipSecondsLeft", "VRTE_DDZaZ_Native", Papyrus_ClipSecondsLeft);   // 1.3.5
    vm->RegisterFunction("NotePoseEnd",     "VRTE_DDZaZ_Native", Papyrus_NotePoseEnd);       // 1.3.5
    vm->RegisterFunction("NoteVibrateResult", "VRTE_DDZaZ_Native", Papyrus_NoteVibrateResult); // 1.3.8
    vm->RegisterFunction("ClimaxClipMode",  "VRTE_DDZaZ_Native", Papyrus_ClimaxClipMode);    // 1.3.6
    vm->RegisterFunction("IsTalking",       "VRTE_DDZaZ_Native", Papyrus_IsTalking);         // 1.3.9
    vm->RegisterFunction("PollStates",      "VRTE_DDZaZ_Native", Papyrus_PollStates);
    vm->RegisterFunction("CanEquipDeviceOn", "VRTE_DDZaZ_Native", Papyrus_CanEquipDeviceOn);
    vm->RegisterFunction("EquipBlockedBy",   "VRTE_DDZaZ_Native", Papyrus_EquipBlockedBy);
    vm->RegisterFunction("LogLine",          "VRTE_DDZaZ_Native", Papyrus_LogLine);
    vm->RegisterFunction("AftermathState",   "VRTE_DDZaZ_Native", Papyrus_AftermathState);
    vm->RegisterFunction("AftermathVisible", "VRTE_DDZaZ_Native", Papyrus_AftermathVisible);
    logger::info("Registered Papyrus natives: SetScenePaused, IsRestrained, "
                 "RestraintName, WornDeviceReport.");
    return true;
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    InitLogging();
    SKSE::Init(skse);

    logger::info("==================================================");
    logger::info("SkyrimNet Devious Awareness Base v1.0.0 loaded.");
    logger::info("Standalone plugin — does not modify VRTouchEvents or PPB.");
    logger::info("==================================================");

    if (auto* papyrus = SKSE::GetPapyrusInterface()) papyrus->Register(RegisterPapyrus);
    if (auto* msg = SKSE::GetMessagingInterface()) {
        msg->RegisterListener(OnSKSEMessage);   // SKSE lifecycle ONLY — see above
        // ★ Dispatches FROM other plugins — PPB's kGetGateInterface. Without this
        // line the clothing gate is invisible to PPB and its no-layer fallback
        // answers instead. Registered here, inside SKSEPluginLoad, so we are
        // listening strictly earlier than any consumer can ask.
        msg->RegisterListener(nullptr, OnAnyPluginMessage);
    }

    // The wear clock is per-playthrough state. Without a co-save every device
    // reads as freshly applied after any reload, which would make the whole
    // duration feature quietly wrong rather than visibly broken.
    if (auto* ser = SKSE::GetSerializationInterface()) {
        ser->SetUniqueID(WornDevices::kRecordType);
        ser->SetSaveCallback(WornDevices::OnSave);
        ser->SetLoadCallback(WornDevices::OnLoad);
        ser->SetRevertCallback(WornDevices::OnRevert);
    }

    return true;
}
