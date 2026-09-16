Scriptname VRTEDD_Controller extends Quest
; =============================================================================
;  VRTE DD/ZaZ AddOn — the control quest.
;
;  WHY AN ESP EXISTS AT ALL (2026-08-26). For its whole life this AddOn shipped
;  no plugin, which meant no quest, no alias, and therefore NO SCRIPT INSTANCE
;  anywhere in the mod. Everything had to be inferred from engine event sinks in
;  the DLL. Three features were built on inference and all three were wrong:
;
;   * CLIMAX was guessed from arousal falling near maximum. But zadLibs
;     ShockActor ends by setting arousal to 10-20, so our OWN shock produced
;     exactly that signature and reported a climax that never happened - and a
;     SexLab or OStim orgasm produced it too, narrated as "the devices brought
;     her off".
;   * AROUSAL was read from the sla_Arousal faction rank. In this load order
;     arousal comes from OSL Aroused, which never writes that faction. The read
;     returned -1 for every actor, so the shock and climax paths never ran.
;   * DEVICE NAMES were read off the worn record, which for a DD device is the
;     RENDERED half and has no name at all.
;
;  A quest gives us RegisterForModEvent, and every one of those three has an
;  exact push event behind it. This script is that bridge and nothing else: it
;  listens, and it hands facts to the DLL. No policy lives here.
; =============================================================================

zadlibs Property libs Auto Hidden

Event OnInit()
    Setup()
EndEvent

