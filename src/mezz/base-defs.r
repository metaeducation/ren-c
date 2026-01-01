Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Boot Base: Other Definitions"
    rights: --[
        Copyright 2012 REBOL Technologies
        Copyright 2012-2019 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    description: --[
        This code is evaluated just after actions, natives, sysobj, and
        other lower level definitions. This file intializes a minimal working
        environment that is used for the rest of the boot.
    ]--
    notes: --[
        Any exported SET-WORD!s must be themselves "top level". This hampers
        procedural code here that would like to use tables to avoid repeating
        itself.  This means variadic approaches have to be used that quote
        SET-WORD!s living at the top level, inline after the function call.
    ]--
]

; Start with basic debugging

c-break-debug: c-debug-break/  ; easy to mix up

func: function/  ; historical and heavily-used abbreviation
proc: procedure/  ; long to write out, so abbreviate it

exit: specialize inline/ [code: [return ~]]

lib: system.contexts.lib  ; alias for faster access

; Note: OPTIONAL-VETO is an optimized intrinsic of the same functionality
; as OPTIONAL:VETO
;
?: opt: optional/  ; NULL -> VOID
??: cascade* reduce [unrun try/ unrun optional/]  ; NULL + ERROR! -> VOID
?!: optional-veto/  ; NULL -> VETO
??!: cascade* reduce [unrun try/ unrun optional-veto/]  ; NULL + ERROR! -> VETO

eval: evaluate/  ; shorthands should be synonyms, too confusing otherwise

probe: func [  ; note: do not want VANISHABLE, else `1 + 2 probe eval []` is 3
    "Debug print a molded value and returns that same value"

    return: [any-value?]
    ^value [any-value?]
][
    ; Remember this is early in the boot, so many things not defined.

    write stdout if antiform? ^value [
        unspaced [mold lift ^value _ _ "; anti"]  ; lifted is quasiform
    ] else [
        mold value
    ]
    write stdout newline

    return ^value
]

