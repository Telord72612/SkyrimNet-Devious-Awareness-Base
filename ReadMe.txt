===============================================================================
  SKYRIMNET DEVIOUS AWARENESS BASE
  Devious Devices / ZaZ / Diary-of-Mine awareness for SkyrimNet
  v1.0                                                             SE / AE / VR
===============================================================================

  CONTENTS
    1.  What this is
    2.  Requirements
    3.  Installation
    4.  The Base and the AddOn are two separate mods
    5.  What is covered
    6.  STATE - the six things a device can do to someone
    7.  EFFECT - what wearing something does over time
    8.  SCALE - the phase ladder, the tiers, and the aftermath
    9.  EVENTS - what her body does, and what she says about it
    10. The layer system - what can go on over what
    11. Tuning: the INI files
    12. What reaches SkyrimNet, and how
    13. Troubleshooting
    14. Known state / credits


-------------------------------------------------------------------------------
  1.  WHAT THIS IS
-------------------------------------------------------------------------------

  SkyrimNet gives NPCs an LLM to think with. It does not know what a device is.
  An NPC in an armbinder and a ball gag will happily hand you an item and tell
  you about the weather, because nothing ever told the model she is wearing
  anything.

  This mod is the missing half. It watches every worn device in your load order
  and tells SkyrimNet, in plain physical language:

      what it IS         a padded leather armbinder, buckled and locked
      what it DOES       her arms are held behind her, elbows drawn together
      how long           four hours, and her shoulders have gone from ache
                         to a deep joint pain she cannot shift
      what STATE         she cannot use her hands at all
      what is VISIBLE    to her, everything; to a passer-by, only what shows

  It never tells the model how to FEEL about any of it. It reports the body and
  leaves the character to SkyrimNet, which knows her personality, history and
  disposition - and this mod does not. That division is deliberate and it is
  the reason the wording throughout is so flat.

  WHY IT IS CALLED A BASE
  It is the knowledge layer, not an interaction mod. It adds no quest, no menu,
  no hotkey and no way of putting a device on anyone. What it does is keep an
  accurate, live description of every device in your game and hand that to
  SkyrimNet in language a language model can use.

  That makes it something to build ON. Every piece of it is readable from the
  outside: the plugin broadcasts a mod event for every device change, climax,
  vibration, shock and stumble; it exposes Papyrus functions for asking what
  someone is wearing and whether a device may go on; and it publishes a C++
  interface other SKSE plugins can query. Our own VR mods are built on it, and
  they are separate downloads precisely so that this half stays useful to
  someone who has never installed them.

  WHAT IT ACTUALLY CHANGES IN PLAY
      She talks like someone wearing it   gagged, blindfolded, deafened, bound
                                          - and stops, correctly, when it comes
                                          off
      Her body answers for itself         a climax, a shock or a blindfolded
                                          stumble is something she reacts to
                                          out loud
      A gag lands mid-sentence            putting a gag or a hood on whoever is
                                          currently speaking cuts her off in
                                          the middle of the word
      Time is felt                        four hours in an armbinder does not
                                          read like four minutes
      Other people see the outside        a plug under a dress is hers to know
                                          and nobody else's

  WHAT IT DOES NOT DO
  It does not equip anything, unequip anything, or decide what should happen to
  anyone. It does not replace a single Devious Devices behaviour - the
  vibrations, shocks, moans and stumbles you see are DD's own, called by this
  mod at the moment its model says they belong. It overrides no mesh, no
  texture and no record.

  It is a DATABASE. It does not decide how devices are put on or taken off.
  Anything that equips a device by any route - a script, a menu, a container, a
  key, a quest, another mod entirely - is caught by an engine-level sink and
  described correctly with no work on your part.


-------------------------------------------------------------------------------
  2.  REQUIREMENTS
-------------------------------------------------------------------------------

  REQUIRED
    SKSE                  (VR: SKSEVR)
    Address Library       for SKSE Plugins (VR: VR Address Library for SKSEVR)
    SkyrimNet             the LLM layer this talks to
    Devious Devices       Assets + Integration + Expansion. The mod calls DD's
                          own zadLibs for vibration, shock, moans and stumbles,
                          so DD is a hard requirement even if all your gear is
                          from somewhere else.
    PapyrusUtil           (StorageUtil - used by the recovery countdown)
    Devious Devices NG    the climax animation is swapped through DD NG's own
                          animation set, and DD NG brings Open Animation
                          Replacer with it. Without NG everything else still
                          works; the climax simply plays DD's ordinary clip.

  RECOMMENDED - covered automatically if present, ignored if absent
    ZaZ Animation Pack
    Diary of Mine
    Devious Lore
    Deviously Accessible
    Laura's Bondage Shop

  NOT REQUIRED
    HIGGS, VRIK, Precision Physic Bodies, VRTouchEvents, any VR mod at all.
    This half is flatscreen-safe.

  ONE DLL COVERS SE, AE AND VR. There is no separate download.

  !! See the warning in section 14 about SE/AE.


-------------------------------------------------------------------------------
  3.  INSTALLATION
-------------------------------------------------------------------------------

  1.  Install with a mod manager (Mod Organizer 2 or Vortex). The installer
      always puts in the base files, then offers the prompt blocks. Gag,
      blindness, deafness and restraint awareness and the observer view are
      ticked by default. The equipment-list filter is not, because it
      replaces a SkyrimNet file - read its description before ticking it.
      The spell lock and the blindfold weapon lock (section 6) live in the
      base files, so they apply whatever you tick.

  2.  TICK "DD SN Database.esp".
      This is the step people miss. A new mod folder often lands UNCHECKED.
      If the plugin is not active, the SKSE side still loads and the log still
      says "loaded" - but device names and climax detection never arrive,
      because those come through Papyrus. See section 13.

  3.  Load order does not matter for this plugin. It overrides nothing.

  4.  Launch, then check the log (section 13). Four lines tell you it is alive.

  UPDATING
    Wear-time data lives in the SKSE co-save and survives an update.

    !! FROM ANY EARLIER VERSION: choose REPLACE, not merge. Prompt files have
       been renumbered twice, and a merge leaves the old copy beside the new
       one - which renders the same block TWICE, in two different places in
       the prompt. After updating, in
       SKSE/Plugins/SkyrimNet/prompts/submodules/user_final_instructions
       there should be NO file named 0770_vrtedd_worn.prompt (it is 0490 now),
       and no file that both starts with 09 and contains "vrtedd".

    !! This release moved the worn-devices block from 0770 to 0490 on purpose: it now
       renders BEFORE SkyrimNet's own "# Response Format" section, so a long
       list of devices no longer sits between the AI's formatting rules and its
       answer.

  UNINSTALLING
    Remove the mod. There is nothing to clean and no scripts left running on
    your actors - the wear clock lives in the DLL and its co-save, not in
    Papyrus properties attached to NPCs.

  WHY THERE IS AN ESP AT ALL
    It is ESL-flagged, so it costs no plugin slot. It exists only because
    Devious Devices announces some of its events carrying FORMS, and an SKSE
    C++ sink cannot receive a Form - only a Papyrus script can. Those two
    events are the climax counter and the device NAMES. Without the plugin
    ticked you lose both.


-------------------------------------------------------------------------------
  4.  THE BASE AND THE ADDON ARE TWO SEPARATE MODS
