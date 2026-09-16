# SkyrimNet Devious Awareness Base

An awareness layer that tells [SkyrimNet](https://www.nexusmods.com/skyrimspecialedition/mods/153634)'s
LLM what restraint gear an NPC is actually wearing, what it is doing to her body, and what anyone else
can see of it. **1,221 Devious Devices records, 222 from ZaZ Animation Pack**, plus Diary of Mine, in 37
device classes.

**Skyrim SE / AE / VR — one DLL.**

> Adult mod. It describes bondage gear on adult NPCs in clinical language. No meshes, no textures, no
> animations — text and code.

---

## What it does

SkyrimNet gives NPCs a language model to think with. It does not know what a device is, so an NPC in an
armbinder and a ball gag will hand you an item and chat about the weather. This is the missing half:

| | |
|---|---|
| **what it IS** | a padded leather armbinder, buckled and locked |
| **what it DOES** | her arms are held behind her, elbows drawn together |
| **how long** | four hours, and her shoulders have gone from ache to deep joint pain |
| **what STATE** | she cannot use her hands at all |
| **what is VISIBLE** | to her, everything; to a passer-by, only what shows |

It never tells the model how to *feel* about it. It reports the body and leaves the character to
SkyrimNet, which knows her personality and history — and this mod does not.

She also answers her own body: a climax, a shock or a blindfolded stumble is a spoken reaction, and a gag
or hood going on **whoever is currently speaking** cuts her off mid-word.

## Why "Base"

It is the knowledge layer, not an interaction mod. No quest, no menu, no hotkey, and **no way of putting a
device on anyone**. It keeps an accurate live description of every device in your game, hands it to
SkyrimNet, and exposes all of it to build on: a **mod event** for every device change, climax, vibration,
shock and stumble; **Papyrus natives** for what someone is wearing, whether a device may go on, and who is
talking; a **C++ interface** for the layer gate; and a **live state file** the prompts read.

It replaces no Devious Devices behaviour — the vibrations, shocks, moans and stumbles are DD's own, called
when this model says they belong.

## Requirements

**Required:** SKSE · Address Library · SkyrimNet · Devious Devices (Assets + Integration + Expansion) ·
Devious Devices NG · PapyrusUtil.

DD is required even if your gear comes from elsewhere — this mod calls DD's own code for vibration, shock,
moans and stumbles. DD NG drives the climax animation and brings Open Animation Replacer with it.

**Covered automatically if present:** ZaZ Animation Pack · Diary of Mine · Devious Lore · Deviously
Accessible · Laura's Bondage Shop. **Not required:** any VR mod — this half is flatscreen-safe.

## Install

Download from [Releases](../../releases) and install with a mod manager. Pick the package matching your
SkyrimNet: `…Beta24.zip` for 0.24.x (loose `prompts/`), `…Beta25.zip` for 0.25+ (external content layer).
Same mod either way; only where the prompts go differs.

1. **Tick `DD SN Database.esp`.** The step people miss — a new mod folder often lands unchecked, and
   without the plugin you lose device names and climax detection.
2. Load order does not matter. It overrides nothing.
3. **Updating: choose REPLACE, not merge.** Prompt files have been renumbered; a merge leaves the old copy
   beside the new one and renders the same block twice.

Wear-time data lives in the SKSE co-save and survives updates. Uninstalling leaves nothing running — the
clock is in the DLL, not in scripts attached to your NPCs.

> The mod folder and plugin keep their original `DD SN Database` names, so existing saves are unaffected by
> the rename.

If something looks wrong, the log is at `Documents/My Games/<your Skyrim>/SKSE/VRTE_DDZaZ.log`; it names
every rejected INI line and every decision. Please include it in any report, and say which runtime.

## What is covered

37 classes, each with its own vocabulary — what it is called, what it does to the body, where it sits.

**Restraint:** armbinders, elbow binders, elbow ties, straitjackets, yokes, arm and leg cuffs, ankle
shackles, bondage mittens, pet suits, pony gear · **Gags:** ball, large, bit, ring, panel, inflatable, each
with its own mouth mechanics · **Sensory:** blindfolds, hoods · **Worn:** collars, corsets, harnesses,
belts, bras, suits, gloves, boots, hobble skirts · **Internal:** plugs and piercings.

**Material** is read from the name — soulgem grades, chaos, stalhrim, ebonite, latex, leather, iron, gold
and the rest — and decides both the wording and whether a device can react to magic at all. A device with no
recognised class still gets its name, material, slots and visibility.

## State — the six things a device can do

Chosen by what the gear does to the body, never by what it is called:

| | |
|---|---|
| **gag** | speech blocked — only muffled sound |
| **blind** | sight blocked |
| **arms** | arms or hands unusable — cannot hold, take, give or fight |
| **legs** | walking cut to a shuffle |
| **all** | cannot act at all — shown instead of arms and legs |
| **deaf** | hearing dulled — she gets tone and volume, not words |

When the gear comes off, the opposite block runs for the next three replies and then stops, because a model
told for an hour that it cannot speak keeps acting gagged out of habit.

### Told versus enforced — the most important distinction here

Almost all of it is **told**, not enforced: the fact goes in front of the LLM and the LLM roleplays it.
Mechanically muting an NPC breaks quests, vendors and follower commands, and cannot be undone by someone who
should be able to spit a gag out. Instruction degrades gracefully; enforcement does not.

Exactly two things are **enforced**, because the LLM's choice cannot reach the engine:

- **Blind** → bows, crossbows and staves are unequipped; aimed spells are unequipped (she keeps healing and
  buffs); she really stumbles, and in combat swings at the nearest body rather than the right one.
- **Gag** → every equipped spell is unequipped. No words, no spell. She keeps her sword.

Both self-heal in each direction: if her AI re-equips the bow, the next tick strips it again, and the moment
the gear comes off it stops. A **staff is taken by a blindfold, not by a gag** — a staff needs no words, so
it is the one way a silenced caster keeps casting.

Boots count as bound legs only at full grade (pony, restrictive, training, any "(Tight)"). Plain decorative
wrist cuffs are deliberately not treated as binding. **Deaf** is hoods only — there is no hearing keyword
anywhere in DD or ZaZ, and a hood is the only thing over the ears.

## Effect and scale — what wearing something does over time

The longer it is worn, the more the body has to say. The clock is per device, per actor, in **game hours**,
and survives saves, loads and cell changes. What accumulates depends on the device: pressure and chafing
(cuffs, collars), joint ache (binders, yokes), jaw strain (gags), damp and heat (sealed suits, latex),
ischemia (clamps — in **minutes**), arousal (plugs, piercings).

Every device climbs the same five-step ladder, at boundaries of **1 / 3 / 6 / 12 game hours**:

**1** silent · **2** a pressed mark, tenderness · **3** congestion, ache, stiffness · **4** deep joint ache,
movement genuinely short · **5** nerve block.

> **Phase 5 is the quietest step, not the loudest.** The limb has stopped reporting: the body says *less*
> while the harm is greatest. So capping a device at 4 is not "gentler" — it pins it at the peak of
> complaint forever and removes its only quiet ending. To make a device milder, slow it down instead.

Each device sits in a tier that scales those boundaries and caps the climb. Higher multiplier = slower:

| tier | speed | ceiling | | tier | speed | ceiling |
|---|---|---|---|---|---|---|
| arms | 1.0 | 5 | | gagOpen / gagFill | 0.5 | 4 |
| limb | 1.5 | 5 | | clamp | **0.08** | 5 |
| strap | **6.0** | **2** | | mitt | 1.0 | 5 |
| yoke | 1.25 | 4 | | damp | 2.0 | 4 |
| rope | 2.0 | 4 | | dampSealed | 1.0 | 5 |
| suit | 1.0 | 5 | | boots / heels (per class) | — | 3 / 2 |

`strap` stopping at 2 is deliberate: an ordinary cuff or collar chafes and stops there, however long it is
worn. `clamp` at 0.08 is not a typo — nipple clamps run in minutes and get *quieter* as sensation fails.

**The aftermath.** Taking a device off does not end it. The phase reached at removal decides what the body
keeps reporting, as a block lasting three replies — full, easing, fading:

| from phase | for | what |
|---|---|---|
| 2 | 1 h | a pressed mark and tenderness |
| 3 | 2 h | congestion clearing, ache and stiffness |
| 4 | 4 h | deep joint ache, range still short |
| 5 | 0.5 h then 6 h | feeling flooding back, then weakness and numb patches |

Removal is the loudest moment in the model precisely because wearing was the quietest. Only the reperfusion
figure is anchored to a source; the rest is an argued shape, which is why it lives in an INI you can
disagree with. Cuffs and collars leave nothing below 24 hours worn. Strain winds down about an hour per tier
once a device is off, so putting the same kind back on soon resumes partway up — an evening's collar
refastened is not a fresh neck. A bystander gets separate wording: a limb shaken out, never the internal
ache.

## Events — what her body does, and what she says about it

Three of these she answers **out loud**, and if she was mid-sentence it is cut first: a **climax**, a
**shock**, and a blindfolded **stumble**. Everything else is background she knows and can refer to without
announcing. Only ever her own line is cut — if somebody else is talking, they finish.

- **Climax** — from Devious Devices' own orgasm event, not guessed from arousal. Counted per actor: the
  first three she rides out on her feet; from the fourth she goes down onto her knees — **but only if her
  arms are free**, since a woman strapped into an armbinder cannot catch herself. The pose is held for
  exactly as long as the animation really plays, and nothing else may fire at her while it does.
- **Vibration** — an edge, not a dice roll: a two-minute vibration is one event, not one per poll. Counts
  piercings, which DD's own NPC loop does not.
- **At full arousal the device goes off again** — if a vibration runs out with no finish, it starts again a
  few seconds later, until she finishes or is left wanting. The chain is told as one start and one stop.
- **Shock** — scales with how many shocking devices she wears, gated on arousal. A plug takes the strength
  out of her legs and she stays down a few seconds; piercings drop her to her knees briefly.
- **Spellfire** — a magicka-reactive device set off by her own casting. Only devices whose material scores
  on the soulgem ladder can do it; an iron plug never will. Arousal from it is **per device and cumulative**
  by site, in combat and in public as much as alone.
- **Stumbling** — a blinded NPC misjudges the ground while actually moving: 5% walking, 25% running, 50% on
  stairs, with a cooldown after a fall.
- **Struggling** — with Better NPC Support's escape system on, her attempt is reported and its result spoken:
  what came off, how (unlocked, worked off, taken off), and what stayed on. A long-worn restraint coming off
  puts her on one knee for a moment.
- **Devices going on and off** — this mod notices every one and broadcasts it; the spoken line belongs to
  whoever caused it. A removal reaches her prompt the moment the device leaves her body.

## Layers — how gear sits on the body

**The layer system restricts nothing.** It is an awareness model answering "how is this sitting on her, and
what can be seen of it". You can equip anything through a menu, container, script or console, in any order.
(The one real restriction is the VR hand gesture in the separate AddOn, which asks this mod for an answer.)

**Internal** plugs and internal piercings · **Under** against the skin with clothes over it — suits,
harnesses, belts, blindfolds, smaller gags · **Mid** where clothing would sit — corsets, boots, mittens,
binders, posture collars, and every hood · **Outer** over everything including armour — yokes, shackles,
ordinary collars.

An under or internal device beneath clothing is described to the **wearer** in full and to an onlooker not
at all. An outer device is described to both.

## Settings

Four INI files in `SKSE/Plugins/`, each documenting its own syntax at the top. Every value is range-checked
and every rejected line is named in the log.

| file | what you edit |
|---|---|
| `VRTE_DDZaZ_Scales.ini` | the model's numbers — speed and ceiling per tier, cooldowns, chances, arousal, aftermath. `[pace] global` is one dial over all of it |
| `VRTE_DDZaZ_Restraints.ini` | teach it gear or spells from other mods, in the six state categories |
| `VRTE_DDZaZ_ClothingGate.ini` | which layer a device counts as, when you disagree |
| `VRTE_DDZaZ_Pairs.ini` | **generated data, not settings** — the Devious Devices pair table |

## The Base and the AddOn are two mods

This one is device knowledge and telling SkyrimNet about it; nothing in it is VR-specific. **DD SN AddOn**
(separate download) is our integration of it with our own mods — the VR hand gestures through Precision
Physic Bodies, the VRTouchEvents tie-in, and the SkyrimNet actions an NPC can choose. The Base does not need
the AddOn; the AddOn needs the Base. The split is drawn there so the useful half carries no VR dependency.

## For developers

```
source/plugin      the SKSE plugin (C++23, CommonLibSSE-NG)
source/scripts     the Papyrus sources
source/esp         the ESP as Spriggit YAML
mod/               the prompts and INIs, as installed
installer/fomod    the FOMOD definition
```

The compiled DLL and ESP are not in the tree — they are on the [release page](../../releases), built from
this source. Point `COMMONLIB_PATH` and `MOD_OUTPUT` in `source/plugin/CMakeLists.txt` at your own paths.
The Papyrus compiles against Devious Devices' and SkyrimNet's own sources; compile-time stubs are
deliberately not redistributed.

## Licence and credits

[MIT](LICENSE) — use it, change it, build on it, ship it. That is the point of a Base. It covers this
repository's own code, prompts and settings; see [`NOTICE.md`](NOTICE.md) for what it does not.

Built on **SkyrimNet** by Sever, **Devious Devices**, **Devious Devices NG**, **ZaZ Animation Pack** and
**Diary of Mine**. `0410_equipment.prompt` is a keyword-filtered fork of SeverActions' version of that file,
kept so devices are not leaked into an onlooker's equipment list; its origin is noted in the file.

Thanks to Winds for providing the base information for pain state and scale.

No device data is shipped and no plugin is patched — everything this mod says about a device is composed at
runtime from the records in **your** load order.

**Honestly:** the core has been played — the climax animation and kneel, the shocks, the device going off
again, blindfolded stumbles, the gag interrupt, the spoken reactions and the live state file all ran in a
real session. The newest wiring is a session old, and the SE/AE build has never been launched on SE or AE:
its runtime independence is established by analysis of the binary, not by a boot.
