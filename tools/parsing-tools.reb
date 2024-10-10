REBOL [
    Title: "Parsing tools"
    Type: module
    Name: Parsing-Tools
    Rights: --{
        Rebol is Copyright 1997-2015 REBOL Technologies
        REBOL is a trademark of REBOL Technologies

        Ren-C is Copyright 2015-2018 MetaEducation
    }--
    License: --{
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }--
    Author: "@codebybrett"
    Version: 2.100.0
    Needs: 2.100.100
    Purpose: --{
        These are some common routines used to assist parsing tasks.
    }--
]

if not find (words of import/) 'into [  ; See %import-shim.r
    do <import-shim.r>
]

import <bootstrap-shim.r>

; PARSING-AT is @codebybrett's invention which enables a concept that is a bit
; like combinators, from prior to combinators existing:
;
;   https://forum.rebol.info/t/what-were-apropos-and-parsing-at-about/1820
;
; It has caused no shortage of pain to maintain it in the bootstrap, but it
; is a worthy exercise.
;
; 1. The user passes in code that may use words INSTRUCTION and POSITION (and
;    in fact, the WORD parameter for the input position is often POSITION!)
;    Hence binding has to be careful here.
;
; 2. The /END feature is not used by bootstrap.
;
; 3. The passed in code we received should be already bound in the caller's
;    context.  But we want to make a variable visible to it that PARSING-AT
;    is able to set.  The user gave us the name, but not a variable...so we
;    have to create one.  To be compatible with bootstrap, we need a context
;    to "overbind" the variable to.
;
export /parsing-at: func [
    "Make rule that evaluates a block for next input position, fails otherwise"

    return: "PARSE rule block"
        [block!]
    var "Word set to input position (will be local)"
        [word!]
    code "Code to evaluate. Should Return next input position, or null"  ; [1]
        [block!]
    :end "Drop the default tail check (allows evaluation at the tail)"  ; [2]
    <local> code-group obj
][
    use [instruction position] [  ; vars need to outlive this function call
        obj: make object! compose [(setify var) ~]  ; make variable [3]
        code: overbind obj code  ; make variable visible to code
        var: has obj var

        if end [
            code-group: as group! code
        ] else [
            code-group: as group! compose [
                either not tail? (var) (code) [null]
            ]
        ]

        code-group: as group! compose [
            instruction: either position: (code-group) [
                [seek position]
            ][
                [bypass]
            ]
        ]

        return compose [(setify var) <here>, (code-group), instruction]
    ]
]