-------------------------------------------------------------------------------

  These are two different mods with two different downloads and two different
  installers. This is not one mod with an optional component.

  The line between them is what each one is ABOUT:

    SKYRIMNET DEVIOUS AWARENESS BASE  (this one)
        Device knowledge, and telling SkyrimNet about it.
        What a device IS, what it DOES over time, what STATE it puts someone
        in, and what can be SEEN of it. It notices a device going on or
        coming off; the line that tells the NPC it happened is the AddOn's.
        Nothing in it is VR-specific and nothing in it depends on any other
        mod of ours.
        Runs on Skyrim SE, AE and VR from the same DLL.
        Everything in this ReadMe is this half.

    DD SN ADDON                       (separate download)
        OUR integration of that database with our other mods.
        The VR hand gestures for putting devices on and taking them off
        (through Precision Physic Bodies), the tie-in with VRTouchEvents, and
        the SkyrimNet actions an NPC can choose for herself - and the
        narration of devices going on and coming off, on every runtime.
        This is the half that is VR-shaped and that knows about our other mods.

  THE BASE DOES NOT NEED THE ADDON. Install this on its own and everything
  described here works, on any platform. If you are writing your own way to put
  devices on actors, that is the intended way to use it: the DLL notices the
  AddOn's plugin is absent and simply never arms the gesture bridge, and no
  other behaviour changes.

  THE ADDON NEEDS THE BASE. It builds on it and adds nothing this ReadMe
  describes.

  !! THE PLUGIN FILE IS STILL CALLED "DD SN Database.esp", and so is the mod
     folder. Only the mod's NAME changed. Renaming either would break existing
     saves, so they stay as they are.

  ★ WHY THE SPLIT IS DRAWN THERE. Device knowledge is useful to anyone -
  another author can build a completely different interaction layer on it.
  Hand gestures, VR physics and our own mod-to-mod plumbing are useful to
  nobody who is not running our setup. Keeping them apart means the useful half
  carries no VR dependency at all.


-------------------------------------------------------------------------------
  5.  WHAT IS COVERED
-------------------------------------------------------------------------------

  1,553 worn records across six frameworks, sorted into 37 device classes.
  Every class has its own vocabulary - what it is called, what it does to the
  body, and where it sits.

  RESTRAINT
      Armbinder · ArmbinderElbow · ElbowTie · StraitJacket · Yoke · YokeFront
      YokeBB · ArmCuffs · LegCuffs · CuffsFront · AnkleShackles · BondageMittens
      PetSuit · PonyGear

  GAGS  (each with its own mouth mechanics)
      Gag · GagLarge · GagBit · GagRing · GagPanel · GagInflatable

  SENSORY
      Blindfold · Hood

  WORN / CLOTHING
      Collar · Corset · Harness · Belt · Bra · Suit · Gloves · Boots
      HobbleSkirt · HobbleSkirtRelaxed

  INTERNAL
      Plug · PlugVaginal · PlugAnal
      PiercingsNipple · PiercingsVaginal

  A device with no recognised class still gets its name, material, slots and
  visibility handled - it simply does not get a class-specific sentence.

  MATERIAL is read from the name, in a fixed order, and changes the wording:
  soulgem grades, chaos/shock/chargeable, stalhrim, ebonite, latex, rubber,
  leather, iron, steel, wood, gold and the rest. Material also decides whether
  a device can react to magic at all (section 9).


-------------------------------------------------------------------------------
  6.  STATE  -  the six things a device can do to someone
-------------------------------------------------------------------------------

  State is what the device PREVENTS. There are exactly six categories, and they
  are chosen by what the gear does to the body, never by what it is called:

    GAG          speech blocked - she can only make muffled sounds
    BLIND        sight blocked - she cannot see anything at all
    ARMS         arms or hands unusable - cannot hold, take, give, or fight
    LEGS         walking cut to a shuffle - cannot run, cannot keep pace
    ALL          cannot act at all - shown INSTEAD of ARMS and LEGS
    DEAF         hearing dulled - she receives speech but is told to treat it
                 as indistinct rather than act on the words

  Each one puts its own instruction block in front of the LLM for as long as
  the gear is on. A gagged NPC is told to answer in sounds, not sentences. A
  bound one stops offering to hand you things. A blindfolded one stops
  describing what she can see.

  ...........................................................................
  !! TOLD versus ENFORCED - the most important distinction in this mod
  ...........................................................................

  Almost all of the above is TOLD, not enforced. The mod puts the fact in
  front of the LLM and the LLM does the roleplay. A gagged NPC is not
  mechanically silenced by us - she is told she cannot form words, and she
  plays it. If your model ignores the instruction, she will talk.

  That is deliberate. Mechanically muting an NPC breaks quests, breaks
  vendors, breaks follower commands, and cannot be undone by an NPC who
  should reasonably be able to spit a gag out. Instruction degrades
  gracefully; enforcement does not.

  TOLD (roleplay - the LLM decides how to honour it)
      gag         cannot form words, only muffled sound
      arms        cannot hold, take, give, draw or fight
      legs        cannot run or keep pace
      all         cannot act
      deaf        hears sound, not words
      everything about wear, strain, aftermath and visibility

  ENFORCED (the game actually does it, whatever the LLM says)
      blind -> RANGED WEAPONS      a bow, a crossbow OR A STAFF in her hands
                                   is UNEQUIPPED, re-checked every 3 seconds
      blind -> AIMED SPELLS        any equipped spell that is not
                                   self-targeted is UNEQUIPPED. She keeps
                                   healing and buffs; she loses fireball.
      blind -> STUMBLING           she really falls, using DD's own animation
      blind -> MIS-TARGETING       in combat she swings at the nearest body
                                   rather than the right one

      gag   -> CASTING             every equipped spell is UNEQUIPPED, at any
                                   delivery. No words, no spell. She keeps her
                                   sword.

      All of these are self-healing in both directions: if her AI re-equips
      the bow, the next tick strips it again, and the moment the gear comes
      off it stops on its own. Nothing is left stuck on her.

      !! A STAFF IS NOT TAKEN BY A GAG, only by a blindfold. A staff channels
         through the staff and needs no words - it is the one way a silenced
         caster is meant to keep casting. A blindfold takes it because that is
         about aiming, not speech.

  WHY THOSE TWO AND NOTHING ELSE
      Both are cases where the LLM's choice cannot reach the engine. Telling a
      model "you are blind" does not stop an AI package from firing a bow, and
      telling it "you are gagged" does not stop one from throwing a firebolt.
      Everything the LLM CAN honour is left to the LLM.
      Blindness in particular has no in-game delivery at all: DD's blindfold
      effect is player-only, ZaZ's says so in its own comment, and the engine's
      Blindness value is used by no mod in a 2,000-plugin load order.

  ALSO ENFORCED, BUT BY SOMEBODY ELSE
      A few ZaZ items carry a "no magic" effect and ZaZ itself unequips her
      spells for those. We add a line saying the device smothers casting so
      the LLM knows why. Since this mod now blocks casting for EVERY gag, that
      ZaZ effect is no longer the only thing doing it - it just gets there
      first for its own gear.

  THE RELEASE SIDE
    When the gear comes off, the opposite block runs for the next THREE replies
    and then stops. This exists because an LLM that has spent an hour being
    told "you cannot speak" keeps acting gagged out of habit for a while. The
    countdown is measured in RENDERS, not seconds, with a 5-second floor so two
    quick exchanges cannot burn it out. Putting the device back on cancels it.

  WHAT IS ALREADY KNOWN
    Every DD gag, blindfold, hood, armbinder, yoke, straitjacket, elbow tie,
    bondage mitten, hobble skirt, ankle shackle and pony gear; ZaZ's zbfWorn*
    keywords; Diary of Mine's DOMWorn* keywords.

    BOOTS COUNT AS BOUND LEGS ONLY AT FULL GRADE - pony, restrictive and
    training boots, and any "(Tight)" boot. Every other boot forces careful,
    unsteady steps without cutting the pace, and says so in the worn list;
    socks and oil boots only keep the skin damp.

    Plain decorative wrist cuffs are deliberately NOT treated as binding. In DD
    they are worn like bracelets and restrict nothing; treating them as bondage
    would silently cripple a large share of the NPCs wearing them.

    DEAF is built in for hooded devices only. That is not an oversight: there
    is no hearing keyword anywhere in DD or ZaZ, and a hood is the only thing
    that covers the ears. A blindfold leaves the ears open, so it is not here -
    and neither is the Extreme Hood worn closed, which is a blindfold. Its
    (Open) version is a hood with holes for the eyes: it muffles hearing and
    speech but does not blind.

  You can teach it about gear or spells from any other mod - section 11.


