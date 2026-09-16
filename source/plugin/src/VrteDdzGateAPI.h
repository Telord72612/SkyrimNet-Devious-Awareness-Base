#pragma once
// ═══════════════════════════════════════════════════════════════════════════════════════
//  VrteDdzGateAPI.h — the PUBLIC clothing-gate interface of the VRTE DD/ZaZ AddOn.
//
//  Copy this single header into your project. Deliberately self-contained: no CommonLib
//  types, no SKSE types beyond the messaging call you already make, actors and devices are
//  addressed by FormID. Mirrors PpbTouchAPI.h's conventions on purpose.
//
//  ── WHAT YOU GET ─────────────────────────────────────────────────────────────────────
//  The AddOn owns what a device IS and where it may go. This interface answers one
//  question synchronously: MAY THIS DEVICE GO ON THIS ACTOR RIGHT NOW? — using the user's
//  own four-layer model over all 1,553 catalogued devices (DD + ZaZ + Diary of Mine),
//  their per-region masks, the heavy-armor rule, the four device-blocks-device rules (a face
//  device over a hood/face item, a collar over a collar, INTERNAL under a belt/suit, and a
//  worn device on the same biped slot) and DD's own zad_Permit* exceptions.
//
//      INTERNAL  plugs, piercings   the site must be bare of clothes AND of covering
//                                   devices (belt/suit honouring zad_PermitVaginal/Anal;
//                                   a piercing uses bra/suit vs zad_ExposedBreasts)
//      UNDER     suits, harnesses,  install needs the region bare; clothes may go OVER
//                belts, gags,       afterwards
//                blindfolds, hoods
//      MID       corsets, boots,    replaces clothing: region must be bare, nothing on top
//                binders, posture
//                collars
//      OUTER     yokes, cuffs,      goes over anything — EXCEPT: only HEAVY armor blocks at
//                collars, shackles  the arms/feet; a hood/gag/face-slot item blocks a cloth
//                                   gag; a worn collar blocks another collar
//
//  ── ACQUIRING THE INTERFACE ──────────────────────────────────────────────────────────
//  The same request/reply pattern HIGGS, PLANCK and PPB use. Any time at or after kPostLoad:
//
//      VRTEDDZ::GateMessage msg{};
//      SKSE::GetMessagingInterface()->Dispatch(VRTEDDZ::GateMessage::kGetGateInterface,
//                                              &msg, sizeof(msg), "VRTE_DDZaZ");
//      if (msg.GetApiFunction)
//          g_gate = static_cast<VRTEDDZ::IVrteGateInterface1*>(msg.GetApiFunction(1));
//
//  A null GetApiFunction means the AddOn is not installed (or too old) — in that case KEEP
//  YOUR OWN FALLBACK: never hard-depend on us. A null return from GetApiFunction(N) means
//  we do not speak revision N; ask for a lower one.
//
//  ── VERSIONING CONTRACT ──────────────────────────────────────────────────────────────
//  * The vtable is APPEND-ONLY. Methods are never reordered, removed or re-signatured.
//  * There is deliberately NO virtual destructor: slot 0 is GetBuildNumber, forever.
//  * Returned strings are owned by us and stable until the NEXT call on the same thread.
//    Copy before you call again.
//
//  ── THREADING ────────────────────────────────────────────────────────────────────────
//  * MAIN THREAD ONLY, and cheap: a keyword walk plus one worn-armor pass. No Papyrus, no
//    VM round-trip, no allocation on the hot path. Safe to call inside a gesture tick.
//
//  ⛔ ── THE ONE TRAP THAT MATTERS: PASS THE RENDERED HALF ──────────────────────────────
//  A DD device is a PAIR. What the player holds is the INVENTORY half, and MEASURED across
//  the whole catalogue: 0 of 1,221 inventory halves carry ANY biped slot or ANY class
//  keyword. Ask about that half and the honest answer is "no opinion", which FAILS OPEN.
//  ★ You already resolve this — your own log prints it:
//        [EQUIP] device 0x2A04DBEF 'Black Leather Restrictive Collar'
//                -> sites neck (DD) cls='Collar' rendered=0x2902AFA5
//    Pass that `rendered` FormID as `deviceFid` whenever it is non-zero; pass the held base
//    only when there is no pair (ZaZ and DoM records are single, so the held base IS the
//    real record). If you pass an inventory half we answer "allowed" (its region resolves to
//    nothing; the miss is logged only when a name-keyed rule matched it), so the failure mode
//    is your current behaviour, never a false refusal.
// ═══════════════════════════════════════════════════════════════════════════════════════

#include <cstdint>

namespace VRTEDDZ
{
    // The four layers, as the user defined them. Returned by GetLayer().
    enum Layer : int
    {
        kLayerNone     = -1,   // not a device we know, or unclassifiable -> we never refuse
        kLayerInternal = 0,
        kLayerUnder    = 1,
        kLayerMid      = 2,
        kLayerOuter    = 3,
    };

    class IVrteGateInterface1
    {
    public:
        // slot 0, forever. Increments when behaviour changes; never renumber.
        virtual std::uint32_t GetBuildNumber() = 0;

        // ★ THE ONE YOU WANT. "" (empty, non-null) = ALLOW.
        // Otherwise: the display name of what is in the way, ready to show or narrate
        // ("Necromancer Black Robes", or a fallback like "the clothing worn there").
        // ⚠ Pass the RENDERED half as deviceFid — see the trap above.
        // ★ BUILD 4 (2026-09-10): ALSO refuses a device whose biped slot is already held by a
        // worn DEVICE, and names that device ("Black Leather Ball Strap Gag") - the engine
        // cannot keep two armors in one slot, and a locked device will not be pushed off.
        // No new slot: your existing release refusal shows it as it is.
        virtual const char* BlockedBy(std::uint32_t actorFid, std::uint32_t deviceFid) = 0;

