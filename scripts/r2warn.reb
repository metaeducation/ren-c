REBOL [
    System: "Rebol 3 (Ren-C Branch)"
    Title: "Warnings for Rebol2 users who are using Ren-C"
    Homepage: https://trello.com/b/l385BE7a/porting-guide
    Rights: {
        Copyright 2012-2018 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        These are warnings for Rebol2 (or Red) programmers using Ren-C.
        The goal is to notice things that are likely mistakes and offer more
        guidance than just a missing word or odd behavior.
    }
    Notes: {
        Ideally, operating in "warning" mode instead of emulation mode (or
        in addition to it) would be a feature of the %redbol.reb script.
        However, parameterizing modules is not something that has been
        implemented yet.
    }
]

export: lib/func [
    {!!! `export` should be a module feature !!!}
    set-word [set-word!]
] lib/in lib [
    print ["REGISTERING WARNING EXPORT:" as word! set-word]
]

checked: enfixed lib/func [
    return: <void>
    :set-word [set-word!]
    code [block!]
] lib/in lib [
    set set-word do in lib code
    export set-word
]

repurposed: deprecated: func [block [block!]] [
    block: copy block
    if url? first block [
        append block spaced [{See Also:} take block]
    ]
    append block {Emulation of the old meanings available via %redbol.reb}

    return func [] compose/only [
        fail 'return (delimit LF block)
    ]
]


any-function!: function!: any-function?: function?: deprecated [
    https://forum.rebol.info/t/596

    {ANY-FUNCTION! (and FUNCTION!) no longer exist, due to standardizing}
    {on one type of "invokable", which is ACTION!}
]

string!: string?: to-string: deprecated [
    https://forum.rebol.info/t/text-vs-string/612

    {ANY-WORD! types are being unified with ANY-STRING!, leaving string as}
    {the name for the category--not a type member.  TEXT! is the new name.}
]

paren!: paren?: to-paren: deprecated [
    https://trello.com/c/ANlT44nH

    {PAREN! has been renamed to GROUP!.}
]

