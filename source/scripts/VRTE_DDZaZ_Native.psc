Scriptname VRTE_DDZaZ_Native Hidden Native
; =============================================================================
;  VRTE DD/ZaZ AddOn — the native bridge.
;
;  ⚠ The script-level `Native` flag is REQUIRED by Caprica for any script that
;    declares `Global Native` functions ("You can only define Native functions
;    in a script marked Native"). Bethesda's own compiler is lenient about this
;    and SkyrimNetApi.psc ships without it — Caprica is not, and the flag is
;    semantically correct for an all-native bridge anyway.
;
;  Implemented in VRTE_DDZaZ.dll (main.cpp RegisterPapyrus). If the DLL is
;  absent these calls are simply unbound: Papyrus logs one line per call site
;  and returns None/false, so the AddOn's decorators degrade to silence rather
;  than to a wrong answer.
; =============================================================================

; Put the AddOn's gestures to sleep for the duration of a scene.
Function SetScenePaused(Bool abPaused) Global Native

; -----------------------------------------------------------------------------
;  THE RESTRAINT REGISTRY
;
;  aiCat:  0 = gag    speech blocked
;          1 = blind  sight blocked
;          2 = arms   arms / hands unusable
;          3 = legs   walking cut to a shuffle
;          4 = all    cannot act at all
;          5 = deaf   hearing dulled          (2026-08-27)
;  ⚠ APPEND ONLY - the integers are a contract with the decorators and with any
;    user INI written against them. Never reorder.
;
;  Answers from THREE sources at once, which is why this is native and not a
;  keyword test in the prompt template:
;    * Devious Devices / ZaZ / Diary-of-Mine keywords, built in — no setup
;    * SKSE/Plugins/VRTE_DDZaZ_Restraints.ini — the user's own gear, matched by
;      NAME (substring), FormID, or worn keyword
;    * SPELLS and active magic effects, by FormID
;  The INI can only ADD. A broken line degrades to "DD still works", never to
;  silence.
; -----------------------------------------------------------------------------
Bool Function IsRestrained(Actor akActor, Int aiCat) Global Native

; The restraining item's display name, or "".
; ⚠ "" is NOT "unrestrained" — a DD device's WORN half carries no FULL record
;   (report 23 §30), so the name lives on the inventory half and only zadlibs
;   can reach it. Always ask IsRestrained separately.
String Function RestraintName(Actor akActor, Int aiCat) Global Native

; ★ 1.3.1 (2026-09-14, for VRTouchEvents - VRTE_to_DDSN_Request_2026-09-14_WornDeviceName_Native):
;   the REAL display name of the device she wears on any bit of aiSlotMask (the inventory half's
;   name: pushed name -> the shipped pair table -> the class push), "" when nothing tracked is worn
;   there or no name is known yet. A DD rendered half carries no name of its own, which is why
;   PPB's payloads say "The Gag" - this answers with "Black Leather Ball Strap Gag".
;   Call only while DD SN Database is loaded (VRTE keeps it behind its ddAddOnLoaded).
;   Receipt: VRTE_DDZaZ.log  [NAME] WornDeviceName 0x... slot N -> '...'
String Function WornDeviceName(Actor akActor, Int aiSlotMask) Global Native

; -----------------------------------------------------------------------------
;  WORN DEVICES — duration + strain (2026-08-26)
;
;  Returns the whole "### Devices worn" block, already formatted, or "" when she
;  wears no covered device (the common case, which must cost nothing).
;
;  ⚠ ONE NATIVE, NOT A FAMILY. SkyrimNet pre-computes every Papyrus decorator
;    for every nearby entity on every render cycle, so a per-device Papyrus walk
;    would multiply by actors AND by cycles. The DLL walks her worn armor once
;    and hands back the finished text.
;
;  Everything feeding it — the wear clock and the vibration/shock edges — runs
;  off ENGINE event sinks inside the DLL. There is no timer and no mod-event
;  registration anywhere in this mod, because with no ESP there is nothing for a
;  script instance to live on.
; -----------------------------------------------------------------------------
String Function WornDeviceReport(Actor akActor) Global Native

; -----------------------------------------------------------------------------
;  WHAT OTHERS CAN SEE (2026-08-30)
;
;  The same block, filtered through the four-layer visibility rule: INTERNAL
;  never shows, UNDER shows only while its region is bare, MID and OUTER always
;  show. The access line ("Sealed by what is worn...") is dropped - that is
;  knowledge about her body, not something a bystander can observe.
;
;  ⛔ WHY THIS EXISTS. SkyrimNet's own get_worn_equipment renders in the LIVE
;  dialogue prompt (0410_equipment.prompt's `dialogue_target` branch) and
;  returns every worn item WITH ITS KEYWORDS - so a plug under full plate is
;  already described to whoever she talks to. Report 30 §1 has the measured
;  trace. This native is the filtered channel that should replace it.
;  ⚠ Fails VISIBLE on an unclassifiable device: an over-reported device is a
;  wrong line, an under-reported one is a silent lie about her body.
; -----------------------------------------------------------------------------
String Function WornDeviceReportVisible(Actor akActor) Global Native

; The after-state (2026-09-10). AftermathState is the wearer's text for THIS prompt and
; ADVANCES the three-prompt countdown (5-second floor); AftermathVisible is what an
; onlooker can see of it, and never advances anything. Both "" when nothing is left.
String Function AftermathState(Actor akActor) Global Native
String Function AftermathVisible(Actor akActor) Global Native

; -----------------------------------------------------------------------------
;  FED BY THE CONTROL QUEST (2026-08-26)
;
;  The DLL cannot reach any of these three on its own, which is why the ESP
;  exists:
;    * arousal lives behind an OSL Aroused Papyrus global native, and the
;      sla_Arousal faction the DLL used to read is never written in this load
;      order - it returned -1 for every actor
;    * a DD device's NAME is on the INVENTORY half, which occupies no biped slot
;      and so is invisible to a worn-armor walk
;    * a climax is an event DD raises, not something to infer from arousal
; -----------------------------------------------------------------------------
Function NoteArousal(Actor akActor, Int aiValue) Global Native
Function NoteDeviceName(Actor akActor, String asClass, String asName) Global Native
Function NoteClimax(Actor akActor) Global Native
; ★ 1.3.5 the climax pose (VRTE_DDZaZ_Equip.ClimaxPose). ClipSecondsLeft: seconds left on the newest clip
; her behaviour graph is playing whose animation name contains asNeedle, as DD NG's replacer picked it
; (< 0 = none found). NotePoseEnd: the pose is over - the DLL may start things on her again.
Float Function ClipSecondsLeft(Actor akActor, String asNeedle) Global Native
Function NotePoseEnd(Int aiActorId) Global Native
; ★ 1.3.8: VibrateEffect's own return for the vibration DoVibrate just ran - 0 (ran, no climax, no edge) makes
; the device go off again at full arousal; 1+ (she came), -1 (edged) and -2 (refused) change nothing.
Function NoteVibrateResult(Int aiActorId, Int aiCame) Global Native
; ★ 1.3.6/1.3.7: the rank ClimaxPose gives her on DDSN_ClimaxPoseFaction (DD SN Database.esp 0x801) for
; climax number aiN - 4 = the Belt Edged kneel (4th or later, arms free, OAR + DD NG's clip present),
; 1 = DD's plain standing edged clip even when belted (climaxes 1-3 with arms free, and every climax while
; ZaZ/DoM restraints bind her arms), 0 = no rank: DD NG's own clip for a DD device it has one for.
; Read by DD SN's two config-only OAR submods.
Int Function ClimaxClipMode(Actor akActor, Int aiN) Global Native
; ★ 1.3.9: True while this NPC is talking in SkyrimNet - her reply is being written, her voice is playing, or she
; is between two of her own queued lines. Read from SkyrimNet's own speech/audio signals, per speaker.
Bool Function IsTalking(Actor akActor) Global Native

; ★ THE UNLOCKING KEY, pushed for the same reason the NAME is (2026-09-08).
; Devious Devices keeps it as a script PROPERTY on the INVENTORY half -
; zadEquipScript.deviceKey (:32) and .NumberOfKeysNeeded (:35) - and C++ cannot
; read a Papyrus property. At removal time the only half in hand is the RENDERED
; one, which carries neither. OnDeviceEquipped is the single place both are
; available, so it pushes the pair through here.
; ⚠ The key is an Int FormID, not a Form: an object crossing the VM boundary is
; packed as its most-derived attached script and the type check refuses the
; upcast. The DLL resolves the FormID on its side.
; ⚠ aiNeeded is DD's NumberOfKeysNeeded - a device can carry several locks.
; ⛔ 2026-09-10: the class String became the RENDERED device's FormID. Removal is
; asked about one exact rendered record, so the registry is keyed on exactly
; that - no second class resolver that has to agree with the first. Must ship
; together with the matching DLL. aiKeyFormId 0 = DD holds no key for this device,
; which in DD's own unlock means no key is REQUIRED.
Function NoteDeviceKey(Actor akActor, Int aiRenderedFormId, Int aiKeyFormId, Int aiNeeded) Global Native

; ★ The same push, for the NAME (2026-09-13). Filed under the exact rendered FormID
; like the key, so the DLL never needs a second class resolver to find it - and pushed
; by the same sweep for EVERY worn device, so every class is named within ~30 s of a
; load, not the 13 the class probe covers. Ships together with the matching DLL.
Function NoteDeviceRecord(Actor akActor, Int aiRenderedFormId, String asName) Global Native

; One line into VRTE_DDZaZ.log (2026-09-10). The AddOn's key rule reports what it
; did here, so the outcome pairs with the gate's approval in one file. Tag it yourself.
Function LogLine(String asLine) Global Native

; FormIDs of every actor the DLL is tracking, so the controller polls arousal
; for exactly those and nobody else.
Int[] Function TrackedActors() Global Native

; -----------------------------------------------------------------------------
;  THE STATE HEARTBEAT (2026-08-27)
;
;  ⛔ WHY THIS HAD TO EXIST. DD signals a running vibration by FACTION RANK
;  alone - zadLibs.psc:2347-2366, SetVibrating does SetFactionRank(
;  zadVibratorFaction, duration) and StopVibrating does SetFactionRank(...,0) +
;  RemoveFromFaction. A faction rank change applies NO magic effect.
;
;  But the DLL reads that edge only from its TESActiveEffectApplyRemoveEvent
;  sink, so a vibration started by DD's own NPC loop was seen only when some
;  UNRELATED effect edge happened to wake the sink on that actor - and a short
;  vibration (DD rolls 5..20 s itself) could miss BOTH edges and never be
;  narrated at all.
;
;  ⚠ Shocks were never affected: ShockActor does a real ShockEffect.RemoteCast,
;    which wakes the sink reliably. This is vibration-only.
;
;  Called from the SAME 3 s poll that already feeds arousal, over the same
;  TrackedActors() set - no new timer, no new registration. The DLL hops to the
;  main thread internally before doing any work.
; -----------------------------------------------------------------------------
Function PollStates() Global Native

; -----------------------------------------------------------------------------
;  THE CLOTHING GATE (2026-08-28) - the roadmap's realism rule:
;  "a body harness or a plug can't be put on a NPC wearing clothes there."
;
;  CanEquipDeviceOn answers whether the device's region is clear; EquipBlockedBy
;  names the garment in the way ("" when nothing is), so a refusal can SAY why -
;  which is what makes the rule readable to the LLM instead of a silent veto.
;
;  ★ THIS IS THE ELIGIBILITY PRIMITIVE for the future NPC->NPC equip action, and
;    it is deliberately built first ("base first"). Any mod may call it too.
;  ★ THE FOUR-LAYER MODEL (2026-08-29, the user's design):
;      INTERNAL  plugs - orifice open + pelvis free of clothes AND devices
;                (belt/suit block, honouring zad_Permit*)
;      UNDER     suits, piercings, harnesses, chastity belts, blindfolds,
;                tight hoods, smaller gags - install needs bare skin there
;      MID       corsets, boots, mittens, binders, posture collars - replace
;                clothing; same install rule as UNDER
;      OUTER     yokes, prisoner cuffs, ordinary collars - over anything;
;                on arms/legs only HEAVY armor blocks (light + clothes never)
;    Cloth-gag carve-out: over helmets, refused by a hood / another gag / the
;    face slot. VRTE_DDZaZ_ClothingGate.ini reclassifies by name substring.
;    A device resolving NO layer fails OPEN, logged.
;  ⚠ Pass EITHER half of a DD pair - the equipping side only ever holds the
;    INVENTORY half, and both halves carry the class keywords.
;  ⚠ Enforcement lands per path: the SkyrimNet action will consult this; PPB
;    enforces the VR gesture (change request filed); the MENU path stays open
;    on purpose - the player can undress her through the same menu, so a block
;    there adds a step, not realism.
; -----------------------------------------------------------------------------
Bool Function CanEquipDeviceOn(Actor akTarget, Form akDevice) Global Native
String Function EquipBlockedBy(Actor akTarget, Form akDevice) Global Native
