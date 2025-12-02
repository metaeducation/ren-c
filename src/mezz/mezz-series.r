Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Mezzanine: Series Helpers"
    rights: --[
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
]


(sys.util/register-codec
    'BE
    []
    ^void
    decode-integer/
    encode-integer/)

(sys.util/register-codec
    'LE
    []
    ^void
    decode-integer:LE/
    encode-integer:LE/)

(sys.util/register-codec
    'UTF-8
    []
    ^void
    decode-UTF-8/
    encode-UTF-8/)

(sys.util/register-codec
    'IEEE-754
    []
    ^void
    decode-IEEE-754/
    encode-IEEE-754/)


last?: single?: lambda [
    "Returns okay if the series length is 1."
    series [any-series? port! map! tuple! bitset! object! any-word?]
][
    1 = length of series
]


array: func [
    "Makes and initializes a block of a given size"

    return: [<null> block!]
    size "Size or block of sizes for each dimension"
        [<opt-out> integer! block!]
    :initial "Initial value (will be called each time if action)"
        [element? action!]
    {rest block}  ; rest: null
][
    rest: null
    initial: default ['~]  ; if not specified, array will be all quasars
    if block? size [
        rest: next size else [
            ;
            ; Might be reasonable to say `array:initial [] <x>` is `<x>` ?
            ;
            panic "Empty ARRAY dimensions (file issue if you want a meaning)"
        ]
        if not integer? size: size.1 [
            panic:blame [
                "Expect INTEGER! size in BLOCK!, not" type of size
            ] $size
        ]
        if tail? rest [rest: null]  ; want `array [2]` => `[~ ~]`, no recurse
    ]

    block: make block! size
    case [
        rest [
            repeat size [append block (array:initial rest ^initial)]
        ]
        action? ^initial [
            repeat size [append block initial]  ; Called every time
        ]
        any-series? initial [
            repeat size [append block (copy:deep initial)]
        ]
    ] else [
        append:dup block initial size
    ]
    return block
]


replace: func [
    "Replaces a search value with the replace value within the target series"

    return: [any-series?]
    target "Series to replace within (modified)"
        [any-series?]
    pattern "Value to be replaced (converted if necessary)"
        [<opt> element? splice!]
    ^replacement "Value to replace with (called each time if action)"
        [void? element? splice! action!]

    :one "Replace one (or zero) occurrences"
    :case "Case-sensitive replacement"  ; !!! Note this aliases CASE native!

    {^value pos tail}  ; !!! Aliases TAIL native (should use TAIL OF)
][
    if not pattern [return target]  ; could fall thru, but optimize...

    let case_REPLACE: case
    case: lib.case/

    pos: target

    while [[pos :tail]: find // [
        pos
        ^pattern
        case: case_REPLACE
    ]][
        if action? ^replacement [
            ;
            ; If arity-0 action, pos and tail discarded
            ; If arity-1 action, pos will be argument to replacement
            ; If arity-2 action, pos and tail will be passed
            ;
            ; They are passed as const so that the replacing function answers
            ; merely by providing the replacement.
            ;
            ^value: apply:relax ^replacement [const pos, const tail]
        ] else [
            ^value: ^replacement  ; might be void
        ]

        pos: change:part pos ^value tail

        if one [break]
    ]

    return target
]


