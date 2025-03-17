REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Base: Other Definitions"
    Rights: --{
        Copyright 2012 REBOL Technologies
        Copyright 2012-2019 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }--
    License: --{
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }--
    Description: --{
        This code is evaluated just after actions, natives, sysobj, and
        other lower level definitions. This file intializes a minimal working
        environment that is used for the rest of the boot.
    }--
    Note: --{
        Any exported SET-WORD!s must be themselves "top level". This hampers
        procedural code here that would like to use tables to avoid repeating
        itself.  This means variadic approaches have to be used that quote
        SET-WORD!s living at the top level, inline after the function call.
    }--
]

; Start with basic debugging

/c-break-debug: c-debug-break/  ; easy to mix up

/func: function/  ; historical and heavily-used abbreviation

lib: system.contexts.lib  ; alias for faster access

/?: maybe/  ; commonly used in Ren-C, more deserving of ? than HELP !!!

/eval: evaluate/  ; shorthands should be synonyms, too confusing otherwise

/probe: func [
    "Debug print a molded value and returns that same value"

    return: "Same as the input value"
        [any-atom?]
    ^value' [any-atom?]
][
    ; Remember this is early in the boot, so many things not defined.

    write-stdout case [
        value' = ^void ["; void"]
        quasi? value' [unspaced [mold value' space space "; anti"]]
    ] else [
        mold unmeta value'
    ]
    write-stdout newline

    return unmeta value'
]

/??: probe/  ; shorthand for debug sessions, not to be committed

; Pre-decaying specializations for THEN?, ELSE?, THEN, ELSE, ALSO
;
; https://forum.rebol.info/t/why-then-and-else-are-mutually-exclusive/1080/9
;
/then?*: then?:decay/
/else?*: else?:decay/
/*then: then:decay/
/*also: also:decay/
/*else: else:decay/
/branched?: then?/  ; alias, maybe more catchy?


; Logic synonyms DID and DIDN'T, sometimes look better

/did: to-logic/
/didn't: not/


; ARITHMETIC OPERATORS
;
; Note that `/` is rather trickily not a PATH!, but a decayed form as a WORD!

/+: infix add/
/-: infix subtract/
/*: infix multiply/
/: infix divide/


; SET OPERATORS

/not+: bitwise-not/
/and+: infix bitwise-and/
/or+: infix bitwise-or/
/xor+: infix bitwise-xor/
/and-not+: infix bitwise-and-not/


; It's easier to pre-process CASCADE's block in usermode, which also offers a
; lower-level version CASCADE* that just takes a block of frames.
;
/cascade: adapt cascade*/ [
    pipeline: reduce:predicate pipeline unrun/
]


; Equality variants (note: bootstrap needs to REDESCRIBE)

/not-equal?: cascade [equal?/ not/] ; should optimize for intrinsics
/strict-equal?: equal?:strict/
/strict-not-equal?: cascade [strict-equal?/ not/]


; COMPARISON OPERATORS
;
; !!! See discussion about the future of comparison operators:
; https://forum.rebol.info/t/349

=: infix equal?/
<>: infix not-equal?/
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
/equal-or-greater?: greater-or-equal?/
/lesser-or-equal?: equal-or-lesser?/
=>: infix equal-or-greater?/
<=: infix lesser-or-equal?/

!=: infix not-equal?/  ; http://www.rebol.net/r3blogs/0017.html
==: infix strict-equal?/  ; !!! https://forum.rebol.info/t/349
!==: infix strict-not-equal?/  ; !!! bad pairing, most would think !=

=?: infix same?/


; Common "Invisibles"

/comment: func [
    "Ignores the argument value, but does no evaluation (see also ELIDE)"

    return: "Evaluator will skip over the result (not seen)"
        [~[]~]
    @discarded "Literal value to be ignored."  ; `comment print "x"` disallowed
        [any-list? any-utf8? blob! any-scalar?]
][
    return ~[]~
]

/elide: func [
    "Argument is evaluative, but discarded (see also COMMENT)"

    return: "The evaluator will skip over the result (not seen)"
        [~[]~]
    ^discarded "Evaluated value to be ignored"
        [any-atom?]  ; e.g. (elide elide "x") is legal
][
    return ~[]~
]

