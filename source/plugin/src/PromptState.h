#pragma once
//
// ★★ THE PROMPT STATE FILE (1.3.0, 2026-09-14 — the user: "this is how we're going to do all our state
// and worn-device prompts from now on").
//
// WHY. Our 14 prompt blocks were Papyrus DECORATORS, and SkyrimNet refreshes its decorator pre-pass on
// its own cycle and returns EMPTY on a warm-up miss. Measured 2026-09-13: a plug went on at 22:42:18,
// the prompts at 22:42:18 and 22:42:22 still listed only the cuffs and the blindfold, the first prompt
// with the plug was 22:53:44 — and 10 of 14 decorators missed on one thoughts render. The LLM
// roleplayed a body it had not been told about.
//
// THE PATTERN (VRTouchEvents', VERIFIED LIVE 2026-09-13, report 45: 22 of 22 renders read the file,
// the reload 189 ms after the write, 0 served stale): the DLL writes ONE JSON file the instant state
// changes, atomically; the prompts read it with
//     read_json("VRTE_DDZaZ/prompt_state", random * 10000 + random * 100 + random)
// — the number is ignored by read_json but is part of SkyrimNet's callback-cache KEY, and `random` is
// never cached, so the read really runs on every render; read_json's own per-file cache then re-reads
// only when the file's mtime changed. The entry is matched on the SkyrimNet entity UUID = npc.UUID.
// The 14 decorators STAY registered: every prompt falls back to its decorator when the file has no
// entry for her (a cold start, a profile without this DLL), and third-party prompts keep working.
//
// File: Data/SKSE/Plugins/VRTE_DDZaZ/prompt_state.json
//   {"version":1,"npcs":[{"uuid":<u64>,"formId":<u32>,"name":"…",
//     "worn":"<the 0770 block>","worn_visible":"<the 0950 block>",
//     "gag":"…","blind":"…","arms":"…","legs":"…","all":"…","deaf":"…",   ("" = free)
//     "after":"…","after_visible":"…",                                     ("" = nothing left)
//     "ungag":"3|2|1|","unblind":…,"undeaf":…,"unbound_arms":…,"unbound_legs":…,"unbound_all":…}]}
// One entry per actor with something to say (tracked, an after-state, or a release countdown).
// Written ONLY when the text changed (the poll rebuilds it every 3 s and compares), and at once on
// an equip, a real removal, a pushed name and a load. Empty at kDataLoaded and every load boundary.
//
#include <cstdint>

namespace PromptState
{
    // kDataLoaded: resolve SkyrimNet's PublicFormIDToUUID (API v3+), write an empty file.
    void Install();

    // kPreLoadGame / kNewGame: nothing survives a load - the file goes back to empty.
    void Reset();

    // MAIN THREAD ONLY (it reads actors): rebuild every entry and write the file if it changed.
    void Refresh(const char* why);

    // Any thread: queue one Refresh on the main thread (coalesced - many requests, one rebuild).
    void RequestRefresh(const char* why);
}
