#pragma once

// =============================================================================
// WornDevices — "what is on her, for how long, and how it feels by now."
//
// Two jobs, deliberately kept apart because they have different scopes:
//
//   1. DURATION, for EVERY DD device. One timestamp per (actor, worn device),
//      stamped from DD's own DDI_DeviceEquipped bus, cleared on DDI_DeviceRemoved.
//      ⚠ DD stamps this itself (zadEquipScript::DeviceEquippedAt, set for NPCs
//      too) but declares it under "Internal script variables" with no Property
//      keyword, so nothing outside that script can read it. Ours is a parallel
//      clock, not a duplicate of an accessible one. See Report 26 §4.
//
//   2. STRAIN, for the ~19 device classes that actually load or compress the
//      body. A gag, a blindfold and a plug are not postural restraint and do not
//      get a pain curve — they get their own sensation line instead. Inventing
//      curves for them would be writing fiction with no model behind it.
//
// ── THE CLOCK ────────────────────────────────────────────────────────────────
// The source table's phases are nominal MINUTES. The user's mapping: multiply by
// four and read them as GAME minutes, i.e. phase boundaries at 1 / 3 / 6 / 12
// GAME HOURS.
//
// ★ That is timescale-independent by construction and needs no setting. The
// boundaries never move; a player at timescale 20 simply lives in a faster world
// and reaches them sooner in real terms, which is correct — everything else in
// their game is faster too. At the user's timescale 4 the x4 cancels exactly and
// the real-time experience reproduces the source's own numbers (phase 4 at ~90
// real minutes), so their setting is the calibration point.
//
// ── THE RULE THAT MATTERS ────────────────────────────────────────────────────
// Reported intensity is NON-MONOTONE. It climbs to phase 4 and then FALLS at
// phase 5, because phase 5 is nerve conduction block: she reports less while
// tissue damage accrues fastest. A naive reader infers "less pain -> improving",
// which is inverted exactly where being wrong costs most. The prompt states this
// as a standing rule rather than repeating it per line — cheaper in tokens, and
// it is a rule about how to READ the state, not part of the state itself.
// =============================================================================

