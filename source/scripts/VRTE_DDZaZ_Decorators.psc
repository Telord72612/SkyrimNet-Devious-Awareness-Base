Scriptname VRTE_DDZaZ_Decorators Hidden
; =============================================================================
;  VRTE DD/ZaZ AddOn — SkyrimNet decorators
;
;  WHY THIS EXISTS (2026-08-23): nothing in the load order ever told an NPC she
;  was gagged. Verified: zero decorators anywhere feed DD state to SkyrimNet,
;  and no prompt component reads a zad_* keyword. A gagged NPC answered in
;  fluent sentences unless the LLM happened to improvise otherwise.
;
;  SkyrimNet's built-in `get_worn_equipment` cannot do this job: it is only
;  rendered inside `dynamic_character_bio.prompt` (the bio pass), and it reports
;  item NAMES — which for DD are empty (report 23 §6.2: all 770 resolvable
;  `deviceRendered` targets are unnamed).
;
;  ⛔ CORRECTED 2026-08-30 (report 30 §1). The "never in the live dialogue
;  prompt" half of that was FALSE in the installed build: character_bio's
;  0410_equipment.prompt has a `dialogue_target` branch, reached from
;  dialogue_response.prompt through system_head/0100_actor_bios's
;  render_character_profile("dialogue_target", responseTarget.UUID). So every
;  worn device - WITH ITS KEYWORDS, and with clothing over it entirely ignored -
;  is already being rendered into the prompt of whoever the wearer talks to.
;  That is a LEAK to close, not a substitute for this work; the filtered channel
;  is `vrtedd_worn_visible` below.
;
;  ── THE SHAPE, after the 2026-08-24 rework ────────────────────────────────
;  FIVE CATEGORIES, each answering one question about the BODY rather than
;  naming a device family, because that is the distinction the prompts act on:
;      0 gag    speech blocked        1 blind  sight blocked
;      2 arms   arms/hands unusable   3 legs   walking cut to a shuffle
;      4 all    cannot act at all     5 deaf   hearing dulled  (2026-08-27)
;
;  ★ WHY DEAF IS THE ODD ONE (the user: "the hearing one is a weird one, as the
;    LLM will still hear the player, but will have to act like they didn't
;    heard"). Every other category removes an ABILITY, which the model can
;    simply not use. This one removes an INPUT the model is still given -
;    SkyrimNet feeds the NPC the player's dialogue no matter what a decorator
;    says. So 0916 cannot say "you did not hear that"; it says the sound
;    arrives MUFFLED and tells the model to answer the tone, the volume and the
;    gesture rather than the words. That is also what the gear really does: a
;    hood dulls, it does not deafen.
;
;  Each category has TWO decorators:
;      vrtedd_<cat>            "" or the device name  -> the constraint block
;      vrtedd_un<cat>          "" or "3"/"2"/"1"      -> the RELEASE block
;
;  ⚠ WHO DECIDES "is it on": the DLL, not this script. VRTE_DDZaZ_Native
;    .IsRestrained folds DD/ZaZ keywords, the user's VRTE_DDZaZ_Restraints.ini
;    (gear by name / FormID / keyword) and SPELLS into one answer. Papyrus
;    cannot parse an INI per render and the prompt's own native
;    `worn_has_keyword` can only ask about keywords — it cannot match a name or
;    see a spell at all. One source of truth, in the one place that can hold it.
;
;  ⚠ WHO DECIDES the NAME: the DLL first, then zadlibs. The DLL can name gear it
;    matched from the INI, but a DD device's worn half has no FULL record, so
;    for DD the name only exists on the INVENTORY half and only
;    zadlibs.GetWornDevice can reach it. Hence the two-step below.
;
;  ⚠ Registration is per-SESSION. Nothing calls Register() from a quest script;
;    VRTE_DDZaZ.dll dispatches it at kNewGame and kPostLoadGame (main.cpp) - NOT at
;    kDataLoaded, where the VM is not ready. Decorators do not survive a session.
;    (The Database has shipped an ESP since 2026-09-03 - the Controller's quest -
;    but this script deliberately stays off it.)
; =============================================================================

; -----------------------------------------------------------------------------
; Register — called from the DLL. Safe to call repeatedly (SkyrimNet overwrites
; an existing registration with the same ID).
; -----------------------------------------------------------------------------
Function Register() Global
    ; No DD-presence gate any more: the INI can carry gear from any mod, and a
    ; spell needs no DD at all. With neither DD nor an INI every call simply
    ; returns "" and every block stays silent, which is the correct no-op.
    Int r0 = SkyrimNetApi.RegisterDecorator("vrtedd_gag_name",      "VRTE_DDZaZ_Decorators", "GetGagName")
    Int r1 = SkyrimNetApi.RegisterDecorator("vrtedd_blind",         "VRTE_DDZaZ_Decorators", "GetBlind")
    Int r2 = SkyrimNetApi.RegisterDecorator("vrtedd_arms_bound",    "VRTE_DDZaZ_Decorators", "GetArmsBound")
    Int r3 = SkyrimNetApi.RegisterDecorator("vrtedd_legs_bound",    "VRTE_DDZaZ_Decorators", "GetLegsBound")
    Int r4 = SkyrimNetApi.RegisterDecorator("vrtedd_all_bound",     "VRTE_DDZaZ_Decorators", "GetAllBound")
    Int r5 = SkyrimNetApi.RegisterDecorator("vrtedd_deaf",          "VRTE_DDZaZ_Decorators", "GetDeaf")
    Debug.Trace("[VRTE_DDZaZ] state decorators: gag=" + (r0 == 0) as String + " blind=" + (r1 == 0) as String \
        + " arms=" + (r2 == 0) as String + " legs=" + (r3 == 0) as String + " all=" + (r4 == 0) as String \
        + " deaf=" + (r5 == 0) as String)

    ; The RELEASE side. Every category gets one, because withdrawing a
    ; constraint does not undo the turns the LLM already wrote under it.
    Int u0 = SkyrimNetApi.RegisterDecorator("vrtedd_ungagged",      "VRTE_DDZaZ_Decorators", "GetUngagged")
    Int u1 = SkyrimNetApi.RegisterDecorator("vrtedd_unblinded",     "VRTE_DDZaZ_Decorators", "GetUnblinded")
    Int u2 = SkyrimNetApi.RegisterDecorator("vrtedd_unbound_arms",  "VRTE_DDZaZ_Decorators", "GetUnboundArms")
    Int u3 = SkyrimNetApi.RegisterDecorator("vrtedd_unbound_legs",  "VRTE_DDZaZ_Decorators", "GetUnboundLegs")
    Int u4 = SkyrimNetApi.RegisterDecorator("vrtedd_unbound_all",   "VRTE_DDZaZ_Decorators", "GetUnboundAll")
    Int u5 = SkyrimNetApi.RegisterDecorator("vrtedd_undeafened",    "VRTE_DDZaZ_Decorators", "GetUndeafened")
    Debug.Trace("[VRTE_DDZaZ] release decorators: gag=" + (u0 == 0) as String + " blind=" + (u1 == 0) as String \
        + " arms=" + (u2 == 0) as String + " legs=" + (u3 == 0) as String + " all=" + (u4 == 0) as String \
        + " deaf=" + (u5 == 0) as String)

    ; ★ 2026-09-10: WHAT A RESTRAINT LEFT BEHIND - the after-state as a prompt block for
    ; three prompts (0790), and its outward signs for an onlooker (0950).
    Int a0 = SkyrimNetApi.RegisterDecorator("vrtedd_aftermath",         "VRTE_DDZaZ_Decorators", "GetAftermath")
    Int a1 = SkyrimNetApi.RegisterDecorator("vrtedd_aftermath_visible", "VRTE_DDZaZ_Decorators", "GetAftermathVisible")
    Debug.Trace("[VRTE_DDZaZ] aftermath decorators: own=" + (a0 == 0) as String + " visible=" + (a1 == 0) as String)

    ; The worn-device block: what is on her, for how long, and how it feels by
    ; now. One decorator for the whole list — see WornDeviceReport's note on why
    ; this is not a family of per-device decorators.
    Int w0 = SkyrimNetApi.RegisterDecorator("vrtedd_worn_devices", "VRTE_DDZaZ_Decorators", "GetWornDevices")
    ; ★ what ANOTHER actor can see on her - the same block, visibility-filtered.
    Int w1 = SkyrimNetApi.RegisterDecorator("vrtedd_worn_visible", "VRTE_DDZaZ_Decorators", "GetWornDevicesVisible")
    Debug.Trace("[VRTE_DDZaZ] worn-device decorators: own=" + (w0 == 0) as String         + " visible=" + (w1 == 0) as String)
EndFunction

; -----------------------------------------------------------------------------
;  ★★ SINCE 1.3.0 (2026-09-14) THESE ARE THE FALLBACK, NOT THE PRIMARY PATH.
;  Every prompt reads Data/SKSE/Plugins/VRTE_DDZaZ/prompt_state.json first
;  (written by the DLL the instant state changes - see PromptState.h) and calls a
;  decorator only when the file has no entry for her. The two clocks below (the
;  after-state's three prompts, the release's three renders) run only on that
;  fallback path; the file grades the after-state by elapsed time and counts a
;  release in 20 s steps. Keep every decorator registered: third-party prompts,
;  cold starts and a profile without the DLL still depend on them.
; -----------------------------------------------------------------------------
;  DD name resolution — the second half of the two-step.
; -----------------------------------------------------------------------------
zadlibs Function DDLibs() Global
    return Game.GetFormFromFile(0x00F624, "Devious Devices - Integration.esm") as zadlibs
EndFunction

; "" when she is not wearing a device of this class, otherwise its display name.
; ⚠ THE NAME IS NOT ON THE WORN ITEM. DD ships each device as a pair — a
;   RENDERED armor (what she actually wears; FULL name empty) and an INVENTORY
;   armor (which carries the name). GetWornDevice resolves the named half.
String Function WornDeviceName(Actor akActor, zadlibs libs, String kwId) Global
    Keyword kw = Keyword.GetKeyword(kwId)
    if !kw || !akActor.WornHasKeyword(kw)
        return ""
    endif
    Armor inv = libs.GetWornDevice(akActor, kw)
    if inv
        return inv.GetName()
    endif
    return ""
EndFunction

; -----------------------------------------------------------------------------
;  The state side. Native decides IF; this decides WHAT IT IS CALLED.
;  Returns "" only when she is genuinely unrestrained in that category — a
;  nameless device still returns the fallback noun, because "" is the signal the
;  prompt gates on and a nameless match must never read as "not restrained".
; -----------------------------------------------------------------------------
String Function NamedRestraint(Actor akActor, Int cat, String fallback) Global
    if !akActor || !VRTE_DDZaZ_Native.IsRestrained(akActor, cat)
        return ""
    endif
    String nm = VRTE_DDZaZ_Native.RestraintName(akActor, cat)
    if nm != ""
        return nm
    endif
    return fallback
EndFunction

String Function GetGagName(Actor akActor) Global
    if !akActor || !VRTE_DDZaZ_Native.IsRestrained(akActor, 0)
        return ""
    endif
    String nm = VRTE_DDZaZ_Native.RestraintName(akActor, 0)
    if nm != ""
        return nm
    endif
    zadlibs libs = DDLibs()
    if libs
        nm = WornDeviceName(akActor, libs, "zad_DeviousGag")
        if nm != ""
            return nm
        endif
        nm = WornDeviceName(akActor, libs, "zad_DeviousGagPanel")
        if nm != ""
            return nm
        endif
    endif
    return "a gag"
EndFunction

String Function GetBlind(Actor akActor) Global
    if !akActor || !VRTE_DDZaZ_Native.IsRestrained(akActor, 1)
        return ""
    endif
    String nm = VRTE_DDZaZ_Native.RestraintName(akActor, 1)
    if nm != ""
        return nm
    endif
    zadlibs libs = DDLibs()
    if libs
        nm = WornDeviceName(akActor, libs, "zad_DeviousBlindfold")
        if nm != ""
            return nm
        endif
        nm = WornDeviceName(akActor, libs, "zad_DeviousHood")
        if nm != ""
            return nm
        endif
    endif
    return "a blindfold"
EndFunction

String Function GetArmsBound(Actor akActor) Global
    if !akActor || !VRTE_DDZaZ_Native.IsRestrained(akActor, 2)
        return ""
    endif
    String nm = VRTE_DDZaZ_Native.RestraintName(akActor, 2)
    if nm != ""
        return nm
    endif
    ; Specific classes first; the generic zad_DeviousHeavyBondage marker rides
    ; along on armbinders AND yokes, so testing it first would name every one of
    ; them the same and throw away the device's real name.
    zadlibs libs = DDLibs()
    if libs
        nm = WornDeviceName(akActor, libs, "zad_DeviousArmbinder")
        if nm != ""
            return nm
        endif
        nm = WornDeviceName(akActor, libs, "zad_DeviousYoke")
        if nm != ""
            return nm
        endif
        nm = WornDeviceName(akActor, libs, "zad_DeviousBondageMittens")
        if nm != ""
            return nm
        endif
        nm = WornDeviceName(akActor, libs, "zad_DeviousCuffsFront")
        if nm != ""
            return nm
        endif
        nm = WornDeviceName(akActor, libs, "zad_DeviousHeavyBondage")
        if nm != ""
            return nm
        endif
    endif
    return "arm restraints"
EndFunction

String Function GetLegsBound(Actor akActor) Global
    if !akActor || !VRTE_DDZaZ_Native.IsRestrained(akActor, 3)
        return ""
    endif
    String nm = VRTE_DDZaZ_Native.RestraintName(akActor, 3)
    if nm != ""
        return nm
    endif
    zadlibs libs = DDLibs()
    if libs
        nm = WornDeviceName(akActor, libs, "zad_DeviousHobbleSkirt")
        if nm != ""
            return nm
        endif
        nm = WornDeviceName(akActor, libs, "zad_DeviousAnkleShackles")
        if nm != ""
            return nm
        endif
        nm = WornDeviceName(akActor, libs, "zad_DeviousBoots")
        if nm != ""
            return nm
        endif
    endif
    return "leg restraints"
EndFunction

String Function GetAllBound(Actor akActor) Global
    if !akActor || !VRTE_DDZaZ_Native.IsRestrained(akActor, 4)
        return ""
    endif
    String nm = VRTE_DDZaZ_Native.RestraintName(akActor, 4)
    if nm != ""
        return nm
    endif
    zadlibs libs = DDLibs()
    if libs
        nm = WornDeviceName(akActor, libs, "zad_DeviousStraitJacket")
        if nm != ""
            return nm
        endif
        nm = WornDeviceName(akActor, libs, "zad_DeviousPetSuit")
        if nm != ""
            return nm
        endif
    endif
    return "full-body restraint"
EndFunction

; ★ HEARING (2026-08-27). Same two-step as the others: the DLL decides IF, this
; decides WHAT IT IS CALLED.
;
; ⛔ ONLY A HOOD IS BUILT IN, and that is measured rather than chosen: no hearing
; keyword exists anywhere in DD or ZaZ. The whole load order was scanned for
; ear/hear/deaf/sound/muffl and the only hits were `zbfEffectGagSound` (the
; gag's own OUTPUT, not the wearer's hearing) and two substring false positives,
; `zadNG_HideArms` and `zad_DeviousPonyGear`. A hood is the one device that
; reaches the ears - 134 records - which is why the worn-device block already
; says every hood dulls sound while gating the SIGHT half on a separate keyword.
;
; ⚠ The fallback noun is "a hood", not "earplugs": whatever matched, the
; built-in path can only have been a hood, and the INI path returns a real name.
String Function GetDeaf(Actor akActor) Global
    if !akActor || !VRTE_DDZaZ_Native.IsRestrained(akActor, 5)
        return ""
    endif
    String nm = VRTE_DDZaZ_Native.RestraintName(akActor, 5)
    if nm != ""
        return nm
    endif
    zadlibs libs = DDLibs()
    if libs
        nm = WornDeviceName(akActor, libs, "zad_DeviousHood")
        if nm != ""
            return nm
        endif
    endif
    return "a hood"
EndFunction

; =============================================================================
;  THE RELEASE SIDE — a countdown, not a state (2026-08-24)
;
;  USER'S PROBLEM, verbatim: "after removing it, she won't speak again cause the
;  LLM is used to not speak and it's part of it's context."
;
;  That is the real failure and it is not a bug in the constraint blocks. They
;  did their job; the LLM then has three or four turns of muffled non-speech (or
;  of refusing to act, or of not seeing) sitting in its context window, and goes
;  on imitating them. Withdrawing an instruction does not delete the examples it
;  already produced. So every constraint needs its own release instruction,
;  stated loudly, for as long as those turns are still in the window.
;
;  Returns "3" / "2" / "1" for the next three renders after the constraint ends,
;  then "". Empty at every other time, INCLUDING while it is still on — so a
;  constraint block and its release block can never both be up, with no
;  coordination between them. Putting the device back on cancels the countdown
;  on the very next render.
;
;  ⚠ THESE DECORATORS HAVE A SIDE EFFECT, on purpose and by necessity. Nothing
;    else fires once per LLM interaction: SkyrimNet pre-computes all Papyrus
;    decorators for the nearby actors once per cycle
;    (DecoratorLibrary::ExecuteAllPapyrusDecoratorsAsync), so the call itself IS
;    the clock. Nothing else in this script writes state; keep it that way.
;
;  ⚠ THE 5-SECOND FLOOR is what makes that safe. If a cycle ever ran the
;    decorators twice in quick succession the countdown would burn out before
;    she said a word — the exact failure being fixed. Erring long costs one
;    redundant line; erring short leaves her mute.
;
;  ⚠ COUNTED IN RENDERS, NOT SECONDS, deliberately. A time window would expire
;    unseen if she is not spoken to for a few minutes — which is precisely when
;    the stale context is still sitting there waiting to be imitated.
;
;  State lives in StorageUtil (PapyrusUtil VR) keyed on the actor: no quest, no
;  alias, no array, and it survives a save/load.
;
;  Detection is OBSERVATIONAL — restrained last call, not now — so it does not
;  care HOW the device came off: our two-hand pull, DD's menu, a quest script, a
;  console command, or a spell wearing off.
; =============================================================================
String Function Recovery(Actor akActor, Int cat, String wasKey, String leftKey, String stampKey) Global
    if !akActor
        return ""
    endif
    if VRTE_DDZaZ_Native.IsRestrained(akActor, cat)
        StorageUtil.SetIntValue(akActor, wasKey, 1)
        StorageUtil.SetIntValue(akActor, leftKey, 0)
        return ""
    endif

    if StorageUtil.GetIntValue(akActor, wasKey, 0) == 1
        StorageUtil.SetIntValue(akActor, wasKey, 0)
        StorageUtil.SetIntValue(akActor, leftKey, 3)
        StorageUtil.SetFloatValue(akActor, stampKey, Utility.GetCurrentRealTime())
        Debug.Trace("[VRTE_DDZaZ] restraint " + cat + " released on " + akActor.GetDisplayName() \
            + " - recovery block armed for 3 renders")
        return "3"
    endif

    Int n = StorageUtil.GetIntValue(akActor, leftKey, 0)
    if n <= 0
        return ""
    endif
    ; ★ D15 (2026-09-13): GetCurrentRealTime restarts at 0 on every launch while the
    ; stamp lives in the co-save, so after quit -> relaunch -> load the age went NEGATIVE
    ; and the countdown froze on the same number until real time caught up with the old
    ; session. A negative age is an expired one.
    Float age = Utility.GetCurrentRealTime() - StorageUtil.GetFloatValue(akActor, stampKey, 0.0)
    if age >= 5.0 || age < 0.0
        n -= 1
        StorageUtil.SetIntValue(akActor, leftKey, n)
        StorageUtil.SetFloatValue(akActor, stampKey, Utility.GetCurrentRealTime())
    endif
    if n <= 0
        return ""
    endif
    return n as String
EndFunction

String Function GetUngagged(Actor akActor) Global
    return Recovery(akActor, 0, "vrtedd_was_gag", "vrtedd_rec_gag", "vrtedd_rect_gag")
EndFunction

String Function GetUnblinded(Actor akActor) Global
    return Recovery(akActor, 1, "vrtedd_was_blind", "vrtedd_rec_blind", "vrtedd_rect_blind")
EndFunction

String Function GetUnboundArms(Actor akActor) Global
    return Recovery(akActor, 2, "vrtedd_was_arms", "vrtedd_rec_arms", "vrtedd_rect_arms")
EndFunction

String Function GetUnboundLegs(Actor akActor) Global
    return Recovery(akActor, 3, "vrtedd_was_legs", "vrtedd_rec_legs", "vrtedd_rect_legs")
EndFunction

String Function GetUnboundAll(Actor akActor) Global
    return Recovery(akActor, 4, "vrtedd_was_all", "vrtedd_rec_all", "vrtedd_rect_all")
EndFunction

; Hearing back. Three renders, exactly like the other five - and it earns its
; place for the same reason they do: the LLM has just written three or four
; turns of "could not make that out", and withdrawing an instruction does not
; delete the examples it already produced. Without this she goes on asking
; people to repeat themselves after the hood is off.
String Function GetUndeafened(Actor akActor) Global
    return Recovery(akActor, 5, "vrtedd_was_deaf", "vrtedd_rec_deaf", "vrtedd_rect_deaf")
EndFunction

; -----------------------------------------------------------------------------
;  THE WORN-DEVICE BLOCK (2026-08-26)
;
;  Returns the finished "### Devices worn" text, or "" when she wears nothing
;  covered — which is almost every NPC, almost always, and costs one native call.
;
;  ⚠ The template MUST still guard with `isString(x) and x != ""`. A Papyrus
;    decorator returning an empty string does not arrive as "" — it arrives as
;    an empty JSON object, and `{} != ""` is TRUE. That bug put the gag and
;    restraint blocks on every NPC in the game for a full session (report 23
;    §32). The guard is not optional anywhere in this mod, ever.
; -----------------------------------------------------------------------------
String Function GetWornDevices(Actor akActor) Global
    if !akActor
        return ""
    endif
    return VRTE_DDZaZ_Native.WornDeviceReport(akActor)
EndFunction

; -----------------------------------------------------------------------------
;  ★ THE OBSERVER VIEW (2026-08-30) - what someone ELSE can see on her.
;
;  Same block, run through the four-layer visibility rule; a covered UNDER
;  device and every INTERNAL one are omitted, and the access line is dropped.
;  Rendered for the DIALOGUE TARGET, which is where SkyrimNet's own equipment
;  dump currently leaks the unfiltered truth (report 30 §1).
;
;  ⚠ Same guard as every decorator here: the template must test
;  `isString(x) and x != ""` - an empty Papyrus result arrives as `{}` and
;  `{} != ""` is TRUE.
; -----------------------------------------------------------------------------
String Function GetWornDevicesVisible(Actor akActor) Global
    if !akActor
        return ""
    endif
    return VRTE_DDZaZ_Native.WornDeviceReportVisible(akActor)
EndFunction

; -----------------------------------------------------------------------------
;  ★ WHAT A RESTRAINT LEFT BEHIND (2026-09-10) - replaces the ddz_aftermath event.
;
;  GetAftermath HAS THE SIDE EFFECT the recovery decorators have, for the same
;  reason: SkyrimNet runs every Papyrus decorator once per conversation cycle, so
;  the call itself is the clock. Each read (5-second floor) moves the countdown on
;  - full, easing, fading, then "". The countdown lives in the DLL, beside the
;  strain model it reads.
;  GetAftermathVisible never moves it: an onlooker looking does not heal her.
; -----------------------------------------------------------------------------
String Function GetAftermath(Actor akActor) Global
    if !akActor
        return ""
    endif
    return VRTE_DDZaZ_Native.AftermathState(akActor)
EndFunction

String Function GetAftermathVisible(Actor akActor) Global
    if !akActor
        return ""
    endif
    return VRTE_DDZaZ_Native.AftermathVisible(akActor)
EndFunction
