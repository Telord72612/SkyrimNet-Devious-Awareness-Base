# SkyrimNet Devious Awareness Base

An awareness layer that tells [SkyrimNet](https://www.nexusmods.com/skyrimspecialedition/mods/153634)'s
LLM what restraint gear an NPC is actually wearing, what it is doing to her body, and what anyone else
can see of it. It covers **1,221 Devious Devices records and 222 from ZaZ Animation Pack**, plus Diary
of Mine, sorted into 37 device classes — state, effects, narration and the prompt blocks the model reads.

**Skyrim SE / AE / VR — one DLL.**

> Adult mod. It describes bondage gear on adult NPCs in clinical language. Nothing here is a mesh, a
> texture or an animation; it is text and code.

---

## What it does

SkyrimNet gives NPCs a language model to think with. It does not know what a device is — so an NPC in
an armbinder and a ball gag will happily hand you an item and chat about the weather, because nothing
ever told the model her mouth is strapped shut and her arms are behind her back.

This is the missing half:

| | |
|---|---|
| **what it IS** | a padded leather armbinder, buckled and locked |
| **what it DOES** | her arms are held behind her, elbows drawn together |
| **how long** | four hours, and her shoulders have gone from ache to a deep joint pain |
| **what STATE** | she cannot use her hands at all |
| **what is VISIBLE** | to her, everything; to a passer-by, only what shows |

It never tells the model how to *feel* about any of it. It reports the body and leaves the character to
SkyrimNet, which knows her personality and history — and this mod does not.

She also answers her own body: a climax, a shock or a blindfolded stumble is a spoken reaction, and
putting a gag or a hood on whoever is **currently speaking** cuts her off mid-word.

## Why "Base"

It is the knowledge layer, not an interaction mod. It adds no quest, no menu, no hotkey, and **no way of
putting a device on anyone**. What it does is keep an accurate, live description of every device in your
game and hand that to SkyrimNet — and expose all of it for other mods to build on:

- a **mod event** for every device change, climax, vibration, shock and stumble
- **Papyrus natives** for what someone is wearing, whether a device may go on, and who is talking
- a **C++ interface** other SKSE plugins can query for the clothing/layer gate
- a **live state file** (`prompt_state.json`) the prompts read, instead of one-answer-at-a-time decorators

It replaces no Devious Devices behaviour: the vibrations, shocks, moans and stumbles you see are DD's
own, called at the moment this mod's model says they belong.

## Install

Download a package from [Releases](../../releases), install with Mod Organizer 2 or Vortex, and **tick
`DD SN Database.esp`** — a new mod folder often lands unchecked, and without the plugin you lose device
names and climax detection.

Pick the package that matches your SkyrimNet:

| package | for |
|---|---|
| `…_SkyrimNet-Beta24.zip` | SkyrimNet 0.24.x — loose `prompts/` layout |
| `…_SkyrimNet-Beta25.zip` | SkyrimNet 0.25+ — the external content-layer layout |

**Requirements:** SKSE, Address Library, SkyrimNet, Devious Devices (Assets + Integration + Expansion),
Devious Devices NG, PapyrusUtil. ZaZ Animation Pack and Diary of Mine are covered automatically if
present. Full detail, settings and troubleshooting are in [`ReadMe.txt`](ReadMe.txt).

> The mod folder and the plugin keep their original `DD SN Database` names on purpose, so existing saves
> and load orders are unaffected by the rename.

## What is in this repository

```
ReadMe.txt              the full manual - requirements, every system, settings, troubleshooting
mod/                    the files that install into the game
  SKSE/Plugins/*.ini      the settings files (see below)
  SKSE/Plugins/SkyrimNet/ the prompt blocks, in both SkyrimNet layouts
installer/fomod/        the FOMOD installer definition
source/plugin/          the SKSE plugin (C++23, CommonLibSSE-NG)
source/scripts/         the Papyrus sources
source/esp/             the ESP as Spriggit YAML (one quest, one hidden faction)
```

The compiled DLL and the ESP are not in the tree — they are on the
[release page](../../releases), built from exactly this source.

### Settings

| file | what you edit |
|---|---|
| `VRTE_DDZaZ_Scales.ini` | the model's numbers: how fast strain climbs per device tier, how far it goes, cooldowns, chances, arousal, what a device leaves behind |
| `VRTE_DDZaZ_Restraints.ini` | teach it gear or spells from other mods — six categories: gag, blind, arms, legs, all, deaf |
| `VRTE_DDZaZ_ClothingGate.ini` | which layer a device counts as, when you disagree with the automatic answer |
| `VRTE_DDZaZ_Pairs.ini` | **generated data, not a settings file** — the Devious Devices pair table |

Each explains its own syntax at the top, every value is range-checked, and every rejected line is named
in `Documents/My Games/Skyrim VR/SKSE/VRTE_DDZaZ.log`.

## Building

The plugin needs [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG) and vcpkg. Point
`COMMONLIB_PATH` and `MOD_OUTPUT` in `source/plugin/CMakeLists.txt` at your own paths — they default to
the author's.

The Papyrus scripts compile against Devious Devices' and SkyrimNet's own sources; you need those mods'
`Scripts/Source` on the import path. Compile-time stubs are deliberately not redistributed here.

## Credits

Built on **SkyrimNet** by Sever, **Devious Devices**, **Devious Devices NG**, **ZaZ Animation Pack** and
**Diary of Mine**. `0410_equipment.prompt` is a keyword-filtered fork of SeverActions' version of that
file, kept so devices are not leaked into an onlooker's equipment list; its origin is noted in the file.

No device data is shipped and no plugin is patched — everything this mod says about a device is composed
at runtime from the records in **your** load order.