-------------------------------------------------------------------------------
  7.  EFFECT  -  what wearing something does over time
-------------------------------------------------------------------------------

  A device does not just sit there. The longer it is worn, the more the body
  has to say about it, and the model tracks that continuously in GAME HOURS.

  The clock is per device, per actor, and it survives saving, loading and cell
  changes. Taking a device off stops it; putting the same KIND back on soon
  after resumes it partway up (section 8).

  What accumulates depends on the device:

    pressure and chafing        cuffs, collars, straps
    joint ache and stiffness    binders, yokes, anything holding a position
    jaw strain                  gags, scaling with how far the jaw is held open
    dampness and heat           sealed suits, latex, anything that cannot breathe
    ischemia                    clamps - and these run in MINUTES, not hours
    arousal                     plugs and piercings, with their own cadence

  Two devices of the same class on different tiers climb at different speeds.
  A cuff and an armbinder ride the same curve; the cuff simply takes six times
  as long to get anywhere, and stops much lower.


-------------------------------------------------------------------------------
  8.  SCALE  -  the phase ladder, the tiers, and the aftermath
-------------------------------------------------------------------------------

  THE LADDER
    Every device climbs the same five-step ladder. What differs is how fast it
    climbs and how far it is allowed to get.

      PHASE 1   silent. Nothing is reported.
      PHASE 2   a pressed mark, tenderness, first awareness
      PHASE 3   congestion, ache, stiffness setting in
      PHASE 4   deep joint ache, range of movement genuinely short
      PHASE 5   nerve block

    The shared boundaries are 1 / 3 / 6 / 12 game hours for phases 2/3/4/5.

  !! PHASE 5 IS THE QUIETEST STEP, NOT THE LOUDEST.
    This surprises people, so it is worth stating plainly. Phase 5 is nerve
    block: the limb has stopped reporting. The body says LESS while the harm is
    greatest. The model is monotone in HARM, not in complaint.

    This is why capping a device at phase 4 is not "gentler" - it pins it at
    the PEAK of complaint forever and removes the only quiet ending it had. If
    you want a device to be milder, slow it down instead of capping it.

  THE TIERS
    Each device belongs to one tier, which scales the boundaries and caps the
    climb. Higher multiplier = SLOWER.

      tier        speed   ceiling   what it models
      ---------   -----   -------   ----------------------------------------
      arms         1.0       5      arms held in a position
      limb         1.5       5      legs, hobbling
      strap        6.0       2      cuffs, collars - chafe and stop there
      yoke        1.25       4      a rigid bar holding the shoulders
      rope         2.0       4      rope-work
      suit         1.0       5      full sealed suits
      gagOpen      0.5       4      the jaw held open
      gagFill      0.5       4      the mouth filled
      clamp       0.08       5      ischemia - MINUTES, not hours
      mitt         1.0       5      hands sealed
      damp         2.0       4      the moisture/heat axis, partial gear
      dampSealed   1.0       5      the moisture/heat axis, a full sealed suit

    Per-class caps apply on top: boots stop at 3, heeled boots at 2.

    strap's ceiling of 2 is deliberate. An ordinary cuff or collar chafes and
    stops there. It never claims nerve damage, however long it is worn.

    clamp's 0.08 is not a typo. Nipple clamps are measured in minutes and get
    QUIETER as sensation fails, which is the honest and more alarming reading.

  THE AFTERMATH  -  what a device leaves behind
    Taking a device off does not end it. The phase reached at removal decides
    what the body keeps reporting. It is a block in the NPC's own prompt, not
    an event, and it lasts THREE prompts: the full line, then the same easing,
    then only what little remains, fading. The hours below are its longest
    life if nobody talks to her in the meantime (game hours):

      from phase 2   1.0 h    a pressed mark and tenderness
      from phase 3   2.0 h    congestion clearing, ache and stiffness
      from phase 4   4.0 h    deep joint ache, range still short
      from phase 5   0.5 h    feeling flooding back  [reperfusion]
                 then 6.0 h    weakness, clumsiness, numb patches  [deficit]

    Phase 5 runs in two parts, and that is the point: removal is the LOUDEST
    moment in the whole model, precisely because wearing was the quietest.

    Only the reperfusion figure is anchored to a real source - a released
    ischemic block returns over minutes. The other four are an argued shape:
    ordered, roughly doubling, defensible - not a measurement. They are in the
    INI so you can disagree with them without needing a rebuild.

    Cuffs and collars mark the skin and nothing more, and only after a LONG
    stretch - below 24 hours worn they leave nothing at all. Without that
    threshold every NPC who wore a collar for an evening reports a mark.

    DECAY AND CREDIT
    Strain winds down over 1 game hour per TIER once a device is off. Put the
    same kind of device back on inside that window and it resumes partway up
    its curve instead of starting fresh - an evening's collar taken off and
    refastened is not a fresh neck.

  A BYSTANDER SEES SOMETHING DIFFERENT
    Phase 5 opens on its reperfusion line and moves on to the deficit line.

    Aftermath has a separate observer wording, shown to whoever is talking to
    her for the first two of those prompts (the Observer View option). A
    passer-by sees a limb being shaken out; numbness and an internal ache stay
    hers alone. Where there is no outward sign at all, they are told nothing.


-------------------------------------------------------------------------------
  9.  EVENTS  -  what her body does, and what she says about it
