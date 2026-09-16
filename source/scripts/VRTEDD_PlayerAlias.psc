Scriptname VRTEDD_PlayerAlias extends ReferenceAlias
; The load-reliability spine. ⚠ A Quest never receives OnPlayerLoadGame, so the
; controller's mod-event registrations would be silently lost on every save load
; without this alias calling Setup() again. Same pattern as VRTouch_PlayerAlias,
; and the reason report 07 exists.
;
; ★ The quest is resolved by FormID rather than through a filled Property. A
;   property has to be set inside the ESP and an UNFILLED one is a silent None -
;   it would disable this whole bridge with no error anywhere. This cannot be
;   left unfilled. (GetOwningQuest() would also do, but Caprica's compat-header
;   ReferenceAlias stub does not inherit it from Alias.) Same idiom the
;   Decorators script already uses to reach zadlibs.

; ⛔ THE FILENAME MOVED, 2026-09-03. The controller quest now lives in
; "DD SN Database.esp" - the DATABASE half of the split, which needs a quest
; only because Papyrus RegisterForModEvent needs a script instance and DD sends
; its climax and device-name events as FORMS, which a C++ sink cannot receive.
; ⚠ THIS LINE IS THE WHOLE BRIDGE. GetFormFromFile with a name that does not
; match returns None, and the block above already explains why that is the
; dangerous shape: Setup() is simply never called again after a load, every mod
; event registration is silently lost, and NOTHING logs an error. If the ESP is
; ever renamed again, this string is the first thing to change.
VRTEDD_Controller Function Ctrl()
    return Game.GetFormFromFile(0x000800, "DD SN Database.esp") as VRTEDD_Controller
EndFunction

Event OnInit()
    VRTEDD_Controller c = Ctrl()
    if c
        c.Setup()
    endif
EndEvent

Event OnPlayerLoadGame()
    VRTEDD_Controller c = Ctrl()
    if c
        c.Setup()
    endif
EndEvent