;
; reword "$1 is $2." [1 "This" 2 "that"] => "This is that."
;
reword: func [
    "Make a string or binary based on a template and substitution values"

    return: [any-string? blob!]
    source "Template series with escape sequences"
        [any-string? blob!]
    values "Keyword literals and value expressions"
        [map! object! block!]
    :case "Characters are case-sensitive"
    :escape "Escape char(s) or [prefix suffix] delimiters (default is $)"
        [char? any-string? word! blob! block!]
]
bind construct [
    ;
    ; Note: this list should be the same as above with delimiters, with
    ; BLOCK! excluded.
    ;
    delimiter-types: [word! | blob! | any-string?/ | char?/]
    keyword-types: [integer! | word! | blob! | char?/ | any-string?/]
][
    let case_REWORD: case
    case: lib.case/

    let out: make (type of source) length of source

    let prefix: null
    let suffix: null
    case [
        null? escape [prefix: "$"]  ; refinement not used, so use default

        any [
            escape = ""
            escape = []
        ][
        ]

        block? escape [
            parse3 escape [
                prefix: delimiter-types
                [<end> | suffix: delimiter-types]
            ] except [
                panic ["Invalid /ESCAPE delimiter block" escape]
            ]
        ]
    ] else [
        prefix: escape
    ]

    ; To be used in a PARSE3 rule, words and integers must be turned to text
    ; to match in a string.  UPARSE does not require this:
    ;
    ;     >> parse "1 1 1" [some ['1 [space | <end>]]]
    ;     == 1
    ;
    if match [integer! word!] opt prefix [prefix: to-text prefix]
    if match [integer! word!] opt suffix [suffix: to-text suffix]

    ; TO MAP! will create a map with no duplicates from the input if it
    ; is a BLOCK! (though differing cases of the same key will be preserved).
    ; This might be better with stricter checking, in case later keys
    ; overwrite earlier ones and obscure the invalidity of the earlier keys
    ; (or perhaps MAKE MAP! itself should disallow duplicates)
    ;
    if block? values [
        values: to map! values
    ]

    ; We match strings generated from the keywords, but need to know what
    ; generated the strings to look them up in the map.  Hence we build a rule
    ; that will look something like:
    ;
    ; [
    ;     "keyword1" suffix (keyword-match: 'keyword1)
    ;     | "keyword2" suffix (keyword-match: 'keyword2)
    ;     | panic
    ; ]
    ;
    ; Note that the enclosing rule has to account for `prefix`, but `suffix`
    ; has to be part of this rule.  If it weren't, imagine if prefix is "$<"
    ; and suffix is ">" and you try to match "$<10>":
    ;
    ;    prefix [
    ;         "1" (keyword-match: '1)  ; ...this will take priority and match
    ;         | "10" (keyword-match: '10)
    ;    ] suffix  ; ...but then it's at "0>" and not ">", so it fails
    ;
    let keyword-match: null  ; variable that gets set by rule
    let any-keyword-suffix-rule: inside [] collect [
        for-each [keyword value] values [
            if error? parse reduce [keyword] keyword-types [
                panic ["Invalid keyword type:" keyword]
            ]

            keep spread compose2:deep '(<*>) [
                (<*> if match [integer! word!] keyword [
                    to-text keyword  ; `parse "a1" ['a '1]` illegal for now
                ] else [
                    keyword
                ])

                (<*> opt suffix)

                (keyword-match: '(<*> keyword))
            ]

            keep:line '|
        ]
        keep 'veto  ; add failure if no match, instead of removing last |
    ]

    prefix: default [[]]  ; default prefix to no-op rule

    let rule: [
        let a: <here>  ; Begin marking text to copy verbatim to output
        opt some [
            to prefix  ; seek to prefix (may be [], this could be a no-op)
            let b: <here>  ; End marking text to copy verbatim to output
            prefix  ; consume prefix (if [], may not be at start of match)
            [
                [
                    any-keyword-suffix-rule (
                        append:part out a ((index of b) - (index of a))

                        let v: apply select/ [
                            values keyword-match
                            case: case_REWORD
                        ]
                        append out switch:type v [
                            frame! [
                                apply:relax v [keyword-match]  ; arity-0 okay
                            ]
                            block! [eval v]
                        ] else [
                            v
                        ]
                    )
                    a: <here>  ; Restart mark of text to copy verbatim to output
                ]
                    |
                one  ; if wasn't at match, keep the ANY rule scanning ahead
            ]
        ]
        to <end>  ; Seek to end, just so rule succeeds
        (append out a)  ; finalize output - transfer any remainder verbatim
    ]

    apply parse3/ [source rule case: case_REWORD]  ; should succeed
    return out
]


move: func [
    "Move a value or span of values in a series"

    return: []  ; !!! Define return value?
    source "Source series (modified)"
        [any-series?]
    offset "Offset to move by, or index to move to"
        [integer!]
    :part "Move part of a series by length"
        [integer!]
    :skip "Treat the series as records of fixed size"
        [integer!]
    :to "Move to an index relative to the head of the series"
][
    part: default [1]
    if skip [
        if 1 > skip [cause-error 'script 'out-of-range skip]
        offset: either to [offset - 1 * skip + 1] [offset * skip]
        part: part * skip
    ]
    part: take:part source part
    insert either to [at head of source offset] [
        lib/skip source offset
    ] either any-list? source [spread part] [part]
    return ~
]


extract: func [
    "Extracts a value from a series at regular intervals"

    series [any-series?]
    width "Size of each entry (the skip), negative for backwards step"
        [integer!]
    :index "Extract from offset position"
        [integer!]
][
    if zero? width [return make (type of series) 0]  ; avoid an infinite loop

    let len: round either positive? width [  ; Length to preallocate
        divide (length of series) width  ; Forward loop, use length
    ][
        divide (index of series) negate width  ; Backward loop, use position
    ]

    index: default [1]
    let out: make (type of series) len
    iterate-skip @series width [
        append out opt (try pick series index)
    ]
    return out
]


alter: func [
    "Append value if not found, else remove it; returns true if added"

    return: [logic?]
    series [any-series? port! bitset!] "(modified)"
    value
    :case "Case-sensitive comparison"
][
    case_ALTER: case
    case: lib.case/

    if bitset? series [
        if find series value [
            remove:part series value
            return null
        ]
        append series value
        return okay
    ]
    if remove find // [series value case: case_ALTER] [
        append series value
        return okay
    ]
    return null
]


collect*: lambda [
    "Evaluate body, and return block of values collected via keep function"

    []: [
        block!
        <null> "if no KEEPs, prevent nulls with (keep ~()~)"
    ]
    body [<opt-out> block!]
    {out}
][
    let keep: specialize (  ; SPECIALIZE to hide series argument
        enclose append/ lambda [  ; Derive from APPEND for :LINE :DUP
            f [frame!]
        ][
            decay either void? f.value [  ; DECAY, we want pure null
                null  ; void in, null out (should it pass through the void?)
            ][
                f.series: out: default [make block! 16]  ; no null return now
                f.value  ; ELIDE leaves as result
                elide eval-free f  ; would invalidate f.value (hence ELIDE)
            ]
        ]
    )[
        series: <replaced>
    ]

    eval bind @keep body  ; discard result (should it be secondary return?)

    out
]


; Classic version of COLLECT which returns an empty block if nothing is
; collected, as opposed to the NULL that COLLECT* returns.
;
collect: redescribe [
    "Evaluate body, and return block of values collected via KEEP function"
] cascade [
    collect*/
    specialize else/ [branch: [copy []]]
]

format: func [
    "Format a string according to the format dialect."
    rules "A block in the format dialect. E.g. [10 -10 #- 4]"
    values
    :pad [char?]
][
    pad: default [space]

    rules: blockify rules
    values: blockify values

    ; Compute size of output (for better mem usage):
    let val: 0
    for-each 'rule rules [
        if word? rule [rule: get rule]

        val: me + switch:type rule [
            integer! [abs rule]
            text! [length of rule]
            char?/ [1]
        ] else [0]
    ]

    let out: make text! val
    insert:dup out pad val

    ; Process each rule:
    for-each 'rule rules [
        if word? rule [rule: get rule]

        switch:type rule [
            integer! [
                pad: rule  ; overwrite argument
                val: form first values
                values: my next
                if (abs rule) < length of val [
                    clear at val 1 + abs rule
                ]
                if negative? rule [
                    pad: rule + length of val
                    if negative? pad [out: skip out negate pad]
                    pad: length of val
                ]
                change out val
                out: skip out pad ; spacing (remainder)
            ]
            text! [out: change out rule]
            char?/ [out: change out rule]
        ]
    ]

    ; Provided enough rules? If not, append rest:
    if not tail? values [append out spread values]
    return head of out
]


printf: proc [
    "Formatted print."
    fmt "Format"
    val "Value or block of values"
][
    print format fmt val
]


split: func [
    "Split series in pieces: fixed/variable size, fixed number, or delimited"

    return: [<null> block!]
    series "The series to split"
        [<opt-out> any-series?]
    dlm "Split size, delimiter(s) (if all integer block), or block rule(s)"
        [
            <opt>  ; just return input
            block!  ; parse rule
            @block!  ; list of integers for piece lengths
            integer!  ; length of pieces (or number of pieces if /INTO)
            bitset!  ; set of characters to split by
            char? text!  ; text to split by
            quoted!  ; literally look for value
            splice!  ; split on a splice's literal contents
            quasiform!  ; alternate way to pass in splice or void
        ]
    :into "If dlm is integer, split in n pieces (vs. pieces of length n)"
][
    if not dlm [
        return reduce [series]
    ]

    if splice? dlm [
        panic "SPLIT on SPLICE?! would need UPARSE, currently based on PARSE3"
    ]

    if match [@block!] dlm [
        return map-each 'len dlm [
            if not integer? len [
                panic ["@BLOCK! in SPLIT must be all integers:" mold len]
            ]
            if len <= 0 [
                series: skip series negate len
                continue  ; don't add to output
            ]
            copy:part series series: skip series len
        ]
    ]

    let size  ; set for INTEGER! case
    let result: collect [parse3 series case [
        integer? dlm [
            size: dlm  ; alias for readability in integer case
            if size < 1 [panic "Bad SPLIT size given:" size]

            if into [
                let count: size - 1
                let piece-size: to integer! round:down (length of series) / size
                if zero? piece-size [piece-size: 1]

                [
                    repeat (count) [
                        series: across opt [repeat (piece-size) one] (
                            keep series
                        )
                    ]
                    series: across to <end> (keep series)
                ]
            ] else [
                [opt some [
                    series: across [one, repeat (size - 1) opt one] (
                        keep series
                    )
                ]]
            ]
        ]
        block? dlm [
            let mk1
            let mk2
            [
                opt some [not <end> [
                    mk1: <here>
                    opt some [mk2: <here>, [dlm | <end>] break | one]
                    (keep copy:part mk1 mk2)
                ]]
                <end>
            ]
        ]
        match [bitset! text! char?] dlm [  ; PARSE behavior (merge w/above?)
            let mk1
            [
                some [not <end> [
                    mk1: across [to dlm | to <end>]
                    (keep mk1)
                    opt thru dlm
                ]]
            ]
        ]
    ] else [
        assert [quoted? dlm]
        let mk1
        compose2:deep '(<*>) [
            some [not <end> [
                mk1: across [to (<*> dlm) | to <end>]
                (keep mk1)
                opt thru (<*> dlm)
            ]]
        ]
    ]]

    ; Special processing, to handle cases where the spec'd more items in
    ; :into than the series contains (so we want to append empty items),
    ; or where the dlm was a char/string/charset and it was the last char
    ; (so we want to append an empty field that the above rule misses).
    ;
    let fill-val: does [copy either any-list? series [[]] [""]]
    let add-fill-val: does [append result fill-val]
    if integer? dlm [
        if into [
            ; If the result is too short, i.e., less items than 'size, add
            ; empty items to fill it to 'size.  Loop here instead of using
            ; INSERT:DUP, because that wouldn't copy the value inserted.
            ;
            if size > length of result [
                repeat (size - length of result) [add-fill-val]
            ]
        ]
    ] else [
        ; If the last thing in the series is a delimiter, there is an
        ; implied empty field after it, which we add here.
        ;
        switch:type dlm [
            bitset! [boolean select dlm opt last series]
            char?/ [boolean dlm = last series]
            text! [
                boolean (find series dlm) and (
                    empty? [_ {_}]: find-last series dlm
                )
            ]
            quoted! [
                boolean (find series unquote dlm) and (
                    empty? [_ {_}]: find-last series unquote dlm
                )
            ]
            block! ['false]
        ] then fill -> [
            if true? fill [add-fill-val]
        ]
    ]

    return result
]


find-all: func [
    "Find all occurrences of a value within a series (allows modification)."

    return: []
    'series "Variable for block, string, or other series"
        [word!]
    value
    body "Evaluated for each occurrence"
        [block!]
][
    verify [any-series? orig: get series]
    while [any [
        set series find get series value
        (set series orig, null)  ; reset series and break loop
    ]][
        eval body
        series: next series
    ]
    return ~
]
