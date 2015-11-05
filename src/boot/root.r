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

self			; (hand-built CONTEXT! value - but, has no WORD table!)
root			; the root context as a block (for GC protection)

system			; system object
errobj			; error object template
strings			; low-level strings accessed via Boot_Strs[] (GC protection)
typesets		; block of TYPESETs used by system; expandable
none-val		; a value of type NONE!
unset-val		; a value of type UNSET!
empty-block		; a value that is an empty BLOCK!
noname			; noname function word

;; Tags used in the native-optimized versions of user-function-generators
;; FUNC and CLOS

transparent-tag	; func w/o definitional return, ignores non-definitional ones
infix-tag		; func is treated as "infix" (first parameter comes before it)
local-tag		; marks the beginning of a list of "pure locals"

;; Natives needed as values by the system for throw names (they use their
;; own function values as names, to be agnostic about words)

parse-native
break-native
continue-native
quit-native
return-native
exit-native

;; The FUNC and CLOS function generators are native code, and quick access
;; to a block containing [RETURN:] is useful to share across all of the
;; instances of functions like those created by DOES.  Having a filled
;; REBVAL of the word alone saves a call to Val_Init_Word_Unbound with
;; the symbol as well.

return-set-word
return-block

boot			; boot block defined in boot.r (GC'd after boot is done)