number!: number?: scalar!: scalar?: series!: series?: deprecated [
    https://trello.com/c/d0Nw87kp

    {Don't use NUMBER!, SCALAR!, SERIES!.  Use ANY-NUMBER!, ANY-SERIES! etc.}
    {(typesets containing ANY- helps signal they are not concrete types)}
]

any-type!: deprecated [
    https://trello.com/c/1jTJXB0d

    {ANY-TYPE! is too much like ANY-DATATYPE!, ANY-VALUE! is the new typeset.}
    {However, it does not contain any "UNSET!", see `<opt>` in action specs.}
]

any-block!: any-block?: deprecated [
    https://trello.com/c/lCSdxtux

    {ANY-ARRAY! has replaced ANY-BLOCK!}
    {This is clearer than BLOCK! being a member name -and- a category name.}
]

any-object!: any-object?: deprecated [
    {ANY-CONTEXT! has replaced ANY-OBJECT!}
    {This is clearer than OBJECT! being a member name -and- a category name.}
]

unset?: repurposed [
    {UNSET? now tests if a WORD! or PATH! is bound to a set variable.}
    {Use NULL? to test for a value's absence, e.g. NULL? () would be true.}
]

value?: repurposed [
    {VALUE? is now a shorthand for ANY-VALUE?, a.k.a. NOT NULL?}
    {SET? is similar to the old meaning of VALUE?}
]

to-rebol-file: deprecated [
    {TO-REBOL-FILE is now LOCAL-TO-FILE}
    {Take note it only accepts TEXT! input and returns FILE!}
    {(unless you use LOCAL-TO-FILE*, which is a no-op on FILE!)}
]

to-local-file: deprecated [
    {TO-LOCAL-FILE is now FILE-TO-LOCAL}
    {Take note it only accepts FILE! input and returns TEXT!}
    {(unless you use FILE-TO-LOCAL*, which is a no-op on TEXT!)}
]

ajoin: deprecated [
    {AJOIN's functionality is replaced by UNSPACED}
]

reform: deprecated [
    {REFORM's functionality is replaced by SPACED}
]

unless: checked [
    enfixed func [
        {Returns left hand side, unless the right hand side is a value}

        return: [<opt> any-value!]
        left [<end> any-value!]
        :right [any-value! <variadic>]
        :look [any-value! <variadic>]
    ][
        right: take right
        if (unset? 'left) or [not group? right] or [block? first look] [
            fail 'look [
                "UNLESS has been repurposed in Ren-C as an infix operator"
                "which defaults to the left hand side, unless the right"
                "side has a value which overrides it.  You may use IF-NOT"
                "as a replacement, or even define UNLESS: :LIB/IF-NOT,"
                "though actions like OR, DEFAULT, etc. are usually better"
                "replacements for the intents that UNLESS was used for."
                "!!! NOTE: `if not` as two words isn't the same in Ren-C,"
                "as `if not x = y` is read as `if (not x) = y` since `=`"
                "completes its left hand side.  Be careful rewriting."
            ]
        ]

        (do as block! right) or [:left]
    ]
]

switch: checked [
    adapt 'switch [
        for-each c cases [
            lib/all [  ; SWITCH's /ALL would override
                match [word! path!] c
                not find [elide comment default] c
                'null <> c
                not datatype? get c
            ] then [
                fail 'cases [
                    {Temporarily disabled word/path SWITCH clause:} :c LF

                    {You may have meant to use a LIT-WORD! / LIT-PATH!} LF

                    {SWITCH in Ren-C evaluates its match clauses.  But to}
                    {help catch old uses, only datatype lookups enabled.}
                    {SWITCH: :LIB/SWITCH overrides.}
                ]
            ]
        ]
    ]
]

apply: checked [
    ;
    ; APPLY with refinements won't work the same way...should this hook apply
    ; in order to warn about it?
    ;
    :apply
]

bind?: deprecated [
    {BIND? has been replaced by `BINDING OF` (gives the context or NULL}
    {if no binding) and BOUND?--which now returns just LOGIC! and is}
    {equivalent to checking if the BINDING OF is <> NULL}
]

selfless?: deprecated [
    {SELFLESS? no longer has meaning (all frames are "selfless"), though a}
    {generator like CONSTRUCT may choose to establish a SELF member}
]

unset!: deprecated [
    https://trello.com/c/rmsTJueg

    {UNSET! is not a datatype in Ren-C, please read about NULL}
]

true?: deprecated [
    {Historical TRUE? is ambiguous.  Use DID, TO-LOGIC or `= TRUE`}
]

false?: deprecated [
    {Historical FALSE? is ambiguous.  Use NOT or `= FALSE`}
]

none?: none!: none: deprecated [
    {NONE is reserved in Ren-C for future use}
    {(It will act like NONE-OF, e.g. NONE [a b] => ALL [not a not b])}
    {_ is now a "BLANK! literal", with BLANK? test and BLANK the word.}
    {If running in <r3-legacy> mode, old NONE meaning is available.}
]

type?: deprecated [
    {TYPE? is reserved in Ren-C for future use}
    {(Though not fixed in stone, it may replace DATATYPE?)}
    {TYPE OF is the current replacement, with no TYPE-OF/WORD}
    {Use soft quotes, e.g. SWITCH TYPE OF 1 [:INTEGER! [...]]}
    {If running in <r3-legacy> mode, old TYPE? meaning is available.}
]

found?: deprecated [
    https://trello.com/c/Cz0qs5d7

    {FOUND? is deprecated, use DID (e.g. DID FIND)}
    {But it's not needed for IFs, just write IF FIND, it's shorter!}
]

op?: deprecated [
    https://trello.com/c/mfqTGmcv

    {OP? can't work in Ren-C because there are no "infix ACTION!s"}
    {See ENFIX? and SET/ENFIX for more information.}
]

also: checked [
    adapt 'also [
        all [
            block? :branch
            not semiquoted? 'branch

            fail 'branch [
                {ALSO serves a different purpose in Ren-C, so use ELIDE for}
                {old-ALSO-like tasks.}
                {See: https://trello.com/c/Y03HJTY4}
            ]
        ]
        ; fall through to normal ALSO implementation
    ]
]

compress: decompress: deprecated [
    https://trello.com/c/Bl6Znz0T

    {COMPRESS and DECOMPRESS are deprecated in Ren-C, in favor of the}
    {DEFLATE/INFLATE natives and GZIP/GUNZIP natives.}
]

clos: closure: deprecated [
    https://forum.rebol.info/t/234

    {All ACTION!s (such as made with FUNC, FUNCTION, METH, METHOD)}
    {have "specific binding", so closure is not needed for that.  The}
    {indefinite survival of args is on the back burner for Ren-C.}
]

exit: deprecated [
    https://trello.com/c/TXqLos1q

    {EXIT as an arity-1 form of RETURN was replaced in *definitional*}
    {returns by LEAVE, and is only available in PROC and PROCEDURE.}
]

try: checked [
    adapt 'try [
        ;
        ; Most historical usages of TRY took literal blocks as arguments.
        ; This is a good way of catching them, while allowing new usages.
        ;
        all [
            block? :optional
            semiquoted? 'optional

            fail 'optional [
                {TRY/EXCEPT was replaced by TRAP/WITH, matching CATCH/WITH}
                {and is more coherent.  See: https://trello.com/c/IbnfBaLI}
                {TRY now converts nulls to blanks, passing through ANY-VALUE!}
            ]
        ]
        ; fall through to native TRY implementation
    ]
]

foreach: deprecated [
    https://trello.com/c/cxvHGNha

    {FOREACH is not appropriately English, use FOR-EACH or EVERY}
]

forall: deprecated [
    https://trello.com/c/StCADPIB

    {FOR-NEXT is a clearer name for what FORALL did (see also FOR-BACK)}
]

forskip: deprecated [
    https://trello.com/c/StCADPIB

    {FORSKIP is now FOR-SKIP, see also FOR-NEXT and FOR-BACK}
]