-------------------------------------------------------------------------------

  Things happen to someone wearing devices. Each produces a line for the
  wearer, a separate line for anyone who could actually have seen it, and
  usually an expiring after-state.

  SPOKEN, OR JUST KNOWN
  Three of them happen TO her body hard enough that she answers them out loud -
  a CLIMAX, a SHOCK and a blindfolded STUMBLE. She says something about it in
  her own words, and if she was in the middle of a sentence when it hit, that
  sentence is cut off first. Everything else - a device starting or stopping, a
  device set off by her own magic, what an onlooker saw - is background she
  knows and can refer to, without being made to announce it.

  Only ever HER line is cut. If somebody else is talking, they finish.

  CLIMAX
    Detected from Devious Devices' own orgasm event - not guessed from arousal.
    The count is per actor and it matters:

      1st to 3rd    "The devices brought her off where she stood. She rode it
                    out on her feet."
                    Seen as: stopped, shuddered through something, carried on.
                    After-state: 150 seconds - breathing has not settled.

      4th onward    she cannot stay upright: she goes down onto her knees and
                    stays there, shaking, until the animation ends.
                    Seen as: dropped to her knees, shook, got back up.
                    After-state: 180 seconds - legs still loose, breathing
                    not even.

    !! SHE ONLY KNEELS IF HER ARMS ARE FREE. An NPC in an armbinder, a
       straitjacket, a yoke, cuffs or any other arm restraint - from Devious
       Devices, ZaZ or Diary of Mine - keeps the animation her own restraint
       calls for and is described as riding it out on her feet, because a woman
       with her arms strapped behind her cannot catch herself on the floor.

    While the animation runs, NOTHING else is allowed to fire at her - no
    shock, no second vibration, no stumble - and it is held for exactly as long
    as the animation actually plays, not a guessed number. Her arousal eases
    back afterwards rather than staying pinned.

    "Edged" - denied rather than finished - is recorded but deliberately NOT
    narrated as a climax, because it is the opposite of one.

  VIBRATION
    Not a dice roll - an EDGE. The mod watches for a device actually starting
    or stopping, so a two-minute vibration is ONE event rather than one per
    poll. Plays DD's own vibration effect and a moan.
    !! DD's own NPC loop is plug-only, so an NPC wearing nothing but vibrating
    piercings never vibrates at all in DD. This counts piercings too.

  AT FULL AROUSAL, THE DEVICE GOES OFF AGAIN
    Devious Devices rolls for a finish while a device runs, and that roll can
    simply not land. If it runs out with nothing while she is at full arousal,
    the device starts again a few seconds later, and again, until she either
    finishes or - in public, where DD only teases - is left wanting.

    That whole chain is told as ONE start and ONE stop. She does not accumulate
    ten "the device started / the device went still" pairs in her memory for a
    single unbroken stretch.

  SHOCK
    6% per shocking device worn - so the chance scales with how many she has on
    - gated on arousal, with a 90-second cooldown per actor. Deals a small
    amount of damage per device, capped at four devices.

    WHERE it discharges decides what it does to her:
      a plug, deep inside      the strength goes out of her legs and she drops,
                               loose, and stays down a few seconds before
                               picking herself up
      piercings, at the skin   sharp and over at once; she sinks to her knees
                               for a moment
      both at once             harder than either, and described as such

    A bystander sees her buckle and go down with no visible cause.
    After-state: 120 seconds, the muscle still twitching.

  SPELLFIRE  ("cast")
    A magicka-reactive device set off by its wearer casting a spell.
    15% per worn plug, 5% per worn piercing, 45-second cooldown per actor.
    !! Only devices whose MATERIAL scores on the soulgem ladder can do this.
    Iron, primitive, steel, ebonite, leather, locking, inflatable and tail
    gear are inert - an iron plug can never fire it, however many are worn.

    AROUSAL FROM IT IS PER DEVICE AND CUMULATIVE, BY SITE:

        nipple  +1     anal  +2     clitoris  +3     vaginal  +3

    so all four worn is +9, and the two piercings alone is +4. Every worn
    device contributes; this is not "the strongest one wins". (Only the
    strongest device FIRES the sound and animation - those are separate
    questions, and treating them as one is why a woman in four devices used
    to gain exactly as much as one in a single plug.)

    Tunable in the [arousal] section of the scales INI.

    WHERE SHE IS STANDING CHANGES WHAT YOU SEE, NOT WHAT SHE FEELS.
    The arousal above is applied in all three cases:

      in combat        she makes a sound and keeps fighting
      in public, or
      while watched    she makes a sound and nothing else
      alone, private   Devious Devices' own vibration plays in full - the
                       animation, the sound, and DD's own arousal on top of
                       ours. This is the loudest case by design.

    !! Before this the arousal only happened in the third case, because it was
    a side effect of the animation. A device that fired in a fight or in a
    tavern moved her arousal by exactly nothing.

    !! There is no longer a combat stagger. A device going off used to make
    her stumble mid-fight, which handed her opponent a free opening every
    time her own gear fired.

  STUMBLING  ("trip")
    A blinded NPC misjudges the ground. 5% while walking, 25% at a run, 50% on
    stairs or a slope, checked every 3 seconds while she is actually MOVING,
    with a 30-second cooldown after a fall. Standing still is safe; so is being
    in a hobble skirt that reduces her to a shuffle, for as long as she shuffles
    rather than walks. A blinded NPC in combat also swings at the nearest body
    rather than the right one - 25%, with a 12-second cooldown between target
    switches.

  STRUGGLING OUT OF THEM
    If you run Better NPC Support for Devious Devices with its escape system
    switched on, an NPC will try to get out of her gear by herself. That attempt
    is reported while it happens, and its RESULT is a spoken line: what came
    off, how it came off - unlocked with a key she had, worked off by effort, or
    simply taken off - and what stayed on. A device that comes off during the
    struggle is folded into that one result instead of being announced
    separately, so an escape reads as one event and not five.

    If what she got off had been on long enough to leave real strain, she goes
    down on one knee for a moment when it is over.

  A GAG THAT LANDS MID-SENTENCE
    Put a gag, a hood, a panel gag or a bridle on the NPC who is speaking AT
    THAT MOMENT and her line stops in the middle of the word. Without this she
    finishes the sentence in clear speech with the gag already strapped on, and
    only the NEXT line is muffled.

    It fires however the gag got there - your hands, a menu, a script, a quest.
    It only fires for the person actually talking.

  DEVICES GOING ON AND COMING OFF
    This mod NOTICES every one - the wear clock, the strain, the keys and the
    live state the prompts read all hang on it - and broadcasts each as a mod
    event any mod can hook. The spoken line itself belongs to whoever caused it:
    the AddOn narrates a device put on or taken off through a menu or a script,
    and VRTouchEvents narrates one done by hand in VR.

    A removal reaches her prompt AT THE MOMENT the device leaves her body, so
    the very next thing she says is already spoken by someone no longer wearing
    it. (Before 1.3.11 the prompt could lag a few seconds behind the line that
    announced the removal, and she would answer as though it were still on.)

    What a device leaves behind once it is off is awareness, and it is here:
    section 8.


-------------------------------------------------------------------------------
  10.  THE LAYER SYSTEM  -  how devices sit on the body
