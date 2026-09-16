cmake_minimum_required(VERSION 3.21)

project(VRTE_DDZaZ LANGUAGES CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# VRTE DD/ZaZ AddOn — the AddOn's OWN SKSE plugin.
#
# ★ BOUNDARY: this is a standalone plugin shipping in the AddOn's own mod folder.
# It does NOT live inside VRTouchEvents and does not touch it. VRTouchEvents is
# its own agent's mod, exactly as PPB is. Both are consumed only through their
# published APIs.
#
# It needs nothing from VRTouchEvents at runtime: PPB supplies the capsule
# geometry and the coverage gate, HIGGS supplies the frame hook and the held
# object, and DD is driven through the engine. Zero VRTE dependencies.
# ---------------------------------------------------------------------------
set(COMMONLIB_PATH "D:/Games/My Skyrim/Tools/CommonLibVR-4.14.0" CACHE PATH "Path to CommonLibSSE-NG source")

# ★ ALL THREE RUNTIMES (2026-08-24). The GESTURES are VR-only by nature - they
# need PPB and HIGGS, neither of which exists on SE/AE - but the SkyrimNet
# PROMPT half is not: gag / blind / restraint awareness, the recovery
# countdowns, and the user's VRTE_DDZaZ_Restraints.ini are all plain worn-armor,
# keyword and spell reads that work identically on any runtime.
#
# Building VR-only was what made the prompt feature VR-only, and nothing else:
# the DLL both REGISTERS the decorators and ANSWERS them, so on SE/AE it never
# loaded, Register() was never dispatched, and every block stayed silent.
#
# The plugin declaration already says StructCompatibility::Independent +
# VersionIndependence::AddressLibrary, so one binary is correct for all three.
# On SE/AE the PPB and HIGGS handshakes simply fail and Install() logs one line
# and stays inert - the prompt path is dispatched from separate SKSE messages
# and does not depend on it.
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR ON CACHE BOOL "" FORCE)
set(BUILD_TESTS      OFF CACHE BOOL "" FORCE)

add_subdirectory("${COMMONLIB_PATH}" CommonLibSSE EXCLUDE_FROM_ALL)

add_library(VRTE_DDZaZ SHARED
    src/main.cpp
    src/DeviceEquip.cpp
    src/WornDevices.cpp
    src/PromptState.cpp
)

target_precompile_headers(VRTE_DDZaZ PRIVATE src/PCH.h)
# A linker map beside the DLL (build output only, never shipped): the crash logger prints
# "VRTE_DDZaZ.dll+RVA" frames and the Release build has no PDB, so the map is how those become
# function names. It decoded the 1.3.1 struggle CTD (2026-09-14). Keep a copy per shipped DLL.
target_link_options(VRTE_DDZaZ PRIVATE /MAP)
target_link_libraries(VRTE_DDZaZ PRIVATE CommonLibSSE::CommonLibSSE)

set_target_properties(VRTE_DDZaZ PROPERTIES
    OUTPUT_NAME "VRTE_DDZaZ"
    DEBUG_POSTFIX ""
)

# Deploy into the AddOn's OWN mod folder.
# ★ THE DLL IS THE DATABASE HALF. Split 2026-09-03 into DD SN Database (device
# knowledge, state, effects, narration, visibility - this DLL) and DD SN AddOn
# (the SkyrimNet actions + the PPB gesture bridge, which the DLL arms only when
# it sees DD SN AddOn.esp loaded). Deploying to the old single folder would put
# a stale DLL in the VFS alongside the real one.
set(MOD_OUTPUT "D:/Games/My Skyrim/mods/DD SN Database/SKSE/Plugins")
add_custom_command(TARGET VRTE_DDZaZ POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MOD_OUTPUT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:VRTE_DDZaZ>" "${MOD_OUTPUT}/"
    COMMENT "Copying VRTE_DDZaZ.dll to the DD SN Database mod folder"
)
