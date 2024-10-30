REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Base: Constants and Equates"
    Rights: --{
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }--
    License: --{
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }--
    Note: --{
        This code is evaluated just after actions, natives, sysobj, and other lower
        levels definitions. This file intializes a minimal working environment
        that is used for the rest of the boot.
    }--
]

; NOTE: The system is not fully booted at this point, so only simple
; expressions are allowed. Anything else will crash the boot.

; Standard constants

zero: 0

; Char constants

nul: NUL:  #""  ; ^(NULL) no longer legal internal to strings
space:     #  ; more useful as space than as synonym for rarely-useful NUL
sp: SP:    space
backspace: #"^(BACK)"
bs: BS:    backspace
tab:       #"^-"
newline:   #"^/"
newpage:   #"^l"
slash:     #"/"
backslash: #"\"
escape:    #"^(ESC)"
cr: CR:    #"^M"
lf: LF:    newline

; Type predicates.  These are planned to all go away in favor of plain &
; decorations (e.g. &any-series?) when needed.  Use e.g. plain any-series?
; in function specs.

null?!: &null?
nothing?!: &nothing?
void?!: &void?
char?!: &char?
space?!: &space?
splice?!: &splice?
logic?!: &logic?
action?!: &action?

blank: _

null: ~null~
ok: okay: ~okay~
ok?: okay?/

void: ~void~
nothing: ~
trash: '~

; These should be aliases for things like system.ports.input and such, but
; for now just to make the syntax of things look better we define them.

stdin: @stdin
stdout: @stdout
stderr: @stderr