Function Setup()
    ; DD's orgasm event. ⚠ The name is DeviceActorOrgasmEx - DD's own doc
    ; comment at zadLibs.psc:1670 spells it "...Exp" with a trailing p, which is
    ; an event that does not exist. Anyone who copied that comment registered
    ; for nothing.
    RegisterForModEvent("DeviceActorOrgasmEx", "OnDDOrgasm")
    RegisterForModEvent("DeviceEdgedActorEx",  "OnDDEdged")

    ; The verbose DD equip bus - this one carries real Forms, which is what
    ; makes the inventory-half name reachable.
    RegisterForModEvent("DDI_DeviceEquipped", "OnDeviceEquipped")

    ; ★★ THE NARRATION RE-HOME (2026-08-29, the user's ruling): "i want all the
    ; event in the AddOn. Climax, vibration, trip, surrounding NPC
    ; persistent_events. The only narration VRTE handle is the Player doing a
    ; DD/ZaZ equip on a NPC, THAT'S IT."
    ; ★★ AND THE SPLIT OF THAT, 2026-09-10 (the user): "The database is pretty much
    ; no event or trigger, just awareness. So state, effect, climax, animation and
    ; sound, but no 'action' really." This quest keeps the EFFECTS - vibration,
    ; shock, spellfire, stumbling, climax - and nothing else. Devices going on and
    ; coming off, plugs, and the scene and choke gates only they used are DD SN
    ; AddOn's VRTEDD_Narrate now, called by the DLL; the removal after-state is a
    ; prompt block (0790), not an event. The player's masturbation onlookers are
    ; VRTouchEvents' own since 2026-09-13 (VRTouch_MainScript) - nothing of ours
    ; relays or narrates them any more.
    pcRef = Game.GetPlayer()
    RegisterForModEvent("VRTE_DDZaZ_DeviceEffect",  "OnDDZDeviceEffect")
    ; 1.2.9 (2026-09-14): the struggle (Better NPC Support's escape attempts, relayed by the DLL)
    ; and DD's own Horny event (zadLibs.SendDDFunctionEvent - a ModEvent.Create with pushed args).
    RegisterForModEvent("VRTE_DDZaZ_Struggle",      "OnDDZStruggle")
    RegisterForModEvent("DDI_FunctionCalled",       "OnDDIFunctionCalled")
    ; ⚠ UNREGISTERED EXPLICITLY, so a save made on 1.2.1 or earlier carries no stale
    ; callback to a function this script no longer has.
    UnregisterForModEvent("VRTE_DDZaZ_PlugInserted")
    UnregisterForModEvent("VRTE_DDZaZ_PlugRemoved")
    UnregisterForModEvent("VRTE_DDZaZ_DeviceMenuOn")
    UnregisterForModEvent("VRTE_DDZaZ_DeviceMenuOff")
    UnregisterForModEvent("VRTE_DDZaZ_DeviceAftermath")
    UnregisterForModEvent("VRTE_DDZaZ_Masturbation")
    UnregisterForModEvent("VRTE_ChokeState")
    UnregisterForModEvent("ostim_start")
    UnregisterForModEvent("ostim_end")
    UnregisterForModEvent("StartSexLabAnimation")
    UnregisterForModEvent("EndSexLabAnimation")
    UnregisterForModEvent("AnimationStart")
    UnregisterForModEvent("AnimationEnd")

    ; ⚠ Kick the poll. Without this the arousal feed never starts and the shock
    ;   path stays inert - the exact failure mode the faction read had.
    RegisterForSingleUpdate(3.0)
    Debug.Trace("[VRTEDD] controller registered: orgasm, edged, equip + arousal poll")
EndFunction

; ⚠ Handlers for custom ModEvents must be declared Function, not Event - a
;   non-native script cannot define new events (report 25 §7).
Function OnDDOrgasm(Form akSource, Form akActor, Int aiArousalAfter)
    Actor a = akActor as Actor
    if a
        ; ⚠ Do NOT filter on akSource. DD's OStim compatibility layer raises this
        ;   same event with itself as the source.
        ; ⚠ And do NOT read arousal here: the event fires BEFORE ActorOrgasm
        ;   writes the new value, and aiArousalAfter is -1 in the default case.
        VRTE_DDZaZ_Native.NoteClimax(a)
    endif
EndFunction

Function OnDDEdged(Form akSource, Form akActor)
    ; Denied, not finished. Recorded for later use; deliberately not narrated as
    ; a climax, because it is the opposite of one.
    Actor a = akActor as Actor
    if a
        Debug.Trace("[VRTEDD] edged: " + a.GetDisplayName())
    endif
EndFunction

; ── AROUSAL: POLLED, AND DELIBERATELY SO ─────────────────────────────────────
; ★ The user's requirement: this must work with OSL Aroused AND classic SexLab
;   Aroused, interchangeably.
;
; ⚠ They do NOT share an event. OSL raises OSLA_ActorArousalUpdated; classic SLA
;   raises nothing comparable. Subscribing to OSL's event would silently do
;   nothing on an SLA install - the same class of invisible failure that made
;   the sla_Arousal faction read return -1 forever.
;
; ★ But they DO share an API - the slaUtilScr / slaFrameworkScr compatibility
;   surface. OSL Aroused ships it over its native DLL, SLO Aroused NG ships it
;   "for backward compatibility", classic SLA ships the original. And DD has
;   already resolved whichever one is installed into its own `Aroused` property,
;   so we piggyback that rather than trying to find the quest ourselves.
;
; ⛔⛔ READ AROUSAL, NOT EXPOSURE (2026-09-14, the user: "make sure the arousal we
;   use also work with OSL and with SLO"). This used to read GetActorExposure.
;   In OSL MODE that IS arousal ("Exposure is just arousal in OSL Mode",
;   ArousalSystemOSL.cpp) - which is why it looked right here. But in OSL's SLA
;   MODE it is the raw exposure arousal is calculated FROM, and in SLO Aroused NG
;   it is {Deprecated}: only the legacy-exposure effect plus DD teasing, while
;   the real arousal is the sum of EVERY effect (nudity, sex, satisfaction,
;   timed, trauma...). GetActorArousal is the total in all three.
;   The writes go through VRTE_DDZaZ_Equip.SetArousalTo for the same reason.
;
; Polled every 3 s over ONLY the actors the DLL is tracking - usually none, and
; rarely more than two or three.
Event OnUpdate()
    Poll()
    RegisterForSingleUpdate(3.0)
EndEvent

Function Poll()
    ; ★ THE STATE HEARTBEAT FIRST, and deliberately ABOVE the arousal guard.
    ; DD signals a running vibration by faction rank only (zadLibs.psc:2347-2366)
    ; and a rank change applies no magic effect, so the DLL's effect sink could
    ; miss a whole short vibration. This bounds that edge at 3 s.
    ;
    ; ⚠ It must not sit behind `if !l || !l.Aroused`: an install with no arousal
    ; mod resolved would return there and silently take the vibration edge down
    ; with it. Vibration detection has nothing to do with arousal.
    VRTE_DDZaZ_Native.PollStates()

    zadlibs l = Libs()
    Int[] ids = VRTE_DDZaZ_Native.TrackedActors()

    ; ⛔ THE AROUSAL GUARD NOW GATES ONLY THE AROUSAL READ (2026-09-10).
    ; It used to `return` here, above both sweeps below. On a load order with no
    ; OSL or SexLab Aroused, l.Aroused is None and the rest of Poll never ran:
    ; already-worn devices never got their names, and the new key sweep would
    ; have been switched off the same silent way. This function's own header
    ; warns about exactly this trap for the vibration edge - PollStates was moved
    ; above the guard for it - but the reconcile had been left behind. Names and
    ; keys have nothing to do with arousal.
    if l && l.Aroused
        Int i = 0
        while i < ids.Length
            Actor a = Game.GetFormEx(ids[i]) as Actor
            if a && !a.IsDead() && !a.IsDisabled()
                VRTE_DDZaZ_Native.NoteArousal(a, l.Aroused.GetActorArousal(a))
            endif
            i += 1
        endwhile
    endif

    ; Names and KEYS for devices that were already on before we started
    ; watching. The equip event covers everything put on since; these cover the
    ; rest. Every 10th tick of a 3 s poll - about every 30 s.
    nameTick += 1
    if nameTick >= 10
        nameTick = 0
        if l
            ReconcileNames(l, ids)     ; needs zadlibs.GetWornDevice
        endif
        ReconcileKeys(ids)             ; DD natives only - needs no zadlibs
    endif
EndFunction

Int nameTick = 0

; ⚠ THE DEVICE NAME IS ON THE INVENTORY HALF. The worn record for a DD device is
;   the RENDERED half and has no FULL name at all, so a worn-armor walk can never
;   produce one. zadlibs.GetWornDevice resolves the named half from the class
;   keyword - this is the only route, and it is Papyrus-only, which is a large
;   part of why this quest exists.
Function ReconcileNames(zadlibs l, Int[] ids)
    ; ★ 2026-09-13: the CLASS-keyed push below is now the DLL's FALLBACK. PushDeviceKey
    ; (the equip edge and ReconcileKeys' sweep over EVERY worn device) files the same
    ; name under the exact rendered FormID, which is what every reader is handed, so
    ; the 13-class limit here no longer leaves anything nameless. Kept as belt and
    ; braces for a DD build whose GetRenderDevice answers None.
    ; ★ 2026-09-12: 10 -> 12. Boots and Gloves were missing, and BOTH have
    ; name-keyed rules that were therefore blind for any device worn before the
    ; session started: the whole boot grade (leg / full / damp is decided by the
    ; NAME, because all 108 DD boots carry identical keywords) and the 08-30
    ; restrictive-glove rule. A device equipped during the session gets its name
    ; from the live DDI_DeviceEquipped edge; everything already on her at load
    ; depends on this sweep.
    ; ★ 2026-09-12: 12 -> 13. Hood added - the Extreme Hood ruling (the (Open) form
    ; can see, the closed form is just a blindfold) is decided by the NAME,
    ; because both forms render the same eye-piece mesh and carry the same
    ; keywords. A hood worn before the session started gets no name without this.
    String[] classes = new String[13]
    classes[0] = "Gag"
    classes[1] = "Blindfold"
    classes[2] = "PlugVaginal"
    classes[3] = "PlugAnal"
    classes[4] = "Plug"
    classes[5] = "PiercingsNipple"
    classes[6] = "PiercingsVaginal"
    classes[7] = "Belt"
    classes[8] = "Collar"
    classes[9] = "Armbinder"
    classes[10] = "Boots"
    classes[11] = "Gloves"
    classes[12] = "Hood"
    Int i = 0
    while i < ids.Length
        Actor a = Game.GetFormEx(ids[i]) as Actor
        if a
            Int c = 0
            while c < classes.Length
                Keyword kw = Keyword.GetKeyword("zad_Devious" + classes[c])
                if kw && a.WornHasKeyword(kw)
                    Armor inv = l.GetWornDevice(a, kw)
                    if inv && inv.GetName() != ""
                        VRTE_DDZaZ_Native.NoteDeviceName(a, classes[c], inv.GetName())
                    endif
                endif
                c += 1
            endwhile
        endif
        i += 1
    endwhile
EndFunction

zadlibs Function Libs()
    if !libs
        libs = Game.GetFormFromFile(0x00F624, "Devious Devices - Integration.esm") as zadlibs
    endif
    return libs
EndFunction

; The device NAME lives on the INVENTORY half; the worn half has none. This is
; the only place both are in hand at once.
Function OnDeviceEquipped(Form akInventoryDevice, Form akKeyword, Form akActor)
    Actor a = akActor as Actor
    Keyword kw = akKeyword as Keyword
    if !a || !kw || !akInventoryDevice
        return
    endif
    String kwId = kw.GetString()
    ; "zad_DeviousPlugAnal" -> "PlugAnal", matching the DLL's own class suffix.
    Int at = StringUtil.Find(kwId, "Devious")
    if at == -1
        return
    endif
    String cls = StringUtil.Substring(kwId, at + 7)
    VRTE_DDZaZ_Native.NoteDeviceName(a, cls, akInventoryDevice.GetName())

    ; ★ THE KEY, pushed alongside the name. This is one of the two places the
    ; INVENTORY half is in hand - the half that carries the key. The other is
    ; the sweep, ReconcileKeys, for devices that were already on.
    PushDeviceKey(a, akInventoryDevice as Armor)
EndFunction

; ═══════════════════════════════════════════════════════════════════════════
; ⛔⛔ THE KEY CAPTURE, REWRITTEN 2026-09-10 - and why the first one never ran.
;
; The first version was:
;     zadEquipScript eq = akInventoryDevice as zadEquipScript
; It compiled, and at runtime it returned None EVERY TIME, silently.
; zadEquipScript extends ObjectReference, and DDI_DeviceEquipped hands us a BASE
; Armor form, not a reference. Casting a base form to a reference-script is legal
; syntax and yields None with no log line - so the name push just above it (a
; plain GetName() on the form) worked 207 times in one session while this block
; did nothing once. PPB measured it: 207 names learned, 0 keys, and every by-hand
; removal refused while the player was holding the correct key.
;
; ★ DD'S OWN AUTHORS NEVER CAST HERE EITHER. zadLibs.psc:766:
;     Key Function GetDeviceKey(armor device)
;         return zadNativeFunctions.GetPropertyForm(device,"deviceKey") as Key
; Their two `as zadEquipScript` casts (:1506, :3061) are both on an
; ObjectReference. The native reads the property straight off the record.
;
; ⚠ NumberOfKeysNeeded is read with a DEFAULT OF 1. PPB enumerated all 56
; properties of zadGagScript on the Black Leather Ball Strap Gag and it is not
; filled there, so an unfilled Int would otherwise read 0. 1 is DD's own script
; default (zadEquipScript.psc:35) - this reads exactly what DD itself would.
;
; ⚠ Keyed on the RENDERED FormID, because that exact record is what the removal
; gate is asked about. A key filed by class had to be found again by a second,
; independent class resolver, and the two can disagree.
; ═══════════════════════════════════════════════════════════════════════════
Function PushDeviceKey(Actor a, Armor inv)
    if !a || !inv
        return
    endif
    Armor rend = zadNativeFunctions.GetRenderDevice(inv)
    if !rend
        return
    endif
    ; ★ D10 (2026-09-13): the NAME rides the same push, filed under the rendered
    ; FormID. This runs for EVERY worn device (equip edge + the 30 s sweep), so
    ; Harness, Corset, Suit, Yoke, the cuffs, HobbleSkirt, PetSuit, StraitJacket,
    ; Bra, Mittens, ElbowTie, Clamps, PonyGear, GagPanel are named after a load
    ; too - the 13-class probe in ReconcileNames left them nameless all session.
    if inv.GetName() != ""
        VRTE_DDZaZ_Native.NoteDeviceRecord(a, rend.GetFormID(), inv.GetName())
    endif
    Int keyId = 0
    Key k = zadNativeFunctions.GetPropertyForm(inv, "deviceKey") as Key
    if k
        keyId = k.GetFormID()
    endif
    Int needed = zadNativeFunctions.GetPropertyInt(inv, "NumberOfKeysNeeded", 1)
    VRTE_DDZaZ_Native.NoteDeviceKey(a, rend.GetFormID(), keyId, needed)
EndFunction

; ★ THE SWEEP - keys for devices that were ALREADY ON (2026-09-10).
; DDI_DeviceEquipped fires once, when a device goes on. A gag worn since an
; earlier session never fires it again, so the equip capture alone leaves every
; pre-existing device with no key record. PPB's follow-up #3 read exactly that -
; "state 2" - on a ball gag Carmella had already been wearing.
; ★ GetDevices(actor, 0, true) returns every WORN INVENTORY device in one call.
; Deliberately NOT the ten-class probe ReconcileNames uses: that list has no
; Harness, cuffs, Yoke, Hood or Boots, and a locked device outside it would stay
; unknown forever. Same ~30 s cadence as the name sweep.
Function ReconcileKeys(Int[] ids)
    Int i = 0
    while i < ids.Length
        Actor a = Game.GetFormEx(ids[i]) as Actor
        if a && !a.IsDead() && !a.IsDisabled()
            Armor[] devs = zadNativeFunctions.GetDevices(a, 0, true)
            if devs
                Int d = 0
                while d < devs.Length
                    PushDeviceKey(a, devs[d])
                    d += 1
                endwhile
            endif
        endif
        i += 1
    endwhile
EndFunction


; ═══════════════════════════════════════════════════════════════════════════
;  ★★ THE NARRATION (moved here from VRTouch_MainScript.psc, 2026-08-29).
;
;  THE RULING (the user): the AddOn is the mouth for everything its own sinks
;  see - plug in/out, menu on/off, vibration / shock / climax / cast / trip,
;  and every surrounding-NPC event. VRTE narrates ONLY the player's gesture
;  equip/undress and keeps a pacing-only ear on plugs.
;
;  ★★ SPLIT AGAIN 2026-09-10: what stays HERE is the effects - vibration, shock,
;  spellfire, stumbling, climax. Menu on/off, plugs and their onlookers are DD SN
;  AddOn's VRTEDD_Narrate; the removal after-state is prompt 0790.
;
;  ★ SELF-CONTAINED since 2026-08-30 (the user: "the AddOn don't need VRTE"):
;  the pronoun helpers and the class->noun words are LOCAL below - no call in
;  this script reaches VRTouchEvents any more. VRTE keeps its own copies for
;  the gesture narration; the wording below is deliberately simpler than
;  script calls both. That makes VRTouchEvents a hard runtime dependency of
;  the narration half; the STATE half (decorators, worn block) needs no VRTE.
;  ⚠ Wording is ported VERBATIM from the VRTE originals - reviewed lines are
;  not rewritten in a move.
; ═══════════════════════════════════════════════════════════════════════════

Actor pcRef
Race  manakinRace   ; cached by DDZCanWitness - a mannequin never witnesses anything

; ── local pronoun + noun helpers (2026-08-30) ────────────────────────────────────
; ⛔ THE USER'S RULING: "the AddOn don't need VRTE, only the player equiping/
; removing gear in VR need VRTE." Until today this script called VRTouch_
; MainScript's Global pronoun helpers and VRTouch_TriggerLib's device
; dictionary - a hard runtime dependency the ReadMe denied having. These local
; copies remove it: VRTE keeps its own for the gesture narration, and the two
; sets never need to agree on anything but English.
String Function Subj(Actor a)
    if a != None && a.GetLeveledActorBase().GetSex() == 0
        return "he"
    EndIf
    return "she"
EndFunction

String Function Poss(Actor a)
    if a != None && a.GetLeveledActorBase().GetSex() == 0
        return "his"
    EndIf
    return "her"
EndFunction

String Function SubjCap(Actor a)
    if a != None && a.GetLeveledActorBase().GetSex() == 0
        return "He"
    EndIf
    return "She"
EndFunction

String Function OrdinalTxt(Int n)
    if n == 1
        return "first"
    ElseIf n == 2
        return "second"
    ElseIf n == 3
        return "third"
    ElseIf n == 4
        return "fourth"
    ElseIf n == 5
        return "fifth"
    ElseIf n == 6
        return "sixth"
    ElseIf n == 7
        return "seventh"
    EndIf
    return n + "th"
EndFunction


Actor Function PC()
    if !pcRef
        pcRef = Game.GetPlayer()
    endif
    return pcRef
EndFunction


; ★★ WHOEVER COULD SEE IT — SHORT-LIVED, NOT PERSISTENT (2026-08-29, the user):
; "for NPC seeing another one getting a gears put on, this should be a
;  shortLiveEvent in skyrimNet, compared to persistentEvent for a NPC that is
;  receiving the gears."
;
; ★ THE ASYMMETRY IS THE POINT. The WEARER keeps a persistent event - it is a
; lasting fact about their own body and they carry it. A WATCHER only saw a
; moment: it belongs in present-tense scene context and should fade, not sit in
; their ~35-entry history forever crowding out their own life.
;
; ⚠ THE KEY IS PER (OBSERVER, SUBJECT). RegisterShortLivedEvent replaces on a
; repeated id (SkyrimNetApi.psc:69, the spell_cast_actor_id pattern), so keying
; on the observer alone would make two bystanders overwrite each other, and a
; single global key would leave only the last witness knowing anything. Keyed
; this way, each watcher independently remembers the LATEST thing they saw
; happen to that person - which is also the anti-spam behaviour we want when
; several devices go on in a row.
;
; Still capped at 4 watchers: even short-lived, a crowd is noise.
; ★ MANNEQUINS ARE NEVER WITNESSES (the user, 2026-09-10: "Mannequin must always be
; excluded"). A HearthFires mannequin is a real Actor and passes every other test in
; these loops. ManakinRace catches every vanilla-race mannequin whatever a mod calls
; it; an empty name catches the rest - and SkyrimNet cannot address a nameless actor
; anyway (it logged "Could not determine name for actor 0x30009AC" in the key test).
Bool Function DDZCanWitness(Actor p)
    if p == None
        return False
    EndIf
    if !manakinRace
        manakinRace = Game.GetFormFromFile(0x0010760A, "Skyrim.esm") as Race
    EndIf
    if manakinRace && p.GetRace() == manakinRace
        return False
    EndIf
    return p.GetDisplayName() != ""
EndFunction


; A keyed, expiring after-state: a second effect on the same actor REPLACES
; the first, and it expires by itself. Physical fact only.
Function DDZAftermath(Actor a, String line, Int ttlMs)
    if a == None || line == ""
        return
    EndIf
    ; ★ D12 (2026-09-13): a SHORTER aftermath never replaces a LONGER one still running -
    ; the 60 s vibration-stop residue used to overwrite the climax's 150/180 s one on this
    ; same key within seconds. Realtime stamps: a value from an earlier launch (the KB's
    ; realtime-restarts-at-0 rule) reads as far in the future and is ignored.
    Float now      = Utility.GetCurrentRealTime()
    Float until    = StorageUtil.GetFloatValue(a, "vrtedd_afterUntil", 0.0)
    Float newUntil = now + (ttlMs as Float) / 1000.0
    if until > newUntil && (until - now) < 600.0
        Debug.Trace("[VRTEDD] aftermath on " + a.GetDisplayName() + " kept - a longer one is still running")
        return
    EndIf
    StorageUtil.SetFloatValue(a, "vrtedd_afterUntil", newUntil)
    SkyrimNetApi.RegisterShortLivedEvent("ddz_after_" + a.GetFormID(), \
        "ddz_aftermath", line, "", ttlMs, a, None)
EndFunction

; ── the struggle (1.2.9, 2026-09-14) ─────────────────────────────────────────
; Better NPC Support for Devious Devices lets an NPC try to escape her devices on
; its own timer (its MCM "Allow NPCs to struggle"). The DLL watches its
; DDNF_Struggling faction and sends "start|<names>" when it goes on, then
; "end|<forced>|<unlocked>|<takenOff>|<remaining>|<kneelPhase>" once it is off and
; every removal has settled ("-" = none). ★ 1.3.3 (the user, 2026-09-14: "Say she
; unlocked it"): BNSDD raises the same faction for a KEY unlock, so the DLL sorts
; each freed device by its route - unlocked with a key, taken off (DD wants no key),
; or forced - and only the forced ones are "worked off by her own effort". The old
; 4-field END is still read, as forced. The user: "a short event for the struggle and direct
; narration for the result". The start is AWARENESS - a short-lived event she and
; anyone near carry for two minutes; the result is SPOKEN. The removals that
; happened inside it never got their own line (the DLL folds them in here), and
; the tired kneel, if one was earned, is the DLL's, right after this.
Function OnDDZStruggle(String eventName, String strArg, Float numArg, Form sender)
    Actor a = sender as Actor
    if a == None || a.IsDead()
        return
    EndIf
    String[] f = StringUtil.Split(strArg, "|")   ; safe: the DLL never sends an empty field ("-")
    if f.Length < 2
        return
    EndIf
    String who = a.GetDisplayName()
    String vp  = Subj(a)
    String vpp = Poss(a)
    if f[0] == "start"
        String names = f[1]
        if names == "-"
            names = vpp + " devices"
        EndIf
        SkyrimNetApi.RegisterShortLivedEvent("ddz_struggle_" + a.GetFormID(), "ddz_struggle", \
            who + " is straining against " + names + ", twisting and pulling at them to get free.", \
            "", 120000, a, None)
        Debug.Trace("[VRTEDD] struggle start on " + who + " against " + names)
    ElseIf f[0] == "end" && f.Length >= 4
        String forced    = f[1]
        String unlocked  = "-"
        String takenOff  = "-"
        String remaining = f[2]
        String remainCap = "-"
        if f.Length >= 6                              ; 1.3.3 layout
            unlocked  = f[2]
            takenOff  = f[3]
            remaining = f[4]
        EndIf
        if f.Length >= 7                              ; 1.3.4: the still-on list, capitalised by the DLL
            remainCap = f[6]
        EndIf
        ; One sentence, clauses joined with ", and has" - never a sentence that starts with a
        ; pronoun (the VM interns "She"/"she" as one string - the 1.2.6 lowercase fix).
        String done = ""
        if unlocked != "-"
            done = "unlocked and removed " + unlocked
        EndIf
        if takenOff != "-"
            if done != ""
                done = done + ", and has "
            EndIf
            done = done + "taken off " + takenOff
        EndIf
        if forced != "-"
            if done != ""
                done = done + ", and has "
            EndIf
            done = done + "worked " + forced + " off by " + vpp + " own effort"
        EndIf
        String line = ""
        if done != ""
            ; ★ 1.3.4 (the user, 2026-09-14): two sentences, not one run-on list - "Carmella has taken off
            ; the Pony Tail Plug and the Grand Soulgem Vaginal Plug. The Black Leather Pony Boots (Tight), the
            ; Black Leather Ball Strap Gag and the Copper Wrist Cuffs Front stayed on." The lists and the
            ; capital "The" come from the DLL (a Papyrus "The " literal can be interned as "the ").
            line = who + " has " + done + "."
            if remainCap != "-"
                line = line + " " + remainCap + " stayed on."
            ElseIf remaining != "-"
                line = line + " Still on: " + remaining + "."
            EndIf
        Else
            if remaining == "-"
                remaining = vpp + " devices"
            EndIf
            line = who + " strained against " + remaining + " until " + vp + " stopped, and nothing came loose."
        EndIf
        ; ★ 1.3.3: the start's two-minute "is straining against ..." event would outlive the struggle.
        ; SkyrimNetApi has no removal call, but re-registering the SAME event id replaces the old one
        ; (the aftermath key above relies on exactly that), so it is overwritten with the result for 1 s.
        SkyrimNetApi.RegisterShortLivedEvent("ddz_struggle_" + a.GetFormID(), "ddz_struggle", line, "", 1000, a, None)
        SkyrimNetApi.DirectNarration(line, a, None)
        Debug.Trace("[VRTEDD] struggle end on " + who + ": " + line)
    EndIf
EndFunction

; ── DD's Horny event (1.2.9, 2026-09-14) ─────────────────────────────────────
; DD's own NPC loop (zadNPCSlots, every 1.5 game hours, belt or harness, arousal
; >= Desire) plays a 5-10 s "hands wander" clip through
; zadLibs.PlayThirdPersonAnimation, which sends DDI_FunctionCalled
; ("PlayThirdPersonAnimation", actor) - the one hook DD gives for it. The user:
; "a persistent event that let the NPC know about the arousal effect of that
; device and it's hard to ignore." Persistent, not spoken: context, no reaction.
; ⚠ zadBQ00 sends the same call for the PLAYER's sleep-stop event - excluded.
; ⚠ A ModEvent.Create event: the handler takes the pushed args in order.
Function OnDDIFunctionCalled(String fn, Form who)
    if fn != "PlayThirdPersonAnimation"
        return
    EndIf
    Actor a = who as Actor
    if a == None || a == pcRef || a.IsDead()
        return
    EndIf
    zadlibs l = Libs()
    if !l
        return
    EndIf
    String noun = ""
    if a.WornHasKeyword(l.zad_DeviousBelt)
        noun = "chastity belt"
    ElseIf a.WornHasKeyword(l.zad_DeviousHarness)
        noun = "harness"
    Else
        return                          ; not DD's Horny event - some other mod's call
    EndIf
    ; once per NPC per two minutes: DD's own cadence is 1.5 game hours, this only
    ; guards a burst. Realtime stamp with the D15 rule (a negative age is expired).
    Float now  = Utility.GetCurrentRealTime()
    Float last = StorageUtil.GetFloatValue(a, "vrtedd_hornyAt", 0.0)
    Float age  = now - last
    if last > 0.0 && age >= 0.0 && age < 120.0
        return
    EndIf
    StorageUtil.SetFloatValue(a, "vrtedd_hornyAt", now)
    String who2 = a.GetDisplayName()
    SkyrimNetApi.RegisterPersistentEvent(who2 + "'s hands have strayed to the " + noun \
        + " locked over " + Poss(a) + " hips. The arousal it keeps building under it is hard to ignore, and there is no reaching past it.", a, None)
    Debug.Trace("[VRTEDD] horny event on " + who2 + " (" + noun + ") - persistent event")
EndFunction

; ── devices going on and coming off, plugs, and their onlookers ──────────────
; ⛔ MOVED 2026-09-10 to DD SN AddOn - VRTEDD_Narrate.psc, called by the DLL while
; DD SN AddOn.esp is loaded. The Database narrates effects only (the user: "no
; 'action' really"). The removal after-state is the 0790 prompt block, read from
; the DLL through vrtedd_aftermath - no event.


; ── the effect events: vibration / shock / climax / cast / trip ─────────────
; Wording ported VERBATIM from the VRTE originals. Mechanism and sensation
; only - never how she takes it.
Function OnDDZDeviceEffect(String eventName, String strArg, Float numArg, Form sender)
    Actor a = sender as Actor
    if a == None
        return
    EndIf
    String[] f = StringUtil.Split(strArg, "|")
    if f.Length < 3
        return
    EndIf
    String what    = f[0]
    String phase   = f[1]
    Bool   visible = (f[2] as Int) == 1
    String who     = a.GetDisplayName()
    String selfNarr = ""
    String seenNarr = ""
    String vp  = Subj(a)
    String vpp = Poss(a)
    if what == "vibration"
        if phase == "start"
            selfNarr = "The device locked against " + who + " has started running, a steady vibration " + vp + " cannot move away from or reach."
            seenNarr = who + "'s body has begun to twitch, and a faint buzzing is audible from somewhere under " + vpp + " gear."
        Else
            selfNarr = "The device locked against " + who + " has gone still again, leaving the skin around it buzzing faintly."
            seenNarr = "The faint buzzing around " + who + " has stopped."
            DDZAftermath(a, who + " has just come off a long stretch of vibration. The skin where the device sits is still buzzing and oversensitive, and " + vpp + " breathing has not gone back to normal.", 60000)
        EndIf
    ElseIf what == "shock"
        Int nDev  = 1
        Int sites = 1
        if f.Length >= 5
            nDev  = f[3] as Int
            sites = f[4] as Int
        EndIf
        ; ★ 2026-09-14 (the user): "piercing are external, plug or internal, having all 4 will be
        ;   more painful than just having piercing" - the wording is graded by WHERE the current
        ;   went (a plug discharges deep inside; a ring at the skin) and by HOW MANY went at once.
        ;   Mechanism and sensation only, as ever - the LLM decides how she takes it.
        Bool inside   = Math.LogicalAnd(sites, 1) == 1
        Bool nipples  = Math.LogicalAnd(sites, 2) == 2
        Bool intimate = Math.LogicalAnd(sites, 4) == 4
        String rings = ""
        if nipples && intimate
            rings = "the rings through " + vpp + " nipples and the one set in " + vpp + " intimate flesh"
        ElseIf nipples
            rings = "the rings through " + vpp + " nipples"
        ElseIf intimate
            rings = "the ring set in " + vpp + " intimate flesh"
        EndIf
        if inside && rings != "" && nDev >= 4
            selfNarr = "All four devices - the plug inside " + who + " and " + rings + " - discharged at the same instant: current from deep inside and through every piercing at once, far harder than any one of them alone. The whole of " + vpp + " pelvis and chest seized with it, and " + vpp + " legs went out from under " + vpp + "."
        ElseIf inside && rings != ""
            selfNarr = "The plug inside " + who + " and " + rings + " discharged together - current from inside and through the piercings at once, harder than either on its own, the muscle between them seizing and " + vpp + " legs going out from under " + vpp + "."
        ElseIf inside
            selfNarr = "The plug locked inside " + who + " discharged - a hard jolt of current deep in the flesh that took the legs out from under " + who + " and left " + who + " limp on the floor for a few seconds."
        ElseIf rings != ""
            selfNarr = "A jolt of current went through " + rings + " - sharp, at the skin, over as fast as it came, leaving the flesh around the piercings twitching."
        Else
            selfNarr = "The device locked against " + who + "'s skin discharged - a sharp jolt of current, over as fast as it came, leaving the muscle under it twitching."
        EndIf
        if inside
            ; 1.3.6: the plug shock RAGDOLLS her now (Paralysis hold, 2 s + 1 s per device - the user, 09-14/15)
            seenNarr = who + " collapsed to the floor with no visible cause, lay limp there for a few seconds, then got back up."
            if nDev >= 4
                DDZAftermath(a, who + " was put on the floor a moment ago by a discharge through every device at once, and has only just got back up. The muscle through " + vpp + " pelvis and chest is still twitching and slow to answer, and " + vpp + " legs are not steady yet.", 180000)
            Else
                DDZAftermath(a, who + " was put on the floor by a discharge a moment ago and has only just got back up. The muscle it passed through is still twitching, and " + vpp + " legs are not steady yet.", 120000)
            EndIf
        Else
            ; 1.3.6: a piercing (or other skin) shock kneels her in bleedout now (2 s + 1 s per device)
            seenNarr = who + " jolted suddenly and sank to " + vpp + " knees with no visible cause, stayed down a few seconds, then got back up."
            DDZAftermath(a, who + " took a discharge through " + vpp + " piercings a moment ago and was down on " + vpp + " knees for a few seconds. The skin around them is still twitching and tender to any touch.", 90000)
        EndIf
    ElseIf what == "climax"
        if f.Length < 6
            return
        EndIf
        Int    n  = f[1] as Int
        String pl = f[5]
        ; ★ 2026-09-13: no capitalised pronoun as its own string - Papyrus's string cache is
        ; case-insensitive and interned, so "She" came back as "she" mid-sentence in VR.
        ; ★ 1.3.6: field 3 is the DLL's "the kneel will play" (4th or later, arms free - the user 09-15: "Kneel
        ; only if arms free" - OAR and DD NG's clip present, not seated/mounted). A bound NPC keeps DD's own
        ; clip for her restraint and is told she rode it out standing.
        if n >= 4 && f[3] == "1"
            selfNarr = "That is the " + OrdinalTxt(n) + " time the devices have brought " + who \
                + " off, and " + vp + " could not stay upright through it: " + vp + " went down onto " + vpp \
                + " knees and stayed there, shaking, for about fifteen seconds before " + vp \
                + " had " + vpp + " legs back."
            seenNarr = who + " suddenly dropped to " + vpp + " knees " + pl + ", shook for a quarter of a minute, and got back up."
            DDZAftermath(a, who + " was taken off " + vpp + " feet by the devices a few minutes ago and spent about fifteen seconds down on " + vpp + " knees, and " + vp + " is upright again, but " + vpp + " legs are still loose under " + vpp + " and " + vpp + " breathing has not evened out.", 180000)
        Else
            selfNarr = "The devices brought " + who + " off where " + vp + " stood, and " + vp \
                + " rode it out on " + vpp + " feet."
            seenNarr = who + " stopped where " + vp + " stood, shuddered through something, and carried on."
            DDZAftermath(a, "The devices brought " + who + " off a few minutes ago, and " + vp + " stayed on " + vpp + " feet through it, and " + vpp + " breathing has not settled back down since.", 150000)
        EndIf
    ElseIf what == "trip"
        String cause = "while walking"
        if phase == "running"
            cause = "at a run"
        ElseIf phase == "stairs"
            cause = "on ground that changed under " + vpp + " feet"
        EndIf
        selfNarr = "Unable to see the ground, " + who + " caught " + vpp + " footing " + cause + " and went down hard."
        seenNarr = who + " stumbled over nothing and fell."
        DDZAftermath(a, who + " took a blind fall a moment ago and is back on " + vpp + " feet, and " + vp + " is feeling for the ground before each step now.", 60000)
    ElseIf what == "cast"
        if f.Length < 6
            return
        EndIf
        String castMode = f[1]
        Int    nDev2    = f[4] as Int
        String place    = f[5]
        String many = "The device"
        if nDev2 > 1
            many = "All " + nDev2 + " of the devices"
        EndIf
        if castMode == "open"
            selfNarr = many + " locked into " + who + " answered the magic " + vp + " was drawing on, waking together and running hard enough that " + vp + " lost hold of what " + vp + " was doing."
            seenNarr = who + " broke off mid-cast, and for a few seconds could not get back under control."
        ElseIf castMode == "combat"
            ; ★ D14 (2026-09-13): the combat lane has been a SOUND only since 09-06 (DoMoan;
            ; the stagger was removed as a free opening for the player). No broken stance.
            selfNarr = many + " locked into " + who + " answered the magic " + vp + " was drawing on, a hard pulse mid-fight that " + vp + " fought on through without losing " + vpp + " footing."
            seenNarr = who + " let out a sound mid-swing with no visible cause, and fought on."
        Else
            selfNarr = many + " locked into " + who + " answered the magic " + vp + " was drawing on, and " + vp + " is " + place + ", and it passed through " + vpp + " without breaking " + vpp + " stride."
            seenNarr = who + " faltered for a moment, breath catching, then carried on."
        EndIf
    Else
        return
    EndIf

    ; ★ 1.3.9 (the user, 2026-09-15: "when a NPC climax, trip or get shock, they should be told something about
    ;   it" -> "React out loud, cut her off if talking"): these three happen TO her body, so she answers them
    ;   aloud. If SHE is the one talking, her own line is cut first (PurgeDialogue, the same call VRTouchEvents'
    ;   choke release makes) - a woman mid-sentence does not finish it through a climax. Someone ELSE talking is
    ;   left alone. Vibration and cast lines stay context; bystanders stay silent context (the witness loop below).
    if what == "climax" || what == "trip" || what == "shock"
        if VRTE_DDZaZ_Native.IsTalking(a)
            Int cut = SkyrimNetApi.PurgeDialogue(False)
            VRTE_DDZaZ_Native.LogLine("[SPEECH] " + who + " was talking when the " + what + " hit - her line was cut (purge " + cut + ")")
        EndIf
        SkyrimNetApi.DirectNarration(selfNarr, a, PC())
    Else
        SkyrimNetApi.RegisterPersistentEvent(selfNarr, a, None)
    EndIf
    if visible && seenNarr != ""
        Int found = 0
        Int tries = 0
        Actor lastSeen = None
        while tries < 12 && found < 4
            Actor probe = Game.FindRandomActorFromRef(a, 1050.0)
            ; ★ D8 (2026-09-13): never the PLAYER - FindRandomActorFromRef can return
            ; the player, which burned one of the four slots on a ddz_saw_<PLAYER> event.
            if probe != None && probe != a && probe != PC() && probe != lastSeen \
            && !probe.IsDead() && !probe.IsDisabled() && !probe.IsChild() && DDZCanWitness(probe)
                if probe.HasLOS(a)
                    ; 600 s, the same TTL as an equip (D17: the old comment said 120 s).
                    ; The key is per (observer, subject) and REPLACES, so a later sight
                    ; of the same person overwrites this one anyway.
                    SkyrimNetApi.RegisterShortLivedEvent("ddz_saw_" + probe.GetFormID() \
                        + "_" + a.GetFormID(), "ddz_witnessed", seenNarr, "", 600000, probe, a)
                    found += 1
                    lastSeen = probe
                EndIf
            EndIf
            tries += 1
        EndWhile
    EndIf
EndFunction

; ⛔ BegForReleaseNow MOVED 2026-09-03 to VRTEDD_Action.psc, on the ACTION
; quest in "DD SN AddOn.esp". The split puts device knowledge in the database
; and the SkyrimNet actions in the addon, so somebody building on the database
; brings their own action layer. ⚠ config/actions/begforrelease.yaml names the
; quest EditorID and script - both changed with it, and a YAML still naming
; VRTEDD_Controller resolves to nothing and the action silently never appears.