        // Convenience: BlockedBy(...)[0] == '\0'. Same rules, same caveats.
        virtual bool CanEquip(std::uint32_t actorFid, std::uint32_t deviceFid) = 0;

        // Which layer this device installs at, if you want to reason yourself.
        virtual int GetLayer(std::uint32_t deviceFid) = 0;

        // Is this a device the AddOn recognises at all (DD / ZaZ / DoM)?
        // False means we have no opinion and you should use your own judgement.
        virtual bool IsDevice(std::uint32_t deviceFid) = 0;

        // Bonus, free from the same model: can an ONLOOKER see this device on this actor
        // right now? (INTERNAL never — except a tail plug, whose tail always shows;
        // UNDER only while its region is bare; MID/OUTER always.) Not needed for the gate;
        // exposed because you already know who is looking.
        virtual bool VisibleOn(std::uint32_t actorFid, std::uint32_t deviceFid) = 0;

        // ═══════════════════════════════════════════════════════════════════
        // APPENDED AT BUILD 2 (2026-09-08). Slots 0-5 above are unchanged, so a
        // consumer compiled against build 1 keeps working untouched and simply
        // never calls these. Check GetBuildNumber() >= 2 before you do.
        // ═══════════════════════════════════════════════════════════════════

        // ★ THE REMOVAL TWIN OF BlockedBy. "" (empty, non-null) = ALLOW REMOVAL.
        // Otherwise: a ready-to-show reason the device will not come off
        // ("You are missing the key to remove this device.", "the chastity belt
        // over it").
        //
        // WHY THIS EXISTS. Taking a device OFF is the same class of question as
        // putting one on - it depends on the layer model, what is worn over it,
        // and whether the lock will open - and all three of those live here. A
        // consumer that answers it locally ends up with a second, drifting copy
        // of a model it does not own. That is the same reasoning that put
        // BlockedBy here rather than in the caller.
        //
        // WHAT IT FOLDS IN, so you do not have to:
        //   * the lock: zad_Lockable / zad_QuestItem / zad_BlockGeneric
        //   * THE KEY: whether the actor doing the removing actually holds the
        //     right DD key, and enough of them (DD's own deviceKey +
        //     NumberOfKeysNeeded, per device - not a blanket "any key")
        //   * the layer model: something worn OVER it that must come off first
        //
        // ⚠ Pass the RENDERED half as deviceFid, exactly as for BlockedBy.
        // ⚠ actorFid is WHO IS WEARING IT. The key is looked for on the PLAYER,
        //   matching Devious Devices' own rule.
        // ⚠ `byHand` distinguishes the two removal routes and they have
        //   different rules: a two-hand pull (false) must satisfy the lock; a
        //   FINGER extraction (true) is exempt from the lock and gated on the
        //   covering garment instead - a plug can be worked loose by hand
        //   unless a belt is over it.
        virtual const char* RemovalBlockedBy(std::uint32_t actorFid,
                                             std::uint32_t deviceFid,
                                             bool          byHand) = 0;

        // Convenience: RemovalBlockedBy(...)[0] == '\0'. Same rules.
        virtual bool CanRemove(std::uint32_t actorFid, std::uint32_t deviceFid,
                               bool byHand) = 0;

        // ═══════════════════════════════════════════════════════════════════
        // APPENDED AT BUILD 3 (2026-09-08), on PPB's request. Slots 0-7 above
        // are unchanged. Gate on GetBuildNumber() >= 3.
        // ═══════════════════════════════════════════════════════════════════

        //   0 = ALLOW    — RemovalBlockedBy returns ""
        //   1 = BLOCKED  — RemovalBlockedBy carries the reason; show it
        //   2 = UNKNOWN  — we have NEVER SEEN this device equipped, so we hold
        //                  no key for it. THIS IS NOT A REFUSAL. Fall back to
        //                  your own rule and your own wording.
        //
        // ★ WHY THIS IS A NEW SLOT AND NOT A SENTINEL FROM RemovalBlockedBy.
        // That method's published contract says its return is "empty,
        // non-null". Narrowing an existing method's return domain is a silent
        // break for anyone who already trusted it - the same append-only rule
        // that keeps slot 0 as GetBuildNumber forever, and the same lesson the
        // HDT-SMP v1/v2 destructor split taught. A new slot is free.
        //
        // ★ WHY THE THIRD STATE EXISTS AT ALL (PPB's argument, and it is
        // right). Reporting UNKNOWN as BLOCKED refuses the device with OUR
        // sentence about a key the player may well be holding - which is
        // precisely how the message this whole exchange set out to fix began
        // lying. On UNKNOWN, the honest answer is "we do not know", and the
        // caller's own rule should decide.
        //
        // ⚠ UNKNOWN IS NOT "FAIL OPEN". A device we have no record of should
        // still not fall off; it should simply be refused for the caller's
        // reason, with the caller's wording, instead of ours.
        //
        // ⚠ Which devices are UNKNOWN, and why it is temporary: we learn a
        // device's key when DD announces its equip, so anything worn BEFORE
        // this build went in - or equipped while our plugin was absent - is
        // unknown until DD re-announces it, which it does on cell load.
        virtual int RemovalStateOf(std::uint32_t actorFid, std::uint32_t deviceFid,
                                   bool byHand) = 0;
    };

    struct GateMessage
    {
        enum { kGetGateInterface = 0x56445A47 };   // 'VDZG'
        void* (*GetApiFunction)(unsigned int revision) = nullptr;
    };
}
