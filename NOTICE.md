# Notice on dependencies

The [MIT licence](LICENSE) covers the source code, prompts and settings files in this repository
only. It does not cover the mods this one is built against.

The MIT licence above covers the source code, prompts and settings files in
this repository only.

Building the Papyrus scripts requires the script sources of Devious Devices and
SkyrimNet on the compiler's import path. Those are NOT included here and are NOT
covered by this licence; obtain them from those mods. The compile-time stubs used
in the author's own build are deliberately not redistributed for the same reason.

The prompt `submodules/character_bio/0410_equipment.prompt` is a keyword-filtered
FORK of SeverActions' version of that SkyrimNet file, kept so that worn devices
are not leaked into an onlooker's equipment list. It is a derived work governed by
that mod's own permissions, not by this licence. Its origin is recorded in the file.

`VRTE_DDZaZ_Pairs.ini` is generated. It contains only FormID and plugin-filename
REFERENCES to records belonging to Devious Devices - no content from that mod is
reproduced in it.

This mod ships no meshes, textures, animations or sounds, and patches no plugin.
Everything it says about a device is composed at runtime from the records present
in the user's own load order.