-------------------------------------------------------------------------------

  !! READ THIS FIRST: IN THIS MOD THE LAYER SYSTEM RESTRICTS NOTHING.

  It is an AWARENESS model. It answers "how is this device sitting on her, and
  what can actually be seen of it" so the LLM is told the truth - a plug under
  a dress is not described to the room, and a corset under a cuirass is known
  to be under it.

  NOTHING HERE STOPS YOU EQUIPPING ANYTHING.
    Put devices on through a menu, a container, a script, a quest, a console
    command - it all works, in any order, over anything. There is no menu
    restriction and there never was one. You cannot un-equip a locked device
    through a menu either, but that is Devious Devices' own rule, not ours.

  WHERE THE ONE REAL RESTRICTION LIVES
    There IS a layer-based restriction, but it applies to exactly one thing:
    putting a device on - or taking one off - BY HAND IN VR, by pressing or
    pulling at a body. That is the DD SN AddOn's territory, driven through
    Precision Physic Bodies, and it fires only for the VR hand gesture. Its
    purpose is to stop you pushing under-layer gear through a suit of heavy
    armour with your palm, to stop a second device going on where a device
    already sits (a gag onto a gagged mouth), and to stop a locked device
    coming off without the key. If you are not playing in VR, or not using the
    hand gesture, nothing in this paragraph applies to you.

    This mod is what ANSWERS those questions - it owns the layer model and it
    knows which key opens which device - but it never asks them of you. It is
    consulted; it does not intervene.

  So the four layers below describe how gear SITS, not what you are allowed to
  do. They are here because the description depends on them.

  Every device is sorted into one of four layers.

    INTERNAL   plugs and internal piercings. Sits inside; a chastity belt or
               a sealed suit over the pelvis covers it completely - which is
               why an onlooker is told nothing about it.

    UNDER      worn against the skin, with clothes over it. Suits, piercings,
               harnesses, chastity belts, blindfolds (the closed Extreme Hood
               included), smaller gags, socks.

    MID        sits where clothing would. Corsets, boots, mittens, pet suits,
               the binder family, posture collars, gas masks, rebreathers,
               pony harness gags - and every hood: nothing goes on over one.

    OUTER      sits over everything, armour included. Yokes, prisoner cuffs
               and shackles, ordinary collars, the sack hood.

  REGIONS
    The body is divided into head, face, neck, torso, arms, feet and pelvis.
    A device's region plus its layer is what decides whether it is visible to
    someone looking at her, and how it is described.

  WHAT THIS CHANGES IN PRACTICE
    An UNDER or INTERNAL device under clothing is described to the WEARER in
    full and to an onlooker not at all. An OUTER device is described to both.
    That is the whole visible/worn split in section 12, and the layer is what
    drives it.

  You can move any device between layers by name - section 11. Doing so
  changes how it is DESCRIBED, and (in VR only) whether the hand gesture will
  place it through armour.


-------------------------------------------------------------------------------
  11.  TUNING: THE INI FILES
-------------------------------------------------------------------------------

  They live in  SKSE/Plugins/  and are read at startup. Every one of them
  explains its own syntax at the top of the file, with examples. Every rejected
  line is named and explained in the log.

  ...........................................................................
  VRTE_DDZaZ_Scales.ini      every timer and every chance in the model
  ...........................................................................

    THE ONE SETTING MOST PEOPLE WANT is at the top:

        [pace]
        global = 1.0        0.5 = everything twice as FAST  (harsher)
                            2.0 = everything twice as SLOW  (gentler)

    It multiplies the per-tier speeds, so the SHAPE of the ladder is kept - a
    cuff still stops early and a yoke still outruns an armbinder.

    Eight sections:
        [pace]        the one global dial
        [phase]       the shared 1/3/6/12 hour boundaries
        [multiplier]  per-tier speed. HIGHER = SLOWER.
        [ceiling]     per-tier cap, 1-5, plus per-class caps
        [cooldown]    REAL seconds between repeatable events
        [chance]      percentages, 0-100
        [arousal]     per-site arousal from spellfire, summed over devices
        [aftermath]   what is left behind after removal, in game hours

    SYNTAX: key = number. Keys are LETTERS ONLY, no digits and no underscores.
    That is why the phase keys read "first/second/third/fourth" and the gag
    tiers "gagopen" and "gagfill". Values are range-checked; one outside its
    range is refused, logged, and the default kept.

    Deleting the file entirely is safe - the mod uses the same numbers from
    code.

  ...........................................................................
  VRTE_DDZaZ_ClothingGate.ini      which layer a device installs at
  ...........................................................................

    Four sections - [internal] [under] [mid] [outer] - and one key:

        [outer]
        name = Iron Shackles

    Matching is a case-insensitive SUBSTRING against the device's display
    name. One line per needle. When a name matches several sections the MOST
    PERMISSIVE wins, so a needle can only ever loosen a device, never trap it.

    Use this when a mod's gear is classified in a way you disagree with - a
    rope harness that should sit against skin, a mask that should replace
    headgear rather than slide under it, cuffs that should go over armour.

    The INI overrides the LAYER only. The region still comes from the device's
    own keywords and slots.

  ...........................................................................
  VRTE_DDZaZ_Restraints.ini      teach it gear and spells it does not know
  ...........................................................................

    Six sections - [gag] [blind] [arms] [legs] [all] [deaf] - and three kinds
    of line.

    !! [deaf] ships COMMENTED OUT at the bottom of the file, because nothing
       needs to be added to it by default. Delete the leading ";" on the
       section header and your lines to switch it on.

    The three kinds of line:

        name    = Ball Gag              any worn item or SPELL whose name
                                        contains this text, case-insensitive
        form    = 0x012345~SomeMod.esp  one exact record: an ARMOR, a SPELL,
                                        or a MAGIC EFFECT. The ~Plugin part is
                                        required.
        keyword = zbfWornGag            any worn keyword's EditorID - one line
                                        covers a whole mod's gear

    A piece of gear may appear in several categories. A hood that covers the
    eyes and the mouth belongs in both [gag] and [blind].

    THIS FILE CAN ONLY ADD. Nothing written here can switch off the built-in
    coverage, so a mistake costs you the line you got wrong and nothing else.

    Because it accepts spells and magic effects, a blindness spell or a
    paralysis effect from any mod can drive the same instruction blocks as a
    physical device.

  ...........................................................................
  VRTE_DDZaZ_Pairs.ini      NOT a settings file - do not edit it
  ...........................................................................

    A generated lookup table of the 1,217 Devious Devices pairs in the load
    order it was built on, matching each device you can carry to the one that
    is actually worn. It exists because those two halves are separate records
    and only the carried one has a name. Nothing in it is a preference, and a
    hand edit will simply be wrong; it is regenerated, not tuned.


-------------------------------------------------------------------------------
  12.  WHAT REACHES SKYRIMNET, AND HOW
