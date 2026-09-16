#pragma once
#include "PCH.h"     // RE::Actor for the restraint registry below
#include <string>

// =============================================================================
// DeviceEquip - the VRTE DD-ZaZ add-on's NARRATION half.
//
// HISTORY, in two moves. This file was `OrificeOpen.{h,cpp}` and drove the gape
// bones itself; on 2026-08-20 PPB took over orifice opening for all three
// orifices and the whole drive path was deleted. On 2026-08-27 PPB took the
// GESTURES too - press-to-equip, the two-hand undress, the fingertip plug
// extraction - and they are PPB/src/DeviceGesture.cpp + PPB_DeviceEquip.psc now.
//
// The name stayed because main.cpp, the Papyrus natives and the log tags all
// use it, and renaming a namespace is churn that buys nothing.
//
// THE BOUNDARY (the user's, and it is right)
//   PPB owns bodies, capsules, collision, the orifices, and now the gestures:
//   it owns the sensor, so it owns detection. Two gesture detectors would drift,
//   and silently.
//   This add-on owns MEANING - what a device is, what wearing it implies, and
//   how any of it should be told to SkyrimNet.
//
// WHAT IS LEFT IN THE .CPP
//   1. THE TRANSLATION SINK. PPB emits generic PPB_Gesture* events; this renames
//      them into the VRTE_DDZaZ_* vocabulary. That indirection is the whole
//      architecture: base VRTouchEvents never learns DD or NSFW concepts, so it
//      stays SFW and everything that knows what a plug is lives on this side.
//      It also CLAIMS gestures for WornDevices (so its TESEquipEvent sink can
//      tell "the player's hands did this" from "a menu did this").
//      ⛔ PPB_PlayerMasturbation is NOT relayed any more (2026-09-13): the user moved it into
//      base VRTouchEvents, which consumes PPB's event directly. Do not re-add a relay.
//   2. THE RESTRAINT REGISTRY - which worn gear blocks speech / sight / arms /
//      legs, from built-in DD/ZaZ coverage plus VRTE_DDZaZ_Restraints.ini.
//      Consumed by the SkyrimNet decorators through the natives in main.cpp.
//   3. THE DECORATOR REGISTRATION.
//
// CONFIG: the gesture knobs moved with the gestures, into
// `Precision Physic Bodies/SKSE/Plugins/PPB_tuning.txt` under "GESTURE DEVICE
// LAYER", same key names. This mod's own VRTE_DDZaZ.ini is now a signpost that
// nothing reads; only VRTE_DDZaZ_Restraints.ini is still parsed.
// =============================================================================

namespace DeviceEquip {
    // Arm the mod-event sink: PPB_Gesture* -> VRTE_DDZaZ_* and gesture claims.
    // Idempotent - called at kPostPostLoad and retried
    // at kDataLoaded, the PPB/AIHands pattern. It no longer acquires PPB or
    // HIGGS: nothing here needs an interface any more, only mod events.
    void Install();

    // No-op since the split - this file holds no gesture state. Kept because
    // main.cpp calls it on every load boundary.
    void Reset();

    // ! LEGACY. VRTE_DDZaZ_Native.SetScenePaused lands here. Pausing now stops
    // this add-on relaying AND stands PPB's gesture layer down, so the request
    // is forwarded across the split via the PPB_GestureSetPaused mod event.
    // New callers should use PPB_Native.SetGesturePaused(bool) directly.
    void SetPaused(bool paused);

    // ★ THE UNDRESS HOLD (2026-09-10). PPB announces a finished two-hand pull BEFORE it
    // asks the removal gate, so a refused pull was narrated as done. The bridge holds
    // that announcement; the gate's verdict and PPB_GestureUnlocked release or cancel
    // it. Called by WornDevices::RemovalCheck for every non-finger verdict
    // (0 ALLOW / 1 BLOCKED / 2 UNKNOWN).
    void OnRemovalVerdict(std::uint32_t actorFid, int state);

    // Resolve holds whose signals never all came (4 s). Rides WornDevices::PollStates.
    void TickHolds();

    // Is DD SN AddOn.esp loaded? False until the load order is readable.
    bool AddOnPresent();

    // ★ Is a SexLab / OStim scene running, or did one end within the last 15 s? The plug
    // narration, the device on/off lines, DeviceFitted and (since 2026-09-13, D5) every effect
    // line consult it. The grace is D2: DD's zadBQ00 strips plugs at AnimationStart and re-fits
    // them after AnimationEnd, and the re-fit arrived after the flag had cleared.
    bool SceneOnNow();

    // ★ THE DEVICE NARRATION (2026-09-10): hand one noticed device change to the AddOn's
    // VRTEDD_Narrate. No-op without the AddOn; a plug during a scene is dropped; a plug on a
    // choked actor goes out persistent. `payload` is EmitDeviceChange's, unchanged.
    void NarrateDeviceChange(std::uint32_t actorFid, bool plug, bool on, const char* payload);


    // Register this AddOn's SkyrimNet decorators (currently `vrtedd_gag_name`,
    // which lets a gagged NPC's dialogue prompt know she cannot speak).
    // ⚠ Must run once per SESSION: decorators are not saved, and this AddOn has
    // no ESP and therefore no quest script to do it from OnInit. Dispatched at
    // kPostLoadGame and kNewGame, when the VM and SkyrimNet are both up.
    void RegisterDecorators();

    // ── the restraint registry (2026-08-24) ──────────────────────────────────
    // Five categories, indexed 0..4: gag, blind, arms, legs, all. Devious
    // Devices / ZaZ / DoM gear is built in; SKSE/Plugins/VRTE_DDZaZ_Restraints.ini
    // lets a user ADD their own gear (by name, FormID or worn keyword) and
    // spells. The INI can only add, never remove, so a broken line degrades to
    // "DD still works" instead of to silence.
    //
    // ⚠ Call LoadRestraints() at kDataLoaded and no earlier: `0x12345~Plugin.esp`
    // needs the data handler and the resolved load order.
    void LoadRestraints();
    bool IsRestrained(RE::Actor* a, int cat);

    // The restraining item's display name, or "". ⚠ "" is NOT "unrestrained" —
    // DD's worn half has no FULL record, so ask IsRestrained separately.
    std::string RestraintName(RE::Actor* a, int cat);
}
