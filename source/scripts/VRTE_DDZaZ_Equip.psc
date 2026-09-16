Scriptname VRTE_DDZaZ_Equip Hidden
; ================================================================
; VRTE DD/ZaZ AddOn - the Papyrus half of the DEVICE EFFECTS.
;
; * 2026-08-27: DoEquip / DoEject / DoUnequipDevice MOVED OUT of this script and
; into PPB_DeviceEquip.psc, alongside the gesture detection that calls them.
; This script keeps only the effects the add-on's own DLL drives: vibration,
; shock, and the combat stun. Nothing here is called by PPB.
;
; WHY THE DLL CANNOT CALL DD DIRECTLY (the reason this script exists at all):
; the DLL cannot pass OBJECTS into a dispatched Papyrus call. The VM types a
; packed reference as its most-derived ATTACHED script (a held DD plug arrived
; as `zadPlugScript`) and the external-dispatch type check refuses to upcast
; script types to Form. Papyrus itself upcasts these fine - so the DLL passes
; raw FormIDs as Ints and THIS script resolves and calls. Plain-value static
; dispatch is the proven-working path (Debug.Notification uses it).
; ================================================================

; ================================================================
; * A WORN DEVICE RESPONDS TO MAGICKA (2026-08-26)
; ================================================================
; Both of these are called from the DLL's TESSpellCastEvent sink, with Ints only
; (the object-dispatch trap above applies to every call in this file).
;
; ⚠ WE ARE CALLING DD, NOT COPYING IT. `zadLibs.VibrateEffect` is a public
;   Papyrus function and this is an ordinary call into it - the same footing as
;   LockDevice and UnlockDeviceByKeyword above. DD then performs its own sound,
;   moan, facial expression, animation, arousal and climax, from its own assets.
;   Nothing of DD is copied, edited or redistributed by this mod.
;
; ⚠ WHY THIS FILLS A REAL GAP. DD ships `zad_EffectVibrateOnSpellCast` and even
;   writes SpellCastVibrate(akActor, ...) generically - but its ONLY caller is
;   zadPlayerScript's OnSpellCast, hardcoded to the PLAYER. On an NPC the keyword
;   has never done anything. And DD's NPC event loop is plug-only, so vibrating
;   PIERCINGS never fire on an NPC either.