-------------------------------------------------------------------------------

  There are two directions, and the difference matters if you are building on
  this.

  A.  WHAT THE PROMPTS PULL  -  standing state

    Twelve prompt files are added, and they read sixteen "decorators" - small
    questions the prompt asks about an actor at render time.

      six STATE decorators        gag name, blind, arms bound, legs bound,
                                  all bound, deaf
      six RELEASE decorators      the three-render countdown after each one ends
      two WORN decorators         the full worn-device block, and the
                                  visible-only version of it
      two AFTER-STATE decorators  what a restraint left behind, and what an
                                  onlooker can see of it

    The prompt files, in the order they render:
      0490 worn devices                          what is on her - before the response rules
      0780 restraint · 0785 blind · 0786 deaf    what it takes away
      0790 what a restraint left behind          for three prompts
      0791 ungagged · 0792 unblinded · 0793 undeafened · 0794 unbound
      0795 gag                                   last of all of ours
      then SkyrimNet's own 0800 "this event just happened" line, after every
      block above, so the moment she reacts to is the last thing she reads
      0950 visible devices                       (in the character bio)
      0410 equipment                             (a replacement - see below)

    RENDER GATING is deliberate:
      the gag block does NOT render in thoughts - a gag stops speech, not
        thinking
      the worn-devices block renders in EVERY mode including thoughts - it is a
        fact about her body, not a topic
      the visible-devices block renders only when she is the one being spoken
        to, so a wearer never gets the censored view of herself

    THE VISIBILITY FIX
      SkyrimNet's stock equipment prompt describes a conversation partner's
      worn items with keywords and with clothing ignored - which means a plug
      under a dress was being described to whoever she was talking to. This mod
      ships a keyword-filtered replacement for that one file. It is merged onto
      SeverActions' version if you have it, so nothing of theirs is lost.

  B.  WHAT THIS MOD PUSHES  -  things that happen

    Three delivery kinds, chosen by how much attention the moment deserves:

      PERSISTENT      the wearer's own experience. Sits in her event history as
                      context. Used for the effects in section 9.

      SHORT-LIVED     expires by itself. Used for two things:
                        ddz_witnessed - what a bystander saw. Keyed per
                          (observer, subject) so each watcher independently
                          remembers the latest thing they saw happen to that
                          person. 10 minutes.
                        ddz_aftermath - the expiring after-state from section 9.
                          A second effect on the same actor replaces the first.

      INTERRUPT       cuts into whatever is being said. Used for nothing
                      here now - a plug going in or out is DD SN AddOn's.

    WHO COUNTS AS A WITNESS
      Up to 4 actors within roughly 1050 units, each of whom must have line of
      sight AND actually have detected her. The player, the dead, the disabled
      and children are excluded - and so is every mannequin, and any actor
      with no name. Even short-lived, a crowd is noise.

    ALL OF IT IS PHYSICAL FACT ONLY. No line this mod produces tells the model
    that something is humiliating, arousing, frightening or deserved. Those are
    conclusions for the character to reach.


-------------------------------------------------------------------------------
  13.  TROUBLESHOOTING
