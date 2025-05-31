Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "Boot Base: Constants and Equates"
    rights: --[
        Copyright 2012 REBOL Technologies
        Copyright 2012-2025 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    note: --[
        This code is evaluated just after natives, datatypes, and type
        constraint definitions are bootstrapped.

        This file intializes a minimal working environment that is used for
        the rest of the boot.

        The system is not fully booted at this point, so only simple
        expressions are allowed. Anything else will crash the boot.
    ]--
]

; Standard constants

zero: 0

; Char constants

space:     _  ; extremely popular single-character literal (rune!)
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

; Binary constants

nul: NUL:  #{00}  ; ^(NULL) no longer legal internal to strings

blank: ~()~
quasar: '~

null: ~null~
ok: okay: ~okay~
ok?: okay?/

noop: tripwire/  ; lack of a hyphen has wide precedent, e.g. jQuery.noop

void: lambda [] [~[]~]

; These should be aliases for things like system.ports.input and such, but
; for now just to make the syntax of things look better we define them.

stdin: @stdin
stdout: @stdout
stderr: @stderr
