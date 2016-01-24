REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Root context"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0.
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        Root system values. This context is hand-made very early at boot time
        to allow it to hold key system values during boot up. Most of these
        are put here to prevent them from being garbage collected.
    }
    Note: "See Task Context for per-task globals"
]

system          ; system object
errobj          ; error object template
strings         ; low-level strings accessed via Boot_Strs[] (GC protection)
typesets        ; block of TYPESETs used by system; expandable
empty-block     ; a value that is an empty BLOCK!

;; Tags used in the native-optimized versions of user-function-generators
;; FUNC and CLOS

transparent-tag ; func w/o definitional return, ignores non-definitional ones
infix-tag       ; func is treated as "infix" (first parameter comes before it)
local-tag       ; marks the beginning of a list of "pure locals"
durable-tag     ; !!! In progress - argument word lookup survives call ending

;; Natives can usually be identified by their code pointers and addresses
;; (e.g. `VAL_FUNC_CODE(native) == &N_parse`) and know their own values via
;; D_FUNC when running.  However, RETURN is special because its code pointer
;; is overwritten so it must be recognized by its paramlist series.
;;
;; (PARSE just wants access to its D_FUNC more convenient from a nested call)

return-native
leave-native
parse-native

;; The BREAKPOINT instruction needs to be able to re-transmit a RESUME
;; instruction in the case that it wants to leapfrog another breakpoint
;; sandbox on the stack, and needs access to the resume native for the label
;; of the retransmitted throw.  It also might need to generate a QUIT
;; throw if the breakpoint hook signaled it.

resume-native
quit-native

;; The FUNC and PROC function generators are native code, and quick access
;; to a block of [RETURN:] or [LEAVE:] is useful to share across all of the
;; instances of functions like those created by DOES.  Having a filled
;; REBVAL of the word alone saves a call to Val_Init_Word_Unbound with
;; the symbol as well.

return-set-word
return-block
leave-set-word
leave-block

boot            ; boot block defined in boot.r (GC'd after boot is done)

