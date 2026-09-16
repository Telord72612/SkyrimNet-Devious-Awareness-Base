// ─────────────────────────────────────────────────────────────────────────────
// SkyrimNet decorators.
//
// Nothing in the load order ever told an NPC she was gagged — verified 08-23:
// zero decorators anywhere feed DD state to SkyrimNet, and no prompt component
// reads a zad_* keyword. SkyrimNet's own `get_worn_equipment` cannot substitute:
// it renders only inside dynamic_character_bio.prompt, never in the live
// dialogue prompt, and it reports item NAMES — which are empty for every DD
// rendered device (report 23 §6.2).
//
// The Papyrus side does the DD lookup and SkyrimNet registration; the wording
// lives in the shipped drop-in prompt so it can be tuned without a rebuild:
//   SKSE/Plugins/SkyrimNet/prompts/submodules/user_final_instructions/
//       0900_vrtedd_gag.prompt
// ─────────────────────────────────────────────────────────────────────────────
void RegisterDecorators()
{
    auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
    if (!vm) return;
    RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb;
    auto* args = RE::MakeFunctionArguments();
    vm->DispatchStaticCall("VRTE_DDZaZ_Decorators", "Register", args, cb);
    delete args;
    logger::info("[SKYRIMNET] dispatched VRTE_DDZaZ_Decorators.Register "
                 "(gag awareness; the script logs whether SkyrimNet accepted it)");
}