#include "PCH.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace WornDevices {

    // ── arming ───────────────────────────────────────────────────────────────
    // Attaches two ENGINE event sinks. Call once at kDataLoaded.
    //
    // ⚠ WHY ENGINE SINKS AND NOT PAPYRUS. This AddOn ships no ESP, so there is
    // no quest and no alias — nothing for a script instance to live on, which
    // means no `RegisterForModEvent` and no `OnUpdate` anywhere in the mod
    // (the same constraint that forces the DLL to dispatch decorator
    // registration, report 23 §29). And a C++ sink cannot hear DD's own
    // DDI_DeviceEquipped: it carries three FORMS through ModEvent.PushForm,
    // while SKSE::ModCallbackEvent exposes only strArg/numArg/sender.
    //
    // TESEquipEvent is better than either. It is the engine's own, fires for
    // every actor on every runtime, needs no ESP and no Papyrus, and catches
    // ZaZ and Diary-of-Mine gear too — none of which raises a DDI event at all.
    //
    // Also arms TESSpellCastEvent (the magicka-reactive trigger, below) and
    // resolves the vanilla location keywords. ⚠ kDataLoaded and no earlier:
    // TESDataHandler::LookupForm needs the resolved load order, and parsing
    // sooner turns every keyword into a silent nullptr — which would leave the
    // whole context classifier answering "wilderness" forever with nothing in
    // the log to say why.
    void Install();

    // ── CONTEXT ──────────────────────────────────────────────────────────────
    // Where she is, and whether anyone who is not the player or one of the
    // player's own followers can currently see her. Together these decide how
    // openly a device is allowed to express itself.
    enum class Context { Unknown, PrivateHome, Guild, Inn, Town, Dungeon, Wilderness };
    Context     Classify(RE::Actor* a);
    const char* ContextWord(Context c);
    bool        StrangerWatching(RE::Actor* a);

    // Everything she is wearing, how long, and how it feels — the block the
    // 0917 prompt renders. Empty string when she wears no covered device, which
    // is the common case and must cost nothing.
    // observerView=false: the wearer's own block, everything, access line and
    // all. observerView=true: only what someone else could SEE on her - the
    // four-layer visibility rule, no access line. See VisibleOn in the .cpp and
    // report 30 §1-§2 for why this exists (SkyrimNet's own equipment dump
    // already leaks covered devices into the live dialogue prompt).
    std::string Report(RE::Actor* a, bool observerView = false);

    // ★ THE AFTER-STATE (2026-09-10) - what a device leaves behind once it is off, as a
    // prompt block rather than an event. AftermathState: the wearer's text for THIS prompt,
    // advancing a three-prompt countdown per strain tier (full, easing, fading; 5-second
    // floor), "" when nothing is left. AftermathVisible: what an onlooker can see of it, for
    // the first two of those prompts; it never advances anything.
    // ★ 1.3.0: `advance` = the decorator's prompt-counted countdown (the default, unchanged);
    // false = the state FILE's reading, graded by elapsed TIME (a file is read on every render and
    // cannot count prompts) and never mutating. `forFile` on the visible half likewise.
    std::string AftermathState(RE::Actor* a, bool advance = true);
    std::string AftermathVisible(RE::Actor* a, bool forFile = false);

    // ★★ THE PROMPT STATE FILE (1.3.0) - everything the 12 prompts used to pull through Papyrus
    // decorators, as one snapshot per actor for PromptState to write (see PromptState.h).
    struct PromptFields {
        std::string name, worn, wornVisible;
        std::string gag, blind, arms, legs, all, deaf;            // restraint labels, "" = free
        std::string after, afterVisible;                          // the after-state, "" = nothing left
        std::string ungag, unblind, undeaf, unboundArms, unboundLegs, unboundAll;   // "3"/"2"/"1"/""
    };
    std::vector<std::uint32_t> PromptActors();          // tracked + after-state + release countdown
    bool PromptEntry(RE::Actor* a, PromptFields& out);  // false = nothing to publish for her

    // ★ 1.3.1 (VRTouchEvents' request, 2026-09-14): the real display name of the device she wears on
    // any bit of `slotMask` - the inventory half's name through NameForDevice (pushed name -> pair
    // table -> class push). "" when nothing covered is worn there or no name is known. A tracked
    // device wins over ordinary clothing on the same bits.
    std::string WornDeviceName(RE::Actor* a, std::uint32_t slotMask);

    // "" the layer question asked backwards: can a bystander see this device on
    // this actor right now? Fails VISIBLE on anything unclassifiable.
    bool VisibleOn(RE::Actor* wearer, RE::TESObjectARMO* device);

    // The device's LAYER as a plain int for the public C++ gate interface:
    // -1 none / 0 internal / 1 under / 2 mid / 3 outer. Same resolution order
    // the gate itself uses (class table -> site keywords -> the user's INI).
    int LayerIdOf(RE::TESObjectARMO* device);

    // Is this a device the AddOn recognises at all (DD / ZaZ / DoM)? Public
    // wrapper over the file-local IsCoveredDevice (which stays in its anonymous
    // namespace - declaring THAT here made every internal call ambiguous).
    bool IsKnownDevice(RE::TESObjectARMO* device);

    // ── PUSHED IN FROM THE ESP'S QUEST SCRIPT ────────────────────────────────
    // All three exist because the DLL cannot reach the facts on its own:
    //   NoteArousal    - OSL Aroused's value lives behind a Papyrus global
    //                    native; the sla_Arousal faction this used to read is
    //                    never written in this load order.
    //   NoteDeviceName - a DD device's name is on the INVENTORY half, which is
    //                    not worn on any biped slot; only zadlibs resolves it.
    //   NoteClimax     - DD's DeviceActorOrgasmEx carries the actor as a Form,
    //                    which replaces guessing from an arousal drop.

    // ── the gesture claim ────────────────────────────────────────────────────
    // Called by DeviceEquip immediately BEFORE one of our own hand gestures
    // equips or removes a device. The TESEquipEvent sink narrates every device
    // change it sees; a claimed one is ours and already narrated through its
    // own event, so the sink stays quiet for it.
    //
    // ⚠ KEYED ON THE ACTOR, NOT ON THE DEVICE. The equip side only ever holds
    // the INVENTORY half (DoEquip calls EquipItemEx on it and DD's OnEquipped
    // chain equips the rendered half), while the sink is handed the RENDERED
    // half — so a device-keyed claim could never match an equip.
    // ⚠ A WINDOW, NOT A TOKEN: one gesture can produce two device changes, so
    // it is not consumed on first match.
    // ⚠ PLUGS BYPASS IT ENTIRELY — see the ledger comment in the .cpp.
    void ClaimGesture(std::uint32_t actorFid);

    // ★ D1 (2026-09-13): the device detail for a HAND equip PPB just confirmed - the name,
    // the worn block's own mechanism line + removal clauses (`detail`), and what an onlooker
    // could see (`seen`, "" when not visible). False = nothing to say (no covered device on
    // `slotMask`, a plug, or an unmapped class). See the .cpp banner.
    bool FittedLines(RE::Actor* a, std::uint32_t slotMask, const char* heldName, bool locked,
                     std::string& label, std::string& detail, std::string& seen);

    // ⛔ THE VIBRATION EDGE CAN BE MISSED WITHOUT THIS (found 2026-08-27).
    // Verified in DD's own source: zadLibs.psc:2347-2366 - SetVibrating does
    // `akActor.SetFactionRank(zadVibratorFaction, duration)` and StopVibrating
    // does SetFactionRank(...,0) + RemoveFromFaction. A FACTION RANK CHANGE
    // APPLIES NO MAGIC EFFECT.
    //
    // But ReEvaluate - the only reader of that edge - was reached ONLY from the
    // TESActiveEffectApplyRemoveEvent sink. So a vibration started by DD's own
    // NPC loop (or by any third mod) was noticed only when some UNRELATED
    // effect edge happened to wake the sink on that actor, and for a short one
    // - DD rolls its own 5..20 s - BOTH edges could be missed entirely.
    // ⚠ Shocks were never affected: ShockActor does a real ShockEffect.RemoteCast
    // (zadLibs.psc:2518), which wakes the sink reliably. Vibration only.
    //
    // The quest script already polls every 3 s over exactly TrackedActors() for
    // arousal, so this rides that existing timer and adds no machinery. It
    // bounds the edge at 3 s instead of at "whenever something else happens".
    void PollStates();

    // ── THE CLOTHING GATE (2026-08-28) ──────────────────────────────────────
    // "" = the device may be equipped on the target; otherwise the display name
    // of the non-device garment covering the region it needs. The QUERY only:
    // the NPC→NPC SkyrimNet action consumes it as eligibility, PPB enforces the
    // gesture path (request filed), and the menu path is deliberately open.
    // Accepts either half of a DD pair. Fails OPEN on an unresolvable class.
    std::string ClothingBlockOn(RE::Actor* target, RE::TESForm* device);

    // ★ THE SLOT GATE (2026-09-10). "" = no worn DEVICE holds one of this device's
    // biped slots; otherwise that device's display name. Pass the RENDERED half.
    // Devices only - a garment in the slot stays the layer model's question.
    std::string DeviceSlotConflict(RE::Actor* target, RE::TESForm* device);

    // The whole equip question: DeviceSlotConflict, then ClothingBlockOn.
    // Every equip caller asks this one, so no two of them can disagree.
    std::string EquipBlockedBy(RE::Actor* target, RE::TESForm* device);

    std::vector<std::uint32_t> TrackedActors();

    // The pushed inventory-half name for a class ("" when none arrived yet).
    // Consumed by DeviceEquip's registry to tell a Restrictive glove from a
    // plain one - the rendered half is nameless (report 23 §30).
    std::string PushedName(std::uint32_t actorFid, const char* cls);

    // ── THE KEY REGISTRY (2026-09-08) ───────────────────────────────────────
    // Devious Devices puts the unlocking key on the INVENTORY half, as script
    // properties `deviceKey` (a Key form) and `NumberOfKeysNeeded`
    // (zadEquipScript.psc:32,35). C++ cannot read a Papyrus property, and the
    // half we are ever handed at removal time is the RENDERED one, which
    // carries neither. So the AddOn's own controller pushes the pair in when DD
    // announces an equip - the same shape as NoteDeviceName, and for the same
    // reason.
    // ⛔ 2026-09-10: keyed on the RENDERED FormID, not the device class - the
    // class key needed two resolvers (DD's announced keyword vs our ClassOf) to
    // agree, and they can differ on a multi-keyword device. keyFid 0 = DD holds
    // no key form for this device, which in DD means NO KEY IS REQUIRED.
    void NoteDeviceKey(RE::Actor* a, std::uint32_t renderedFid,
                       std::uint32_t keyFid, int needed);

    // "" = removable. Otherwise a ready-to-show reason. See
    // VRTEDDZ::IVrteGateInterface1::RemovalBlockedBy for the contract.
    const char* RemovalBlockedBy(RE::Actor* wearer, RE::TESObjectARMO* device,
                                 bool byHand);

    // 0 ALLOW / 1 BLOCKED / 2 UNKNOWN-never-seen. Build 3; PPB asked for the
    // third state so an absence of evidence stops being served as a refusal.
    int RemovalStateOf(RE::Actor* wearer, RE::TESObjectARMO* device, bool byHand);

    // One-shot: the last KEYED removal approval for this actor, if younger than
    // maxAgeS. The AddOn's bridge spends it when PPB confirms the unlock, to hand
    // DD's key-consumption rule to VRTEDD_Keys. False = nothing keyed was approved.
    bool TakeKeyedAllow(std::uint32_t actorFid, double maxAgeS, std::uint32_t& renderedFid,
                        std::uint32_t& keyFid, int& needed);

    // ★ Is at least one worn hood-keyworded device a REAL hood (i.e. not a
    // mis-tagged blindfold)? The deaf row asks this instead of the bare
    // keyword, so a blindfold cannot deafen but an actual hood still does -
    // including when she wears both at once.
    bool WearsRealHood(RE::Actor* a);
    // ★ 2026-09-12: true only if something worn genuinely blocks sight - the
    // Extreme Hood (Open) carries zad_DeviousBlindfold but has eye holes.
    bool WearsSightBlocker(RE::Actor* a);
    // ★ 2026-09-12: true only for a FULL-grade boot - the ones that hobble.
    bool WearsFullGradeBoot(RE::Actor* a);
    void NoteArousal(RE::Actor* a, int value);
    void NoteDeviceName(RE::Actor* a, const char* cls, const char* name);
    // ★ 2026-09-13: the same name, filed under the exact RENDERED FormID (like the key), pushed
    // by the Controller's 30 s sweep for EVERY worn device and by the equip edge. The class
    // push above stays as a fallback. Erased with the device; see the .cpp banner (D4/D9/D10).
    void NoteDeviceRecord(RE::Actor* a, std::uint32_t renderedFid, const char* name);
    void NoteClimax(RE::Actor* a);
    // ★ 1.3.5 the climax pose: seconds left on the newest active clip whose animation name contains
    // `needle` (< 0 = none found), and the pose's end (the lanes may start things on her again).
    float ClipSecondsLeft(RE::Actor* a, const char* needle);
    void NotePoseEnd(std::uint32_t fid);
    // ★ 1.3.8: VibrateEffect's return for the vibration DoVibrate just ran (0 = ran, no climax, no edge -> the
    // 100 lane re-arms so the device goes off again at full arousal).
    void NoteVibrateResult(std::uint32_t fid, int came);
    // ★ 1.3.6/1.3.7: the DDSN_ClimaxPoseFaction rank ClimaxPose sets for climax number n (4 kneel, 1 standing,
    // 0 = no rank, DD NG's own device clip).
    int ClimaxClipMode(RE::Actor* a, int n);
    // ★ 1.3.9: whether this NPC is talking in SkyrimNet right now (reply being written, voiced, or between two of
    // her queued lines), and whether a worn device fills or covers the mouth (gags of every framework, all hoods).
    bool IsTalking(std::uint32_t fid);
    bool IsSpeechDevice(RE::TESObjectARMO* w);

    // SKSE co-save. The clock is per-playthrough state and must survive a load;
    // without this every device reads as "just put on" after every reload.
    void OnSave(SKSE::SerializationInterface* intf);
    void OnLoad(SKSE::SerializationInterface* intf);
    void OnRevert(SKSE::SerializationInterface* intf);

    inline constexpr std::uint32_t kRecordType = 'VDWD';   // VRTE DD Worn Devices
    // ⛔ THIS WAS 1 WHILE OnSave WROTE THE v2 ROPE BYTE (found 2026-08-27, by a
    // cold read). OnSave wrote the per-device rope flag unconditionally but
    // OnLoad only reads it `if (version >= 2)` - so every load left one byte
    // per device unread and the stream MISALIGNED from the second device on.
    // A single-device actor survived; a fully-kitted one - the case the model
    // exists for - lost her whole wear clock on every reload. One character.
    inline constexpr std::uint32_t kVersion    = 2;
}