-------------------------------------------------------------------------------

  THE LOG IS THE FIRST STOP, ALWAYS:

      Documents\My Games\Skyrim VR\SKSE\VRTE_DDZaZ.log
      (SE/AE: Documents\My Games\Skyrim Special Edition\SKSE\)

  Four lines say it is alive:

      SkyrimNet Devious Awareness Base v1.0.0 loaded.
      [SCALES] 56 value(s) taken, 0 refused, pace x1.00
      [RESTRAINT] ...: N rule(s) loaded, 0 rejected | gag=N blind=N ...
      [GATE] layer INI: 30 needle(s) - internal=0 under=8 mid=11 outer=11

    The [GATE] line appears the first time a device is equipped, not at
    startup. Its absence at launch is normal.

  "NOTHING HAPPENS AT ALL"
      Check the plugin is ticked (section 3, step 2). A new mod folder often
      lands unchecked, and the SKSE half still logs "loaded" without it.

  "MY INI EDIT DID NOTHING"
      Read the log. Every refused line is named, with the reason. The commonest
      cause in Scales.ini is a key containing a digit or an underscore - keys
      are letters only.

  "A LOCKED DEVICE WON'T COME OFF BY HAND"
      By hand in VR, with DD SN AddOn installed, a locked device comes off
      when you are carrying the key Devious Devices assigns to it - the same
      key its own menu asks for. The
      refusal message names that key by the name it has in YOUR game: with
      Devious Lore installed, the restraints key is the "Simple Skeleton Key"
      and the chastity key is the "Ornate Skeleton Key".

      A device DD assigns no key to comes off without one, exactly as DD's own
      menu allows. Quest devices and generic-block devices stay on regardless.

      With your own wrists bound (an armbinder, a yoke - any heavy bondage),
      nothing comes off by hand, key or not. That is Devious Devices' own rule
      for unlocking someone else.

      THE KEY IS SPENT THE WAY DD SPENDS IT. With DD SN AddOn installed, a hand
      unlock uses up the key exactly as DD's own menu would: when
      "Consume Keys" is on in DD's MCM (the default), or when the device
      destroys its own key.
      With Consume Keys off, the key stays in your pack.

      For about half a minute after a save loads, a device that was already worn
      may still refuse: each worn device's key is read on a sweep every 30
      seconds, and until the sweep reaches it there is no record. Try again
      shortly and it clears.

      In the log, a device whose key has been read looks like this:
          [WORN] key 0x1301DE4F device 0x2901614D = 0x2A01775F 'Simple Skeleton Key' x1
      and a successful removal like this:
          [WORN] REMOVE ALLOWED 0x1301DE4F device 0x2901614D - 'Simple Skeleton Key' x1 held (1)
      a refusal like this:
          [WORN] REMOVE REFUSED 0x1301DE4F device 0x2901614D - This device can only be removed with the Simple Skeleton Key, and will stay on otherwise.
      and the key being spent (DD SN AddOn) like this:
          [KEY] Black Leather Ball Strap Gag unlocked by hand - 1x 'Simple Skeleton Key' spent (DD's Consume Keys)

      !! This only ever affects taking devices off BY HAND IN VR. Devious
      Devices' own menu is unaffected and always works.

  "A DEVICE WON'T GO ON"
      By menu, container, script or console: not this mod - nothing here
      restricts those routes (section 10), and a refusal there is Devious
      Devices' own rule. In VR, placing a device BY HAND with DD SN AddOn
      installed, the gate refuses:
        - a device whose body slot another worn device already holds - a second
          gag on a gagged mouth. The message names the device already there;
          the device stays in your hand, and this log reads:
            [GATE] EQUIP REFUSED 0x... device 0x... - 'Black Leather Ball Strap Gag' is in the way
        - an under-layer device pushed through clothing or armour. The message
          names the garment; moving the device to [outer] in the clothing-gate
          INI lets it through.

  "A DEVICE IS DESCRIBED AS VISIBLE WHEN IT SHOULD NOT BE" (or the reverse)
      That IS the layer system. Move it between [under] / [mid] / [outer] in
      the clothing-gate INI - section 11.

  "AN NPC ISN'T ACTING RESTRAINED"
      Check the [RESTRAINT] counts in the log. If the gear is from a mod that
      is not in section 5, add it - section 11.

  REPORTING A BUG
      Attach the log. Line 2 gives the version, which is how anyone can tell
      which build you were actually running.


-------------------------------------------------------------------------------
  14.  KNOWN STATE
-------------------------------------------------------------------------------

  This is a BETA.

  VERSION PAIRING  -  only if you also use VRTouchEvents
    1.2.3 and later go with VRTouchEvents 3.3 (built 2026-09-13) or later. The
    two changed together - update both at once:
      - an OLDER DD SN with VRTouchEvents 3.3: onlookers are told about the
        player's masturbation twice.
      - DD SN 1.2.3+ with an OLDER VRTouchEvents: nobody is told about it, and
        devices and gear put on BY HAND in VR are not narrated at all.
    Without VRTouchEvents none of this applies.
    1.2.4 and later go with DD SN AddOn 1.2.4 (its lines read the fields the
    way these versions send them).

  NEW IN 1.0  (the first public release - everything since 1.2.6)

    THE NAME. The mod is now SkyrimNet Devious Awareness Base. The plugin file
    and the mod folder keep their old "DD SN Database" names on purpose, so
    existing saves, patches and load orders are untouched.

    SHE ANSWERS HER OWN BODY
    - A climax, a shock and a blindfolded stumble are now SPOKEN reactions. Up
      to now they were filed as things she knew about and never mentioned.
    - If she is talking when one lands, her sentence is cut first.
    - Putting a gag or a hood on WHOEVER IS SPEAKING cuts her off mid-word.
      Before this she finished the sentence in clear speech with the gag on.
    - Only ever the speaker's own line is cut; nobody else is interrupted.

    THE PROMPT KEEPS UP
    - What she is wearing, her restraint state and what onlookers can see are
      written to a live file the prompts read, instead of being asked for one
      answer at a time. A device going on or coming off shows up in her very
      next reply.
    - A removal reaches that file the moment the device leaves her body. It
      used to lag a few seconds behind the line announcing it, so she answered
      as though it were still on.
    - The worn-devices block now renders BEFORE SkyrimNet's own "# Response
      Format" section (the file moved from 0770 to 0490), so a long device list
      no longer sits between the AI's formatting rules and its answer.

    THE CLIMAX ANIMATION
    - No more ending flat on the floor: DD's supine clip is held off and a
      standing one plays instead.
    - From the fourth climax she goes down onto her knees - but ONLY if her
      arms are free. Bound arms keep the animation her restraint calls for.
    - The pose is held for exactly as long as the animation really plays, and
      nothing else may fire at her while it does.

    EFFECTS
    - Shock by site: a plug takes her legs out from under her and she stays
      down a few seconds; piercings drop her to her knees briefly.
    - At full arousal a device that ran out without a finish starts AGAIN a few
      seconds later, until she finishes or is left wanting. The whole chain is
      told as one start and one stop.
    - Blindfolded stumbles now roll while she is actually moving. They could
      previously never happen to a slowed or indoor NPC.
    - If Better NPC Support's escape system is on, her struggle and its result
      are reported - what came off, how, and what stayed on - and a long-worn
      restraint coming off puts her on one knee for a moment.
    - Removing a device no longer resets her arousal, and a scene stripping her
      gear no longer counts as taking it off.
    - The five-minute guard added in 1.2.6 after a full-arousal climax is GONE.
      It was never asked for and it blocked the next one.
    - Moans work again: a compile-time stub declared Devious Devices' moan
      function with the wrong number of arguments, so every moan this mod asked
      for was silently refused.

    SETTINGS
    - VRTE_DDZaZ.ini is no longer shipped. It held no settings - it was a note
      saying the hand-gesture knobs had moved to Precision Physic Bodies.
    - The retired "plugecho" key is gone from the Scales INI. It was read and
      ignored. Every remaining key in that file does something.
    - The Restraints INI lists all six categories at the top, [deaf] included.

    REQUIREMENTS
    - Devious Devices NG is listed as required: the climax animation is swapped
      through its animation set, and it brings Open Animation Replacer with it.

  (The entries below are this mod's development history, under its old name
   "DD SN Database". 1.0 above is the first public release.)

  NEW IN 1.2.6

    - A climax no longer ends on the floor. Devious Devices' own orgasm clip
      is a static supine pose; the first three climaxes now play DD's standing
      clip over it (feet planted, hands to the front of the hips) and from the
      fourth on the edged clip, which on a belted NPC is DD's own kneel, with
      three moans. Arousal is set to 50 afterwards. This applies to every
      climax the mod credits, whichever lane started the vibration.
    - A vibrating device at full arousal goes off by itself: at 99 or more,
      off a five-minute cooldown ([cooldowns] full in the Scales INI), the
      vibration starts, the finish is permitted indoors and she is edged
      elsewhere, never in combat. Devious Devices' own loop would do this once
      per 1.5 game hours and only for the NPCs it monitors.
    - Only the Shocking Soulgem plugs and piercings discharge (see 1.2.5).
    - A few event lines started a sentence with "she" in lower case; the
      capitalised pronoun is gone from them.

  NEW IN 1.2.5  (what the first VR session on 1.2.4 showed)

    - Devious Devices puts a device back on within a moment of taking it off
      - on every plug or gag change, outfit re-apply and cell load - and that
      was told as a real removal and a new equip ("drew the plug out" five
      times for a plug that never moved), while the one real removal was
      swallowed. Removal lines now wait about two seconds and are dropped if
      the device comes straight back; a device already on is not "now
      wearing" again; every device class gets the same short echo guard.
    - A shock-capable plug going in no longer reads as a jolt. The effect that
      marks such a plug sits on the wearer the whole time it is worn; only
      the real discharge counts. A jolt Devious Devices fires itself is told
      even when this mod's own shock roll declines that pass.
    - The equip gate reads a device's name from a shipped pair table
      (VRTE_DDZaZ_Pairs.ini, 1,217 Devious Devices pairs), so the rules that
      go by name apply when a device is put on BY HAND. A device from a
      plugin the table does not know is name-blind on the equip ask, as
      before. (The 1.2.4 note about "[PAIR] rendered->inventory map" is
      superseded: the line now reads "table: N pairs resolved".)
    - A knocked-out NPC who wakes wearing arm restraints gets her bound-arm
      pose back; Devious Devices only re-checks it on its own equips.
    - A device's name no longer goes blank for half a minute after Devious
      Devices re-asserts it.
    - The restraint block no longer says every restraint needs "the proper
      key"; the worn line says what actually opens it.
    - Only the Shocking Soulgem plugs and piercings discharge. The two Black
      Soulgem plugs and the three chaos / filled soulgem plugs carry shock
      keywords that Devious Devices never implemented; this mod used to
      discharge them on the keyword alone. A Black Soulgem plug is a very
      strong vibrator, nothing more.

  NEW IN 1.2.4  (the integration review's fix list)

    WHAT IS NOT SAID ANY MORE
      - An orgasm in a SexLab or OStim scene is no longer told as the devices'
        doing. A climax counts as the devices' only while one of them is
        actually running.
      - Nothing happens to an unconscious NPC - no shock, no vibration, no
        line - until she wakes. A knocked-out NPC in four shocking devices
        could be killed by them before.
      - Effects and device changes on someone in a scene, or in the fifteen
        seconds after one ends, are not narrated: the scene owns the moment.
        Plugs a scene took out and put back are no longer announced as a new
        insertion.
      - A vibration this mod started from a spell cast is told once, by the
        cast line - not again when it stops - and a short after-state never
        replaces a longer one still running.

    NAMES
      - Every worn device is named within about thirty seconds of loading a
        save, whatever its type. Harnesses, corsets, suits, yokes, cuffs,
        hobble skirts, mittens, clamps and pony gear used to stay nameless all
        session, which left their material, damp and wear lines blank.
      - A device's name no longer lingers after it comes off: a plain gag put
        on after a Scold's Bridle is a plain gag again.
      - The rules that go by NAME (the ClothingGate INI, tall collars, the
        Scold's Bridle, the collar with nipple clamps) now apply when a device
        is put on BY HAND, not only when it is taken off. The hand path always
        saw a nameless device before and fell back to the class default.
        (Look for "[PAIR] rendered->inventory map" in the log - a count above
        zero means the names are there.)
      - Something blocking a device is named - "the Iron Chastity Belt" - not
        called "the device locked over the hips".

    REMOVAL
      - Diary of Mine's Enchanted Slave Collar and Steel Slave Collar of
        Leashing cannot be removed: the enchantment holds them.
      - The four devices Devious Devices itself refuses to unlock (the Lively
        Rope Harness and the Strong Leather hobble dress, armbinder and
        blindfold) are refused here too, instead of an approval followed by
        "you don't have the key".

    SMALLER
      - The fourth-climax kneel plays only when she actually climaxed.
      - A jolt from DD's own shock is described by where it landed; a nipple
        piercing is no longer told as a plug knocking her down.
      - The player is never counted among the NPCs who saw an effect.
      - After quitting and relaunching, the "just released" countdown no
        longer stalls.
      - What a removed device left behind no longer survives a quick-load.
      - A Diary of Mine plug put in by hand paces VRTouchEvents' touch lines
        again (it did not, in 1.2.3).
      - The combat cast line no longer says she broke her stance.

  NEW IN 1.2.3

    WHAT AN ONLOOKER IS TOLD
      - Someone looking at her is told what can be SEEN, in a few words -
        "hooked through the nipples", "locked around the neck", "binding the
        arms behind the back" - and nothing she feels. It used to repeat her
        own description, aches and damp skin included.
      - Arm and leg cuffs read as bands on the arms and legs, not as something
        on the wrists and ankles.
      - A Scold's Bridle reads as a hinged iron cage around the head, not as
        something filling the mouth.
      - Pony tails show, including Devious Lore's three unnamed ones.

    DEVICES
      - Boots come in three grades, by name (section 6):
          socks and oil boots   the skin stays damp; nothing more
          leg boots             careful, unsteady steps, the pace not cut -
                                no longer counted as bound legs
          full boots            pony, restrictive, training and "(Tight)"
                                boots - still bound legs
      - Every real hood is a MID layer: nothing goes on over it.
      - The Extreme Hood: closed it is a blindfold - sight only. The (Open)
        version is a hood with holes for the eyes - she can see, while hearing
        and speech are muffled.
      - Pony harness gags sit on the mid layer, over the face and head.
      - Smooth Rope Shibari is a rope harness.
      - Rope neck, arm, leg and crotch binds are KNOTTED on, not locked - to
        her and to anyone watching ("knotted around the throat").
      - 23 restraint items that carry no framework keyword at all are now
        described to her and to onlookers: the Pama furniture set, several
        Deviously Accessible collars and corsets, the Loose Dibellan Rope
        Harness, and Dark Desires' Hand Cuffs Backside and Blindfold of Desire.
        Awareness only - no effects, no climax, no line when they go on or come
        off (Pama's furniture drives its own).
      - The Iron Collar with Chained Nipple Clamps (and its rusty twin) is a
        collar with a chain running to the nipples: collar chafing and a
        collar's after-state, not a clamp's.
      - The Iron Yoke (Fiddle) is a yoke with the hands held up in front.
      - ZaZ's rope shackles tied around the neck bind the wrists, not the legs.
      - Diary of Mine's Cuffs Rope (wrists and ankles) counts as bound wrists.
      - Submissive Arm and Leg Cuffs chafe instead of building joint pain.
      - Onlookers see cuffs that hold the hands behind the back, or together in
        front, as exactly that.
      - Diary of Mine yokes, armbinders, ankle and wrist restraints now count
        as restraints.
      - Hobble skirts, and Scribe's and Seamstress' gear, keep the skin damp
        like other partial gear.
      - Boots, gloves and hoods already worn when a save loads are named
        properly too.

    MOVED OR REMOVED
      - The player's masturbation and its onlookers belong to VRTouchEvents
        now. Nothing in this mod relays it.
      - Three gesture events this DLL used to rename for VRTouchEvents are
        gone - VRTouchEvents 3.3 hears Precision Physic Bodies directly.

  NEW IN 1.2.2  (the Database is awareness only)

    - Devices going on and coming off, plugs, and whoever saw it are narrated
      by DD SN AddOn now, on every runtime. This mod still notices each one
      and broadcasts it as a mod event.
    - What a restraint leaves behind is a block in the NPC's own prompt for
      three prompts - the full ache, then easing, then fading - instead of an
      event. An onlooker sees the outward signs for the first two.
    - The equip and removal rules Precision Physic Bodies asks about are
      handed over only while DD SN AddOn is installed.

  NEW IN 1.2.1  (from the first VR key test)

    VR ONLY, WITH PRECISION PHYSIC BODIES AND DD SN ADDON
      - A pull the gate refuses is no longer narrated as done. PPB announces
        a pull before it asks the gate, so a refused pull used to tell the NPC
        the device came off while it stayed on. The announcement now waits
        until Devious Devices has really unlocked the device, and names it
        properly ("Black Leather Ball Strap Gag", not "The Gag").
      - A key is spent the way DD's own menu spends it (section 13).
      - Bound wrists keep every device on, as in DD.
      - A device cannot go on where another device already holds its body
        slot - no second gag on a gagged mouth. It stays in your hand.

    EVERYWHERE
      - A mannequin never counts as a witness or an audience, and a blind NPC
        never mistakes one for a target.
      - Refusals are logged as well as approvals.

  NEW IN 1.2  (since 1.1-beta)

    ENFORCED
      - A gag now blocks spellcasting: every equipped spell is unequipped, at
        any delivery. A staff is kept - it needs no words.
      - A blindfold now takes staves as well as bows and crossbows.

    TOLD
      - A plain DD blindfold no longer reads as a hood. Some carry DD's hood
        keyword; the mod now also checks the mesh, so a blindfold blinds,
        leaves the ears open, and keeps its name in the worn list.
      - The restraint and blindness blocks now survive a cold SkyrimNet
        decorator cache. A bound NPC was sometimes described as entirely free.
      - Prompt order: worn devices first, then what they take away, then the
        recoveries, the gag last, and SkyrimNet's "this event just happened"
        line after all of them (section 12).
      - Each block states only its own restriction, in positive wording, and
        ends with the same line: a device holds as firmly after hours as in
        the first minute. The blocks used to contradict each other.

    EVENTS
      - Spellfire arousal is per device and cumulative (nipple +1, anal +2,
        clitoris +3, vaginal +3) and applies in combat, in public and in
        private. Tunable in [arousal]. The combat stagger is gone.

    VR ONLY, WITH PRECISION PHYSIC BODIES
      - PPB can now reach the layer gate. In 1.1 it never connected.
      - Taking a locked device off by hand needs the key DD assigns to that
        device, read per device - including devices already worn when the
        save loads, about 30 seconds after loading. Needs a PPB build with
        removal support.

    LOG
      - The first line now reads "VRTE DD/ZaZ Database v<version> loaded."

  !! WHAT HAS ACTUALLY BEEN PLAYED. The core has: the climax animation and the
     kneel, the shocks and their ragdoll, the device going off again at full
     arousal, blindfolded stumbles, the gag interrupt, the spoken reactions and
     the live state the prompts read were all seen working in a real session.
     The newest wiring - a removal reaching the prompt at the unequip, and the
     worn block's new position - is one session old and has not been played
     yet. Please send VRTE_DDZaZ.log (section 13) with any report, and say
     which runtime.

  !! The SE/AE build has never been launched on SE or AE. It is one runtime-
     universal DLL and the SE/AE paths are static analysis of the binary, not
     observation. VR is where it has actually run.

  !! The strain aftermath (section 8) is built and deployed but has not yet
     been seen firing in a real playthrough. It needs a device worn for hours
     of game time and then removed.

  Device counts are specific to the load order they were measured on. Class,
  slots, keywords and meshes are properties of the records themselves and
  travel fine; the totals in section 5 will differ on your setup.

  Everything this mod says about a device is composed from the records in YOUR
  load order at runtime. It ships no device data and patches no plugin.

===============================================================================
