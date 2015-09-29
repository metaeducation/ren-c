REBOL [
	System: "REBOL [R3] Language Interpreter and Run-time Environment"
	Title: "REBOL 3 Boot Base: Constants and Equates"
	Rights: {
		Copyright 2012 REBOL Technologies
		REBOL is a trademark of REBOL Technologies
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Note: {
		This code is evaluated just after actions, natives, sysobj, and other lower
		levels definitions. This file intializes a minimal working environment
		that is used for the rest of the boot.
	}
]

; NOTE: The system is not fully booted at this point, so only simple
; expressions are allowed. Anything else will crash the boot.

;-- Standard constants:
on:  true
off: false
yes: true
no:  false
zero: 0

;-- Special values:
REBOL: system
sys: system/contexts/sys
lib: system/contexts/lib

;-- Char constants:
null:      #"^(NULL)"
space:     #" "
sp:        space
backspace: #"^(BACK)"
bs:        backspace
tab:       #"^-"
newline:   #"^/"
newpage:   #"^l"
slash:     #"/"
backslash: #"\"
escape:    #"^(ESC)"
cr:        #"^M"
lf:        newline
crlf:      "^M^J"

;-- Function synonyms:
q: :quit
not: :not?
!: :not?
min: :minimum
max: :maximum
abs: :absolute
empty?: :tail?
---: :comment
bind?: :bound?

rebol.com: http://www.rebol.com

; GROUP! is a better name than PAREN! for many reasons.  It's a complete word,
; it's no more characters, it doesn't have the same first two letters as
; PATH! so it mentally and typographically hashes better from one of the two
; other array types, it describes the function of what it does in the
; evaluator (where "BLOCK! blocks evaluation of the contents, the GROUP!
; does normal evaluation but limits it to the group)...
;
; There are some ways to bridge compatibility between GROUP! and PAREN!, but
; since it's a big change the burden to bear in initial tests will be on
; GROUP!.  So the datatype stays PAREN! by default with a special switch to
; turn group on.  This will give an opportunity to try out the synonym
; mechanisms on the new idea before a radical change on the old one.

group?: :paren?
group!: :paren!
