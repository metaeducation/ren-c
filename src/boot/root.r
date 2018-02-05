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
typesets        ; block of TYPESETs used by system; expandable
empty-block     ; a value that is an empty BLOCK!
empty-string    ; a value that is an empty STRING!

space-char      ; a value that is a space CHAR!
newline-char    ; a value that is a newline CHAR!

;; Tags used in the native-optimized versions of user-function-generators
;; FUNC and PROC

with-tag        ; <with> for no locals gather (disables RETURN/LEAVE in FUNC)
ellipsis-tag    ; FUNC+PROC use as alternative to [[]] to mark varargs
opt-tag         ; FUNC+PROC use as alternative to _ to mark optional void? args
end-tag         ; FUNC+PROC use as alternative to | to mark endable args
local-tag       ; marks the beginning of a list of "pure locals"
skip-tag        ; means parameter will be skipped if type doesn't match

;; !!! See notes on FUNCTION-META in %sysobj.r

function-meta

;; As an interim way of having a MAP! that a C hook can poke performance stats
;; into which is known to the garbage collector

stats-map