compose: specialize compose2/ [pattern: '()]  ; use template binding if not @()

branched?: then?/  ; alias, maybe more catchy?


; It's easier to pre-process CASCADE's block in usermode, which also offers a
; lower-level version CASCADE* that just takes a block of frames.
;
cascade: adapt cascade*/ [
    pipeline: reduce:predicate pipeline unrun/
]


; Alternative spellings.
;
; Question mark versions may seem of questionable use--who would say `NOT?`
; instead of just saying `NOT`--but maybe it would add some extra clarity that
; you were getting a logic back.  Also it would allow people to do things
; like redefine NOT locally and still have access to NOT? without having to
; say LIB.NOT ... maybe useful.  Just trying it out.

did: did?: to-logic/
didn't: didn't?: not/
not?: not/
both: both?: and?/
nor?: cascade [or?/ not/]
nand?: cascade [and?/ not/]
nor: infix cascade [or/ not/]
nand: infix cascade [and/ not/]


; ARITHMETIC OPERATORS
;
; Note that `/` is rather trickily not a PATH!, but a decayed form as a WORD!

+: infix add/
-: infix subtract/
*: infix multiply/
/: infix divide/


; SET OPERATORS

not+: bitwise-not/
and+: infix bitwise-and/
or+: infix bitwise-or/
xor+: infix bitwise-xor/
and-not+: infix bitwise-and-not/


; Equality variants (note: bootstrap needs to REDESCRIBE)

not-equal?: cascade [equal?/ not/] ; should optimize for intrinsics
lax-equal?: equal?:relax/
lax-not-equal?: cascade [lax-equal?/ not/]


; COMPARISON OPERATORS
;
; !!! See discussion about the future of comparison operators:
; https://forum.rebol.info/t/349

=: infix equal?/
<>: infix not-equal?/
!=: infix not-equal?/  ; http://www.rebol.net/r3blogs/0017.html

<: infix lesser?/
>: infix greater?/

; "Official" forms of the comparison operators.  This is what we would use
; if starting from scratch, and didn't have to deal with expectations people
; have coming from other languages: https://forum.rebol.info/t/349/
;
>=: infix greater-or-equal?/
=<: infix equal-or-lesser?/

; Compatibility Compromise: sacrifice what looks like left and right arrows
; for usage as comparison, even though the perfectly good `=<` winds up
; being unused as a result.  Compromise `=>` just to reinforce what is lost
; by not retraining: https://forum.rebol.info/t/349/11
;
equal-or-greater?: greater-or-equal?/
lesser-or-equal?: equal-or-lesser?/
=>: infix equal-or-greater?/
<=: infix lesser-or-equal?/

?=: infix lax-equal?/
?!=: infix lax-not-equal?/


elide-if-heavy-void: vanishable func [
    "Argument is evaluative, but discarded if heavy void (or void)"

    return: [any-value?]
    ^value "Evaluation product to be ignored"
        [any-value?]  ; ghost! is passed through
][
    if '~()~ = lift ^value [return ()]
    return ^value
]

; COMMA! is the new expression barrier.  But `||` is included as a way to
; make comma antiforms to show how to create custom barrier-like constructs.
;
||: func [] [return ()]

|||: func [
    "Inertly consumes all subsequent data, evaluating to previous result"

    return: [ghost!]
    'omit [element? <variadic>]
][
    insist [null? try take omit]
    return ()
]


; EACH will ultimately be a generator, but for now it acts as QUOTE as a
; quick and dirty container for the data, that things like MAP and FOR will
; recognize.  map x each [a b c] [...]` will give you x as a, then b, then c.
;
each: quote/


; REQUOTE is helpful when functions do not accept QUOTED! values.
;
requote: reframer func [
    "Remove Quoting Levels From First Argument and Re-Apply to Result"
    f [frame!]
    {num-quotes}
][
    num-quotes: quotes of (f.1 except [
        panic ["REQUOTE must have an argument to process"]
    ])

    f.1: noquote f.1

    return quote:depth opt (trap eval f) num-quotes
]

; https://forum.rebol.info/t/for-lightweight-lambda-arrow-functions/2172
;
->: infix lambda [
    @words "Names of arguments (will not be type checked)"
        [<end> _ word! 'word! ^word! refinement? block! group!]
    body "Code to execute (will not be deep copied)"
        [block!]
][
    case [
        not words [words: []]  ; x: -> [print "hi"] will take no arguments
        if group? words [words: eval words]
    ]
    arrow words body
]

; Particularly helpful for annotating when a branch result is used.
; https://forum.rebol.info/t/2165/
;
<-: identity/

; !!! NEXT and BACK seem somewhat "noun-like" and desirable to use as variable
; names, but are very entrenched in Rebol history.  Also, since they are
; specializations they don't fit easily into the NEXT OF SERIES model--this
; is a problem which hasn't been addressed.
;
next: specialize skip/ [offset: 1]
back: specialize skip/ [offset: -1]

; Function synonyms

min: minimum/
max: maximum/
abs: absolute/

delimit: lambda [delimiter line :head :tail] [
    join // [text! line with: delimiter head: head tail: tail]
]
unspaced: specialize delimit/ [delimiter: null]
spaced: specialize delimit/ [delimiter: space]
newlined: specialize delimit/ [delimiter: newline, tail: ok]

an: lambda [
    "Prepends the correct 'a' or 'an' to a string, based on leading character"
    value {s}
][
    if null? value [panic @value]
    head of insert (s: form value) either (find "aeiou" s.1) ["an "] ["a "]
]


empty?: lambda [
    "OKAY if none or void, if empty, or if index is at or beyond its tail"
    []: [logic?]
    container [
        <opt> none? any-series? any-sequence? object! port! bitset! map!
    ]
][
    any [
        null? container  ; e.g. input was void
        none? container
        0 = length of container  ; sequences always have > 0 length, not empty
    ]
]

; 1. The facility for auto-naming TRASH! only happens if spec has [return: ~]
;    Since this returns null in addition to trash, we do it ourselves.

print: func [
    "Output SPACED text with newline, return NULL if line has no output"

    return: [
        trash! "result invisible in the console when there was output"
        <null> "if the input was <opt-out>, e.g. void passed vs. empty string"
    ]
    line "Line of text or block, [] has NO output, CHAR! newline allowed"
        [<opt-out> char? text! block! @any-element?]
][
    if char? line [
        if line <> newline [
            panic "PRINT only allows CHAR! of newline (see WRITE-STDOUT)"
        ]
        write stdout line
        return ~#print~  ; [1]
    ]

    if pinned? line [
        line: unpin line
        line: either block? line [
            mold spread line  ; better than FORM-ing (what is FORM?)
        ] else [
            reduce [line]  ; in block, let SPACED handle molding logic
        ]
    ]

    write stdout (opt spaced line) then [
        write stdout newline
        return ~#print~  ; [1]
    ]

    return null
]

echo: proc [
    "Freeform output of text, with @WORD, @TU.P.LE, and @(GR O UP) as escapes"

    @args "If a BLOCK!, then just that block's contents--else to end of line"
        [element? <variadic>]
    {line}
][
    line: when block? first args [take args] else [
        collect [
            cycle [
                case [
                    tail? args [stop]
                    new-line? args [stop]
                    comma? first args [stop]
                ]
                keep take args
            ]
        ]
    ]
    write stdout form map-each 'item line [
        switch:type item [
            word?:pinned/
            group?:pinned/ [
                get:groups inside line item
            ]
        ] else [
            item
        ]
    ]
    write stdout newline
]