; abTease=1 -> DD edges her instead of running ActorOrgasm.
; ★ The DLL passes 1 deliberately. ActorOrgasm plays DDZazHornyE, which is a
;   SUPINE FLOOR POSE - knees up, legs apart, no transition in or out - and it
;   holds for up to 20 seconds. It is not something to drop on an NPC anywhere a
;   person might be standing. Edging keeps the response to something that reads
;   as a stumble in her composure.
; aiClimaxNum: 0 = edge only (no finish permitted). Anything higher permits the finish (it is
; the number this climax would be; the pose itself takes its number from the DLL - 1.3.5).
Function DoVibrate(Int actorId, Int strength, Int aiClimaxNum) Global
    Actor a = Game.GetFormEx(actorId) as Actor
    ; ★ D3 (the user, 2026-09-13): nothing runs on an unconscious actor (the DLL's
    ; ReEvaluate holds the lane upstream; this is the belt to that brace).
    if !a || a.IsDead() || a.IsUnconscious()
        return
    endif
    zadlibs libs = Game.GetFormFromFile(0x00F624, "Devious Devices - Integration.esm") as zadlibs
    if !libs
        return
    endif
    ; ★ 1.3.7: the climax pose's marker is DD SN's OWN faction now (DDSN_ClimaxPoseFaction, DD SN Database.esp
    ; 0x801) - never the shared vanilla actor value Variable03 (the user, 2026-09-15: "make sure it's our own
    ; variable"); DD SN no longer writes Variable03 anywhere. A pose ends by removing the rank; this also drops
    ; one a pose could not (a stack lost across a load), so DD's own edge can never borrow our clip choice.
    ; (Safe to drop here: the DLL never dispatches DoVibrate while a climax pose is playing on her.)
    Faction poseFac = Game.GetFormFromFile(0x801, "DD SN Database.esp") as Faction
    if poseFac
        a.RemoveFromFaction(poseFac)
    endif
    ; duration 0 -> DD rolls its own 5..20 s. silent=false so DD behaves exactly
    ; as it does on its own event path (the player-only text is gated inside DD).
    ; ★ D13 (2026-09-13): the call is SYNCHRONOUS (zadLibs ticks Utility.Wait(1)
    ; inside it, 5-20 s) and it RETURNS the answer: -2 refused, -1 edged, 0 ran with
    ; no climax, 1+ = she came.
    Int came = libs.VibrateEffect(a, strength, 0, aiClimaxNum == 0, false)
    ; ★ 1.3.8 (the user, 2026-09-15: "Device goes off again"): DD picks its own length (5-20 s) and rolls the
    ; finish at 8 % a second, so a short vibration usually ends with nothing - came == 0. The DLL then re-arms
    ; the full-arousal lane and the device goes off again on the next pass, until she finishes (or, in public,
    ; until DD edges her once). 1+ and -1 leave the lane as the climax / the edge left it; -2 = DD refused.
    VRTE_DDZaZ_Native.NoteVibrateResult(actorId, came)

    ; ★ VOICE THROUGH THE KNEEL (user, 2026-08-27: "wire some of the DD moan with
    ; the kneel, it make sense. use DD rules for if they are gag or not.").
    ;
    ; From the fourth climax the standing clip gives way to the kneel, which runs
    ; far longer than the single OrgasmSound DD plays at the start of it - so
    ; after that one sound the whole collapse plays out in silence. These fill it.
    ;
    ; ★ libs.Moan IS DD'S OWN, so the gag rule comes for free and stays correct
    ; even if DD changes it. zadLibs.psc:1752 branches on
    ;   akActor.WornHasKeyword(zad_DeviousGag) -> gaggedSound.Play  (muffled)
    ;   else                                   -> ShortMoanSound.Play
    ; Nothing here needs to know which, and nothing here should ever test for a
    ; gag itself - DD owns that answer.
    ;
    ; ⚠ ONLY ON THE KNEEL. The standing climaxes already read as one clean beat
    ; and DD's own sound covers them; adding voice to every climax would be the
    ; spam the user has warned about from the start.
    ; ⚠ SPACED AT 4s, NOT TIGHTER. Utility.Wait is unreliable under ~100ms and
    ; DD's own clip is running underneath - three moans across twelve seconds
    ; tracks the collapse without talking over the orgasm sound that opened it.
    ; ★ 2026-09-13: the kneel and the moans moved to ClimaxPose (below), which the DLL calls
    ; at the MOMENT of DD's orgasm event. Here they ran only after VibrateEffect returned -
    ; i.e. after DD's twenty seconds on the floor - and only for our own cast lane.
EndFunction

; ★★★ THE CLIMAX POSE (1.3.5; REBUILT 1.3.6 after the independent review of 2026-09-15).
;   The user, 2026-09-14: "Used the edge belted for the 4th animation and onward, and use the edged normal clip
;   for the 3 first animation, it was good and that's the one i want. Also, as it's an animation, we can better
;   control the flow by tracking how long it last and stopping anything else to trigger after the animation stop"
;   The user, 2026-09-15: "Kneel only if arms free"; belted NPCs on 1-3: "Do the standing animation".
;   climaxes 1-3   DDZazHornyD - DD's EDGED event, as DD NG's replacer picks it for what she wears (a free-armed,
;                  unbelted NPC: DD's plain standing clip, ~10 s). Rank 1 on DDSN_ClimaxPoseFaction marks the pose so
;                  a BELTED free-armed NPC gets that same standing clip back through DD SN's DDSN_EdgedStanding
;                  submod - DD NG's own 100303 would otherwise kneel her on every edged clip.
;   from the 4th   Rank 4: DD SN's DDSN_ClimaxKneel answers with DD NG's Belt Edged kneel
;                  (15.3 s) - ONLY with arms free; a bound NPC keeps DD's own clip for her restraint. Three of
;                  DD's moans at 4 / 8 / 12 s over the kneel (as before: voice only on the kneel).
;   ★ 1.3.7 THE MARKER IS OURS: DDSN_ClimaxPoseFaction (DD SN Database.esp 0x801), rank = the DLL's
;   ClimaxClipMode for the clip's length - 4 kneel, 1 standing, 0 = no rank (DD NG's own clip for a DD device
;   it has one for). 1.3.5/1.3.6 used the vanilla actor value Variable03, which any mod or the game's AI may use.
;   Arms bound by ZaZ or DoM restraints (the DLL's RestraintOf, every framework) = rank 1 on EVERY climax: no
;   kneel while bound, and DD NG's belt kneel held off.
;   HOW LONG: exactly the clip her graph is playing. It is read half a second in and RE-READ every step
;   (ClipSecondsLeft); the pose ends a moment before the clip does, or as soon as it loops (DDZazHornyD is a
;   LOOPING clip) or disappears. If it can never be read, DD's own orgasm hold is used (19.5 s, logged).
;   ORDER AT THE END (review): the idle reset FIRST, then the marker rank, the animating faction and movement -
;   dropping the marker first let OAR swap the clip at a loop point before the reset landed.
; ★ NO JUMP. The DLL dispatches this from DD's orgasm SIGNAL and has already put her in zadAnimatingFaction
;   (aiOwned = 1), so DD's ActorOrgasm never starts its floor clip. aiOwned = 0: the DLL added nothing (DD's
;   clip got there first, or the signal missed) - this function then removes only a faction it added itself.
;   ⛔ EVERY exit removes a faction this pose owns (review: it leaked on the no-pose exits, saved with the game,
;   and DD then refused every animation on her) and calls NotePoseEnd (or the DLL keeps her lanes closed).
; ★ WEAPON: as DD's own StartThirdPersonAnimation (zadLibs.psc :1094-1108) - sheathe, wait up to 3.5 s, and no
;   pose if it is still drawn ("starting animation will break the actor weapon state").
; ★ THE VIBRATION ENDS AT THE CLIMAX (the user, 2026-09-14: "End vibration") - DD's own StopVibrating.
; ★ Relief: arousal 50 over DD's random 0-75, once the clip has played.
; GUARDS (1.3.3, kept): the player, the dead, disabled or unconscious - nothing. Unloaded, seated or mounted -
;   no pose (DD plays none there either); the vibration stop and the relief still apply.
Function ClimaxPose(Int actorId, Int n, Int aiOwned) Global
    Actor a = Game.GetFormEx(actorId) as Actor
    if !a || a == Game.GetPlayer()
        VRTE_DDZaZ_Native.NotePoseEnd(actorId)
        return
    endif
    zadlibs libs = Game.GetFormFromFile(0x00F624, "Devious Devices - Integration.esm") as zadlibs
    Faction animFac = None
    if libs
        animFac = libs.zadAnimatingFaction
    endif
    Bool ownFaction = aiOwned == 1 && animFac
    if a.IsDead() || a.IsDisabled() || a.IsUnconscious()
        if ownFaction
            a.RemoveFromFaction(animFac)
        endif
        VRTE_DDZaZ_Native.NotePoseEnd(actorId)
        return
    endif
    Bool poseAllowed = animFac && a.Is3DLoaded() && !a.IsOnMount() && a.GetSitState() == 0
    if poseAllowed && a.IsWeaponDrawn()
        a.SheatheWeapon()
        Int t = 0
        while a.IsWeaponDrawn() && t < 35
            Utility.Wait(0.1)
            t += 1
        endwhile
        if a.IsWeaponDrawn()
            poseAllowed = false
            VRTE_DDZaZ_Native.LogLine("[WORN] climax pose #" + n + " on " + a.GetDisplayName() + " skipped - the weapon would not sheathe (DD's own rule)")
        endif
    endif
    if poseAllowed
        if !a.IsInFaction(animFac)
            ownFaction = true
        endif
        a.AddToFaction(animFac)
        a.SetFactionRank(animFac, 1)
        a.SetDontMove(true)
    endif
    if libs
        libs.StopVibrating(a)
    endif
    if !poseAllowed
        if ownFaction
            a.RemoveFromFaction(animFac)
        endif
        if !a.IsDead()
            SetArousalTo(a, 50)
        endif
        VRTE_DDZaZ_Native.NotePoseEnd(actorId)
        return
    endif

    ; 4 = the kneel, 1 = standing even if belted, 0 = DD decides (arms bound) - the DLL's one answer
    Int mode = VRTE_DDZaZ_Native.ClimaxClipMode(a, n)
    Faction poseFac = Game.GetFormFromFile(0x801, "DD SN Database.esp") as Faction
    if poseFac && mode > 0
        a.AddToFaction(poseFac)
        a.SetFactionRank(poseFac, mode)
    elseif !poseFac
        VRTE_DDZaZ_Native.LogLine("[WORN] climax pose on " + a.GetDisplayName() + " - DDSN_ClimaxPoseFaction missing (DD SN Database.esp older than 1.3.7?) - DD NG picks the clip")
    endif
    Debug.SendAnimationEvent(a, "DDZazHornyD")
    Utility.Wait(0.5)
    Float left = VRTE_DDZaZ_Native.ClipSecondsLeft(a, "ZazHornyD")
    Bool measured = left > 0.0
    String kind = "standing edged clip"
    if mode == 4
        kind = "the kneel"
    elseif mode == 0
        kind = "DD's clip for her restraint"
    endif
    if measured
        Int tenths = (left * 10.0) as Int
        VRTE_DDZaZ_Native.LogLine("[WORN] climax pose #" + n + " (" + kind + ") on " + a.GetDisplayName() + " - clip playing, " + (tenths / 10) + "." + (tenths % 10) + " s left of it at the first read")
    else
        VRTE_DDZaZ_Native.LogLine("[WORN] climax pose #" + n + " (" + kind + ") on " + a.GetDisplayName() + " - the clip could not be read, holding DD's own 19.5 s")
    endif

    Float fallback = 19.5
    Float waited = 0.0
    Float prev = left
    Int moans = 0
    Bool gone = false
    Bool done = false
    while !done && !gone
        Float stepS = 1.0
        if measured
            if left < 1.2
                stepS = left - 0.1
            endif
        elseif fallback - waited < 1.0
            stepS = fallback - waited
        endif
        if stepS < 0.1
            stepS = 0.1
        endif
        Utility.Wait(stepS)
        waited += stepS
        ; she can be killed, knocked out or unloaded mid-clip - stop holding, and never moan from a corpse
        if a.IsDead() || a.IsDisabled() || a.IsUnconscious() || !a.Is3DLoaded()
            gone = true
        else
            if mode == 4 && moans < 3 && waited >= 4.0 * (moans + 1)
                libs.Moan(a)
                moans += 1
            endif
            if measured
                Float now = VRTE_DDZaZ_Native.ClipSecondsLeft(a, "ZazHornyD")
                if now <= 0.15 || now > prev + 0.5
                    done = true         ; at its end, looped back to the start, or no longer playing
                else
                    prev = now
                    left = now
                endif
            elseif waited >= fallback
                done = true
            endif
        endif
    endwhile

    if !gone
        Debug.SendAnimationEvent(a, "IdleForceDefaultState")
    endif
    if poseFac
        a.RemoveFromFaction(poseFac)
    endif
    if ownFaction
        a.RemoveFromFaction(animFac)
        if !a.IsDead()
            a.SetDontMove(false)
        endif
    endif
    if !a.IsDead()
        SetArousalTo(a, 50)             ; "relief" - the TOTAL, on OSL and SLO alike (see SetArousalTo)
    endif
    VRTE_DDZaZ_Native.NotePoseEnd(actorId)
EndFunction

; ★★ AROUSAL, THE ONE PLACE (2026-09-14, the user: "make sure the arousal we use also work with OSL
;   and with SLO, the two main arousal system"). Both ship the slaFrameworkScr compatibility surface
;   and DD resolves whichever is installed into zadLibs.Aroused - but EXPOSURE is not AROUSAL in both:
;     * OSL Aroused, OSL mode: "Exposure is just arousal" (ArousalSystemOSL.cpp) - the same number.
;     * OSL Aroused, SLA mode: exposure is what arousal is CALCULATED from.
;     * SLO Aroused NG: GetActorExposure is {Deprecated} - the legacy-exposure effect plus DD teasing
;       only; arousal is the clamped SUM of every effect (nudity, sex, satisfaction, timed, trauma...).
;   So READ with GetActorArousal, and SET a target by moving only the exposure part by the gap -
;   exposure = target - (total - exposure) - which is SeverActions_SLOArousal's shipped formula and
;   lands exactly in OSL mode. SLO's SetActorExposure already forces its own recalculation.
; ⚠ SLO scales every legacy-exposure change by its MCM legacy multiplier, so ONE correction pass
;   follows when the first lands more than 1 away. Returns the arousal reached, -1 if unavailable.
; ⛔ NEVER zadLibs.UpdateExposure for this: without OSL it writes classic SLA's StorageUtil key that
;   SLO never reads, and SLO's dummy OSLAroused.esp flips DD's own OSL branch on an SLO install.
Int Function ArousalOf(Actor a) Global
    zadlibs libs = Game.GetFormFromFile(0x00F624, "Devious Devices - Integration.esm") as zadlibs
    if !a || !libs || !libs.Aroused
        return -1
    endif
    return libs.Aroused.GetActorArousal(a)
EndFunction

Int Function SetArousalTo(Actor a, Int aiTarget) Global
    zadlibs libs = Game.GetFormFromFile(0x00F624, "Devious Devices - Integration.esm") as zadlibs
    if !a || !libs || !libs.Aroused
        return -1
    endif
    if aiTarget < 0
        aiTarget = 0
    elseif aiTarget > 100
        aiTarget = 100
    endif
    Int total = libs.Aroused.GetActorArousal(a)
    if total < 0
        return -1                        ; a child, or the framework does not know her
    endif
    Int pass = 0
    while pass < 2 && (total - aiTarget > 1 || aiTarget - total > 1)
        Int exposure = libs.Aroused.GetActorExposure(a)
        libs.Aroused.SetActorExposure(a, exposure + (aiTarget - total))
        Int after = libs.Aroused.GetActorArousal(a)
        if after == total
            pass = 2                     ; nothing moved (a clamp, a lock) - do not push again
        endif
        total = after
        pass += 1
    endwhile
    return total
EndFunction

; ★★ THE TIRED KNEEL (the user, 2026-09-14). A really restraining device - arm binder, yoke, leg
;   restraint, rope bind - that had reached effect phase 4 or 5 comes off, and the body that was
;   held in it gives way: "it does the bleedout animation for when device are removed that reached
;   effect 4". Gags and nipple clamps never do it ("painful, but wont throw someone on their knee").
;   The DLL decides (DrainPendingOff: the removal is real, the tier and phase are the after-state's
;   own) and paces it to one per NPC per 30 s; this only plays it.
;
; ★ "SHORT, LIKE THE SHOCK" (the user): the very motion and length of the (then) plug-shock knockdown - since
;   1.3.6 the plug shock ragdolls and this bleedout kneel is the PIERCING shock's reaction -
;   the vanilla BleedOutStart animation EVENT for 3 s. ⚠ An event, never real bleedout: that would
;   mean driving her health to zero and could kill a non-essential NPC.
;
; ⚠ Not on a mounted or seated actor: an animation event fired into furniture or a saddle breaks
;   the pose rather than playing a kneel. aiPhase is for the log only (4 or 5 kneel the same).
Function TiredKneel(Int actorId, Int aiPhase, Int unused) Global
    Actor a = Game.GetFormEx(actorId) as Actor
    if !a || a.IsDead() || a.IsDisabled() || a.IsUnconscious() || !a.Is3DLoaded()
        return
    endif
    if a.IsOnMount() || a.GetSitState() != 0
        Debug.Trace("[VRTE_DDZaZ] tired kneel on " + a.GetDisplayName() + " skipped - mounted or seated")
        return
    endif
    Debug.SendAnimationEvent(a, "BleedOutStart")
    Utility.Wait(3.0)
    ; Re-check: she can be knocked out, killed or unloaded inside the three seconds, and an idle
    ; reset sent into VRTouchEvents' KO ragdoll would fight it.
    if a && !a.IsDead() && !a.IsUnconscious()
        Debug.SendAnimationEvent(a, "BleedOutStop")
        Debug.SendAnimationEvent(a, "IdleForceDefaultState")
    endif
EndFunction

; The shock. DD's ShockActor is public, has its own NPC branch, and does the
; whole job: the message, the ShockEffect cast, and dropping arousal to 10-20.
;
; ⚠ THIS IS THE DELIVERY DD NEVER WROTE FOR NPCs. zadEventPeriodicShocker and
;   zadEventChaosPlug are player-only, and the NPC loop runs only Horny and
;   Vibration - so a Plug (Shock) on an NPC has never once discharged. The
;   keyword was on the device the whole time with nothing to fire it.
; aiCount = how many shock devices discharged together.
; aiSites  = where, as a mask: 1 plug (inside), 2 nipple piercings, 4 intimate
;            piercing. More than one bit is normal - a full set fires at once.
;
; ⛔ SUPERSEDED 1.3.5 - plug = RAGDOLL, piercings = BLEEDOUT (see the reaction block in the body).
; ★ THE REACTION (user, 2026-08-26). DD's own ShockActor plays NO animation at
;   all - the shock is invisible apart from a vanilla shader flash. So the body
;   reaction is entirely ours:
;     * a PLUG shocks inside her -> the vanilla BLEEDOUT animation, the same
;       down-on-one-knee-and-crawl the game uses for an essential NPC at 0 HP.
;       Sent as an animation EVENT, so it never touches her health or her
;       essential flag and cannot leave her stuck down.
;     * PIERCINGS only -> no bleedout. She stops where she is and rides it out,
;       the same weight as a vibration.
;
; ⚠ Damage scales but is capped hard: 2.5% of MAX health per device, so a full
;   set of four is 10% and never more. It is a jolt, not a weapon.
Function DoShock(Int actorId, Int aiCount, Int aiSites) Global
    Actor a = Game.GetFormEx(actorId) as Actor
    ; ★ D3 (the user, 2026-09-13): never on an unconscious actor. VRTouchEvents' KO holds
    ; her at low health with HealRate 0 for game-hours; 2.5 % per device per jolt killed
    ; her inside it, and the bleedout played on a paralysed ragdoll.
    if !a || a.IsDead() || a.IsDisabled() || a.IsUnconscious()
        return
    endif
    zadlibs libs = Game.GetFormFromFile(0x00F624, "Devious Devices - Integration.esm") as zadlibs
    if libs
        libs.ShockActor(a)
        ; ★ VOICE ON THE SHOCK (user, 2026-09-03: "yes, add it").
        ;
        ; ⛔ Shock was the ONLY effect in the mod that made no sound at all, and
        ; it is the one that puts an NPC on the floor. libs.ShockActor only
        ; prints a notification - there is no sound anywhere on DD's shock path.
        ; Vibration, climax, combat stun and the trip all speak through DD; a
        ; silent knockdown read as a bug every time it fired.
        ;
        ; ★ libs.Moan IS DD'S OWN, so the gag rule comes free and stays correct
        ; even if DD changes it: zadLibs branches on WornHasKeyword(zad_DeviousGag)
        ; to the muffled sound, else the open one. NEVER test for a gag here -
        ; DD owns that answer, and DoCombatStun already does exactly this.
        ;
        ; ⚠ ONE moan, not the kneel's three. Shock fires far more often than a
        ; fourth climax - the user's own spam warning - and it is a single jolt,
        ; not a collapse that plays out over twelve seconds.
        libs.Moan(a)
    endif

    if aiCount < 1
        aiCount = 1
    endif
    if aiCount > 4
        aiCount = 4
    endif

    ; Damage first, so the reaction reads as a consequence of it.
    Float maxHp = a.GetBaseActorValue("Health")
    if maxHp > 0.0
        a.DamageActorValue("Health", maxHp * 0.025 * aiCount)
    endif

    ; ★★ 1.3.5 THE REACTION BY SITE (the user in VR, 2026-09-14: "bleedout for piercing, ragdoll for plugs.
    ;   The more device, the longer"). Replaces the 08-26 split below (plug = bleedout, piercings = stagger).
    ; ⚠ Not on a mounted, seated or unloaded actor - a knockdown or an animation event fired into a saddle or
    ;   furniture breaks the pose instead of playing.
    if a.IsDead() || a.IsUnconscious() || !a.Is3DLoaded() || a.IsOnMount() || a.GetSitState() != 0
        return
    endif
    ; bit 1 = a PLUG, i.e. it discharged inside her -> she goes down in a RAGDOLL.
    if Math.LogicalAnd(aiSites, 1) == 1
        ; ★★ 1.3.6 THE PARALYSIS HOLD (the user, 2026-09-15: "Paralysis hold" - for "The more device, the
        ;   longer"). She is down for 2 s + 1 s per device that fired: 3 s for the plug alone, up to 6 s for a
        ;   full set of four (aiCount is capped at 4 above).
        ;   THE RECIPE IS THIS LOAD ORDER'S OWN, VR-PROVEN: VRTouchEvents' knock-out (VRTouch_MainScript.psc
        ;   :2019-2022 down, :2110-2115 wake) and the Knockout mod (aaaKnockoutUnconsciousnessEffectScript :31-32,
        ;   :163-166) - ForceActorValue("Paralysis", 1) keeps the body limp, PushActorAway(self, 0.001) is the
        ;   trigger ("to ragdoll"); the wake is ForceActorValue("Paralysis", 0) (the engine plays the get-up)
        ;   then QueueNiNodeUpdate. Both of those also SetUnconscious(true) - this does NOT (she is shocked, not
        ;   knocked out), so a Paralysis-only hold on a PLANCK-driven NPC is untested in VR.
        ; ⛔ CORRECTED 1.3.6 (review): 1.3.5 used PushActorAway(a, 0.0) and claimed PLANCK's watchdog fires
        ;   exactly that - PLANCK calls the engine's internal push from the PLAYER's position; the Papyrus
        ;   force this load order has proven is 0.001.
        ; ⚠ Paralysis is SAVED with the game; this stack resumes after a load and releases her. If VRTouchEvents
        ;   knocks her out inside the hold, its own wake owns the Paralysis - it is left alone here.
        a.ForceActorValue("Paralysis", 1.0)
        a.PushActorAway(a, 0.001)
        Utility.Wait(2.0 + aiCount)
        if !a.IsDead() && !a.IsUnconscious()
            a.ForceActorValue("Paralysis", 0.0)
            a.QueueNiNodeUpdate()
            ; The get-up resets the animation graph and DD re-chooses its bound-arm set only on its own equip
            ; events (the 09-13 KO finding) - the same re-evaluation the DLL asks for after a KO wake.
            RefreshBoundPose(actorId)
        endif
    Else
        ; Piercings only -> the vanilla BLEEDOUT kneel, longer with every device that fired: 2 s + 1 s per
        ; device - 3 s for one piercing, 4 s for nipple and clit rings together (1.3.6 review: without a plug
        ; only the two piercing sites exist, so the "6 s for four" 1.3.5 wrote cannot happen here; any other
        ; shocking device that fires with them adds its second too). The length is the one the plug bleedout
        ; used since 08-26.
        ; ⚠ ANIMATION EVENT ONLY. Forcing real bleedout would mean driving her health to zero, which risks
        ;   killing a non-essential NPC outright. The event plays the same motion with none of that.
        Debug.SendAnimationEvent(a, "BleedOutStart")
        Utility.Wait(2.0 + aiCount)
        ; she can be knocked out or killed inside the wait - an idle reset into a KO ragdoll would fight it
        if !a.IsDead() && !a.IsUnconscious()
            Debug.SendAnimationEvent(a, "BleedOutStop")
            Debug.SendAnimationEvent(a, "IdleForceDefaultState")
        endif
    endif
EndFunction

; The combat lane. A vanilla stagger plus the moan — DD's animation machinery is
; deliberately NOT involved, because StartThirdPersonAnimation sheathes her
; weapon (disarming her mid-fight) and the DD idles are long full-body poses.
; A stagger is the engine's own combat vocabulary: brief, interruptible, safe.
;
; ★ The user's call (2026-08-26): "3.5sec stunt in combat is good actualy, but
;   not with those 'feet up on back' animation of course."
;
; ⚠ The graph variable is "staggerMagnitude", LOWERCASE. Behaviour-graph names
;   are case-sensitive and the capitalised form this used to send silently did
;   nothing, leaving whatever value was already in the graph.
Function DoCombatStun(Int actorId, Int strength, Int unused) Global
    Actor a = Game.GetFormEx(actorId) as Actor
    if !a || a.IsOnMount() || a.IsSwimming() || a.IsDead()
        return
    endif
    zadlibs libs = Game.GetFormFromFile(0x00F624, "Devious Devices - Integration.esm") as zadlibs
    if libs
        libs.Moan(a)
    endif
    a.SetAnimationVariableFloat("staggerDirection", 0.0)
    a.SetAnimationVariableFloat("staggerMagnitude", 0.1 + 0.08 * strength)
    Debug.SendAnimationEvent(a, "staggerStart")
EndFunction

; ================================================================
; THE DISCREET RESPONSE (restored 2026-08-27)
;
; One of THREE mutually exclusive arms of the magicka-reactive worn-device
; response in WornDevices.cpp: combat -> DoCombatStun, discreet -> DoMoan,
; open -> DoVibrate. "Discreet" is the branch taken when a stranger has line of
; sight or the location is not private - she does not perform in front of
; strangers, she just makes a sound.
;
; ! THIS FUNCTION WENT MISSING and the DLL kept calling it. Added 2026-08-26,
; lost the same week to a slice-to-end-of-file rewrite of this script, and NOT
; noticed because a dispatch to a function that does not exist is not an error
; the game surfaces - it is one line in Papyrus.0.log, while the C++ side still
; burned its 45 s cooldown and still told VRTouchEvents the effect had fired.
; Recovered verbatim from the session transcript, not rewritten.
;
; ! WE ARE CALLING DD, NOT COPYING IT. zadLibs.Moan is a public Papyrus function
; and it is gag-aware inside DD: it plays the gagged clip instead when her mouth
; is stopped. Nothing of DD is copied, edited or redistributed.
; ================================================================
Function DoMoan(Int actorId, Int unused1, Int unused2) Global
    Actor a = Game.GetFormEx(actorId) as Actor
    if !a
        return
    endif
    zadlibs libs = Game.GetFormFromFile(0x00F624, "Devious Devices - Integration.esm") as zadlibs
    if libs
        libs.Moan(a)
    endif
EndFunction


; ================================================================
; AROUSAL, PER SITE AND ACCUMULATED (2026-09-06, the user's spec)
;
; The DLL sums the sites she is wearing - nipple 1, anal 2, clitoris 3,
; vaginal 3 - and hands the total here. All four devices = +9; the two
; piercings alone = +4.
;
; ★ WHY THIS GOES THROUGH DD'S `Aroused` PROPERTY AND NOT A DIRECT CALL TO
;   EITHER FRAMEWORK. The user's standing requirement is that OSL Aroused and
;   classic SexLab Aroused both work, interchangeably. They do not share an
;   event, but they DO share `slaFrameworkScr` - and DD has already resolved
;   whichever one is installed into zadLibs.Aroused. Piggybacking that is the
;   same trick the arousal READ uses in VRTEDD_Controller.Poll(), so there is
;   exactly one place that knows which framework is present, and this adds NO
;   new dependency to the mod.
;
; ⚠ SetActorExposure is a SET, not a modify - slaFrameworkScr exposes no
;   ModifyExposure at the top level - so read-then-write. This runs on the
;   Papyrus thread from a 3 s-paced trigger, so the read-modify-write race is
;   not worth a lock; the worst case is one lost tick of somebody else's
;   simultaneous change.
;
; ⚠ NO CLAMP HERE ON PURPOSE. The frameworks cap exposure themselves, and
;   guessing a ceiling would silently disagree with whichever one is installed.
;
; ✅ CHECKED 2026-09-14 against OSL (both modes) and SLO Aroused NG: this is a RELATIVE
;   move of the exposure part, so the total arousal moves by the same delta in all of
;   them - it never needed SetArousalTo's gap maths (that is for reaching a TARGET).
Function DoArousal(Int actorId, Int delta, Int unused) Global
    Actor a = Game.GetFormEx(actorId) as Actor
    if !a || delta <= 0
        return
    endif
    zadlibs libs = Game.GetFormFromFile(0x00F624, "Devious Devices - Integration.esm") as zadlibs
    if !libs || !libs.Aroused
        return
    endif
    Int now = libs.Aroused.GetActorExposure(a)
    if now < 0
        return
    endif
    libs.Aroused.SetActorExposure(a, now + delta)
EndFunction


; ================================================================
; THE BLIND MODULE's two hands (2026-08-29). Both called from the DLL's 3 s
; poll for actors the registry says are BLIND (cat 1).
; ================================================================

; ★ WE ARE CALLING DD, NOT COPYING IT. zadLibs.Trip is public and generic -
; it plays DD's own fall-over animation (VR-aware, OAR keyword-replaced) and a
; gag-aware SexlabMoan - but its ONLY shipped caller is player-only
; (zadEventBoots). This is the missing NPC half, the same footing as
; VibrateEffect, ShockActor and Moan.
; ⚠ Deliberately NOT a real ragdoll: PushActorAway physics on a PPB/PLANCK-
; driven actor is exactly the class of interaction the knowledgebase warns
; about; the animation cannot desync anything.
; (1.3.6 note: the PLUG SHOCK does ragdoll - the user's ruling of 2026-09-14, "ragdoll for plugs" - using
; the Paralysis recipe VRTouchEvents' knock-out already runs on these same NPCs. The trip stays an animation.)
Function DoTrip(Int actorId, Int unused1, Int unused2) Global
    Actor a = Game.GetFormEx(actorId) as Actor
    if !a || a.IsDead() || !a.Is3DLoaded()
        return
    endif
    zadlibs libs = Game.GetFormFromFile(0x00F624, "Devious Devices - Integration.esm") as zadlibs
    if libs
        libs.Trip(a)
    endif
EndFunction

; Melee only while blind: slot -1 = a ranged WEAPON, 0/1 = an aimed SPELL in
; that hand. The DLL decides WHAT to strip (it can read a spell's delivery
; type; Papyrus cannot) - this only performs the unequip it names.
; ⚠ abPreventEquip stays False: the AI may re-equip and the next poll strips
; again. A sticky block would need its own release bookkeeping on un-blinding;
; a 3 s re-strip is self-healing in both directions.
; ★ THE MIS-TARGET (2026-08-30, the user: "build it"). While a blind NPC is
; ALREADY in combat, the DLL rolls 25% per 3 s and hands us the NEAREST living
; body - whoever it is, allies included; that is the ruling ("attack ANYONE if
; they are blind. Cause they can't see"). StartCombat is the engine's own
; retarget; no frenzy, no faction edits, so it ends with the fight.
Function DoBlindMisTarget(Int actorId, Int targetId, Int unused) Global
    Actor a = Game.GetFormEx(actorId) as Actor
    Actor t = Game.GetFormEx(targetId) as Actor
    if !a || !t || a.IsDead() || t.IsDead() || !a.Is3DLoaded()
        return
    endif
    if a.IsInCombat()
        a.StartCombat(t)
    endif
EndFunction

Function DoBlindStrip(Int actorId, Int itemId, Int slot) Global
    Actor a = Game.GetFormEx(actorId) as Actor
    if !a || a.IsDead()
        return
    endif
    if slot >= 0
        Spell sp = Game.GetFormEx(itemId) as Spell
        if sp
            a.UnequipSpell(sp, slot)
        endif
    else
        Weapon w = Game.GetFormEx(itemId) as Weapon
        if w
            a.UnequipItem(w, false, true)
        endif
    endif
EndFunction


; ================================================================
; ⛔⛔ THE GATE'S PAIR PROBLEM, AND ITS FIX (2026-08-30, audit finding)
;
; VRTE_DDZaZ_Native.CanEquipDeviceOn documented that it accepts EITHER half of
; a DD pair. ⛔ THAT IS MEASURABLY FALSE: 0 of 1,221 DD INVENTORY halves carry
; any zad_Devious* class keyword, and 0 of 1,221 carry a biped slot - they hold
; only zad_InventoryDevice, VendorNoSale and the like. All 1,221 rendered halves
; carry the class. So the native, handed the half an equipper actually holds,
; resolves no class, no site, no region - and FAILS OPEN for every DD device.
; The whole four-layer gate was inert on its own documented call path.
;
; ★ DD ships the missing link: zadlibs.GetRenderedDevice(armor) (zadlibs.psc:778)
; walks its own pair. Papyrus can call it; the DLL cannot. So the resolution
; lives here, and THIS is what callers should use - not the raw native.
;
; ⚠ CALLERS: the future NPC->NPC action, and PPB's gesture layer (its change
; request must be updated to name this function, not the native).
; ================================================================
Bool Function CanEquipOn(Actor akTarget, Form akDevice) Global
    return EquipBlockedOn(akTarget, akDevice) == ""
EndFunction

; "" = it may go on; otherwise the name of what is in the way.
String Function EquipBlockedOn(Actor akTarget, Form akDevice) Global
    if !akTarget || !akDevice
        return ""
    endif
    Form d = ResolveRendered(akDevice)
    return VRTE_DDZaZ_Native.EquipBlockedBy(akTarget, d)
EndFunction

; Can this actor SEE that device on the wearer? (the observer view's per-device
; question, exposed for callers that want one device rather than the block)
Bool Function ResolvesToRendered(Form akDevice) Global
    return ResolveRendered(akDevice) != akDevice
EndFunction

; The inventory->rendered walk. Returns the input unchanged when it is already
; the rendered half, when DD is absent, or when DD cannot resolve the pair -
; every one of which degrades to the old behaviour rather than to an error.
Form Function ResolveRendered(Form akDevice) Global
    Armor a = akDevice as Armor
    if !a
        return akDevice
    endif
    ; A rendered half occupies biped slots; an inventory half occupies none.
    ; That is the same one-line test the DLL's equip sink uses to tell them
    ; apart, and it needs no keyword lookup.
    if a.GetSlotMask() != 0
        return akDevice
    endif
    zadlibs libs = Game.GetFormFromFile(0x00F624, "Devious Devices - Integration.esm") as zadlibs
    if !libs
        return akDevice
    endif
    Armor r = libs.GetRenderedDevice(a)
    if r
        return r as Form
    endif
    return akDevice
EndFunction


; * THE WAKE (2026-09-13). VRTouchEvents' knock-out ragdolls her (Paralysis + a push) and the
; get-up resets the animation graph; Devious Devices re-evaluates its bound-arm animation sets
; only on its own equip events - so she came round with her hands FREE while the Copper Wrist
; Cuffs Front were still locked on. The DLL calls this on its first poll after IsUnconscious()
; clears. EvaluateAA is DD's own public "choose the correct animation set or revert to default
; based on equipped devices"; it refuses while Paralysis is set, and the get-up takes a moment,
; hence the wait. NeedsBoundAnim is DD's own list of the classes that carry a pose (cuffs front,
; elbow tie, armbinders, yokes, pet suit) - nothing is sent for a plain collar.
Function RefreshBoundPose(Int actorId) Global
    Actor a = Game.GetFormEx(actorId) as Actor
    if !a || a.IsDead()
        return
    endif
    Utility.Wait(3.0)
    if a.IsDead() || a.IsUnconscious() || a.GetActorValue("Paralysis") > 0.0
        return
    endif
    zadlibs libs = Game.GetFormFromFile(0x00F624, "Devious Devices - Integration.esm") as zadlibs
    if libs && libs.BoundCombat && libs.NeedsBoundAnim(a)
        libs.BoundCombat.EvaluateAA(a)
    endif
EndFunction


; * 1.3.9 THE GAG CUTS HER OFF (the user, 2026-09-15: "When we give a NPC a gag ball, or anything that goes in the
; mouth, like a hood, if that current NPC is the one talking, they should get an interupt. Only if they are the
; currently talking NPC"). The DLL calls this from the equip itself, only when the device fills or covers the mouth
; and SkyrimNet's own signals say THIS NPC is the one talking. PurgeDialogue(False) stops the line playing now and
; drops what was queued behind it - the same call VRTouchEvents' choke release makes. The reaction to the gag is
; the equip line (VRTouchEvents' hand line or the AddOn's), which arrives after this, so nothing swallows it.
Function CutSpeech(Int actorId) Global
    Actor a = Game.GetFormEx(actorId) as Actor
    if !a || a.IsDead()
        return
    endif
    Int cut = SkyrimNetApi.PurgeDialogue(False)
    if cut == 1
        VRTE_DDZaZ_Native.LogLine("[SPEECH] " + a.GetDisplayName() + " was talking when the gag/hood went on - her line was cut mid-word")
    else
        VRTE_DDZaZ_Native.LogLine("[SPEECH] " + a.GetDisplayName() + " was talking when the gag/hood went on - her reply was dropped before its voice started (purge returned " + cut + ")")
    endif
EndFunction