/elide-if-void: func [
    "Argument is evaluative, but discarded if void"

    return: [any-value? pack?]
    ^value' "Evaluated value to be ignored"
        [any-value? pack?]  ; pack? is passed through
][
    if value' = ^void [return ~[]~]
    return unmeta value'
]

; COMMA! is the new expression barrier.  But `||` is included as a way to
; make comma antiforms to show how to create custom barrier-like constructs.
;
/||: func [] [return ~,~]

/|||: func [
    "Inertly consumes all subsequent data, evaluating to previous result"

    return: [~[]~]
    'omit [element? <variadic>]
][
    until [null? try take omit]
    return ~[]~
]


; EACH will ultimately be a generator, but for now it acts as QUOTE as a
; quick and dirty container for the data, that things like MAP and FOR will
; recognize.  map x each [a b c] [...]` will give you x as a, then b, then c.
;
each: quote/


; REQUOTE is helpful when functions do not accept QUOTED! values.
;
/requote: reframer lambda [
    "Remove Quoting Levels From First Argument and Re-Apply to Result"
    f [frame!]
    <local> p num-quotes result
][
    if not p: first words of f [
        fail ["REQUOTE must have an argument to process"]
    ]

    num-quotes: quotes of f.(p)

    f.(p): noquote f.(p)

    light (eval f then result -> [  ; !!! proper light-null handling here?
        quote:depth get:any $result num-quotes
    ] else [null])
]

; COMPOSE1 is the classic compose (may be renamed to COMPOSE*), which assumes
; you want to compose groups..and that the binding you want to use for the
; group is the binding of the passed-in list.
;
compose1: specialize (adapt compose/ [
    pattern: inside template pattern
]) [pattern: just ()]


; https://forum.rebol.info/t/for-lightweight-lambda-arrow-functions/2172
;
->: infix lambda [
    @words "Names of arguments (will not be type checked)"
        [<end> blank! word! lit-word? meta-word! refinement? block! group!]
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
/next: specialize skip/ [offset: 1]
/back: specialize skip/ [offset: -1]

; Function synonyms

/min: minimum/
/max: maximum/
/abs: absolute/

/delimit: lambda [delimiter line :head :tail] [
    join // [text! line :with delimiter :head head :tail tail]
]
/unspaced: specialize delimit/ [delimiter: null]
/spaced: specialize delimit/ [delimiter: space]
/newlined: specialize delimit/ [delimiter: newline, tail: ok]

/an: lambda [
    "Prepends the correct 'a' or 'an' to a string, based on leading character"
    value <local> s
][
    if null? value [fail @value]
    head of insert (s: form value) either (find "aeiou" s.1) ["an "] ["a "]
]


/empty?: func [
    "OKAY if blank or void, if empty, or if index is at or beyond its tail"
    return: [logic?]
    container [
        ~void~ blank! any-series? any-sequence? object! port! bitset! map!
    ]
][
    return any [
        void? container
        blank? container
        0 = length of container  ; sequences always have > 0 length, not empty
    ]
]


/print: func [
    "Output SPACED text with newline (evaluating elements if BLOCK!)"

    return: "Returns null if line outputs nothing, e.g. print [void]"
        [~ ~null~]
    line "Line of text or block, [] has NO output, CHAR! newline allowed"
        [<maybe> char? text! block! any-the-value?]
][
    if char? line [
        if line <> newline [
            fail "PRINT only allows CHAR! of newline (see WRITE-STDOUT)"
        ]
        write-stdout line
        return ~
    ]

    case [
        the-block? line [
            line: mold spread line  ; better than FORM-ing (what is FORM?)
        ]
        any-the-value? line [
            line: reduce [line]  ; in block, let SPACED handle molding logic
        ]
    ]

    write-stdout (maybe spaced line) then [
        write-stdout newline
    ] else [
        return null
    ]
]

/echo: func [
    "Freeform output of text, with @WORD, @TU.P.LE, and @(GR O UP) as escapes"

    return: [~]
    @args "If a BLOCK!, then just that block's contents--else to end of line"
        [element? <variadic>]
    <local> line
][
    line: if block? first args [take args] else [
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
    write-stdout form map-each 'item line [
        switch:type item [
            the-word!
            the-tuple!
            the-group! [
                get:groups inside line item
            ]
        ] else [
            item
        ]
    ]
    write-stdout newline
]


; Convenient alternatives for readability
;
neither?: nand?/
both?: and?/
