REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Mezzanine: Series Helpers"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
]

empty?: func [
    {Returns TRUE if empty or NONE, or for series if index is at or beyond its tail.}
    series [any-series! object! gob! port! bitset! map! blank!]
][
    tail? series
]


; !!! Although this follows the -OF naming convention, it doesn't fit the
; pattern of a reflector as it takes two arguments.  Moreover, it is a bit
; sketchy...it doesn't check to see that the two series are the same, and
; if all it's doing is plain subtraction it seems like a poor primitive to
; be stuck with giving a name and suggested greater semantics to.  Review.
;
offset-of: func [
    "Returns the offset between two series positions."
    series1 [any-series!]
    series2 [any-series!]
][
    (index of series2) - (index of series1)
]


last?: single?: func [
    "Returns TRUE if the series length is 1."
    series [any-series! port! map! tuple! bitset! object! gob! any-word!]
][
    1 = length of series
]


extend: func [
    "Extend an object, map, or block type with word and value pair."
    return: [<opt> any-value!]
    obj [object! map! block! group!] {object to extend (modified)}
    word [any-word!]
    val [<opt> any-value!]
][
    append obj reduce [to-set-word word :val]
    :val
]


join-all: function [
    "Reduces and appends a block of values together."
    return: [<opt> any-series!]
        "Will be the type of the first non-void series produced by evaluation"
    block [block!]
        "Values to join together"
    <local> position
][
    forever [
        if tail? block [return ()]
        unless void? base: do/next block 'block [break]
    ]

    ; !!! It isn't especially compelling that  `join-of 3 "hello"` gives you
    ; `3hello`; defaulting to a string doesn't make obviously more sense than
    ; `[3 "hello"]` when using a series operation.  However, so long as
    ; JOIN-OF is willing to do so, it will be legal to do it here.
    ;
    join-of base block
]


remold: func [
    {Reduces and converts a value to a REBOL-readable string.}
    value {The value to reduce and mold}
    /only {For a block value, mold only its contents, no outer []}
    /all  {Mold in serialized format}
    /flat {No indentation}
][
    all_REMOLD: all
    all: :lib/all

    mold/(all [only 'only])/(all [all_REMOLD 'all])/(all [flat 'flat])
        reduce :value
]


array: func [
    "Makes and initializes a series of a given size."
    size [integer! block!] "Size or block of sizes for each dimension"
    /initial "Specify an initial value for all elements"
    value "Initial value (will be called each time if a function)"
    <local> block rest
][
    if block? size [
        if tail? rest: next size [rest: _]
        unless integer? size: first size [
            cause-error 'script 'expect-arg reduce ['array 'size type of :size]
        ]
    ]
    block: make block! size
    case [
        block? :rest [
            loop size [block: insert/only block array/initial rest :value]
        ]
        any-series? :value [
            loop size [block: insert/only block copy/deep value]
        ]
        function? :value [ ; So value can be a thunk :)
            loop size [block: insert/only block value] ; Called every time
        ]
    ] else [
        insert/dup block either initial [value][_] size
    ]
    head of block
]


replace: function [
    "Replaces a search value with the replace value within the target series."
    target  [any-series!] "Series to replace within (modified)"
    pattern "Value to be replaced (converted if necessary)"
    replacement "Value to replace with (called each time if a function)"

    ; !!! Note these refinments alias ALL, CASE, TAIL natives!
    /all "Replace all occurrences"
    /case "Case-sensitive replacement"
    /tail "Return target after the last replacement position"

    ; Consider adding an /any refinement to use find/any, once that works.
][
    all_REPLACE: all
    all: :lib/all
    case_REPLACE: case
    case: :lib/case
    tail_REPLACE: tail
    tail: :lib/tail

    save-target: target

    ; !!! These conversions being missing seems a problem with FIND the native
    ; as a holdover from pre-open-source Rebol when mezzanine development
    ; had no access to source (?).  Correct answer is likely to fix FIND:
    ;
    ;    >> find "abcdef" <cde>
    ;    >> == "cdef" ; should probably be NONE!
    ;
    ;    >> find "ab<cde>f" <cde>
    ;    == "cde>f" ; should be "<cde>f"
    ;
    ; Note that if a FORM actually happens inside of FIND, it could wind up
    ; happening repeatedly in the /ALL case if that happens.

    len: case [
        ; leave bitset patterns as-is regardless of target type, len = 1
        bitset? :pattern [1]

        any-string? target [
            unless string? :pattern [pattern: form :pattern]
            length of :pattern
        ]

        binary? target [
            ; Target is binary, pattern is not, make pattern a binary
            unless binary? :pattern [pattern: to-binary :pattern]
            length of :pattern
        ]

        any-block? :pattern [length of :pattern]
    ] else [1]

    while [pos: find/(all [case_REPLACE 'case]) target :pattern] [
        ; apply replacement if function, or drops pos if not
        ; the parens quarantine function invocation to maximum arity of 1
        (value: replacement pos)

        target: change/part pos :value len

        unless all_REPLACE [break]
    ]

    either tail_REPLACE [target] [save-target]
]


reword: function [
    "Make a string or binary based on a template and substitution values."

    source [any-string! binary!]
        "Template series with escape sequences"
    values [map! object! block!]
        "Keyword literals and value expressions"
    /case
        "Characters are case-sensitive"  ;!!! Note CASE is redefined in here!
    /escape
        "Choose your own escape char(s) or [prefix suffix] delimiters"
    delimiters [blank! char! any-string! word! binary! block!]
        {Default "$"}
        ; Note: since blank is being taken deliberately, it's not possible
        ; to use the defaulting feature, e.g. ()
    /into
        "Insert into a buffer instead (returns position after insert)"
    output [any-string! binary!]
        "The buffer series (modified)"

    <static>

    ; Note: this list should be the same as above with delimiters, with
    ; BLOCK! excluded.
    ;
    delimiter-types (
        make typeset! [blank! char! any-string! word! binary!]
    )
    keyword-types (
        make typeset! [blank! char! any-string! integer! word! binary!]
    )
][
    case_REWORD: case
    case: :lib/case

    output: default [make (type of source) length of source]

    prefix: _
    suffix: _
    case [
        unset? 'delimiters [
            prefix: "$"
        ]

        block? delimiters [
            unless parse delimiters [
                set prefix delimiter-types
                set suffix opt delimiter-types
            ][
                fail ["Invalid /ESCAPE delimiter block" delimiters]
            ]
        ]
    ] else [
        prefix: ensure delimiter-types delimiters
    ]

    ; MAKE MAP! will create a map with no duplicates from the input if it
    ; is a BLOCK!.  This might be better with stricter checking, in case
    ; later keys overwrite earlier ones and obscure the invalidity of the
    ; earlier keys (or perhaps MAKE MAP! itself should disallow duplicates)
    ;
    ; !!! To be used as keys, any series in the block will have to be LOCK'd.
    ; This could either be done with copies of the keys in the block, or
    ; locking them directly.  For now, the whole block is locked before the
    ; MAKE MAP! call.
    ;
    if block? values [
        values: make map! lock values
    ]

    ; The keyword matching rule is a series of [OR'd | clauses], where each
    ; clause has GROUP! code in it to remember which keyword matched, which
    ; it stores in this variable.  It's necessary to know the exact form of
    ; the matched keyword in order to look it up in the values MAP!, as trying
    ; to figure this out based on copying data out of the source series would
    ; need to do a lot of reverse-engineering of the types.
    ;
    keyword-match: _

    ; Note that the enclosing rule has to account for `prefix` and `suffix`,
    ; this just matches the keywords themselves, setting `match` if one did.
    ;
    any-keyword-rule: collect [
        for-each [keyword value] values [
            unless match keyword-types keyword [
                fail ["Invalid keyword type:" keyword]
            ]

            keep reduce [
                ; Rule for matching the keyword in the PARSE.  Although it
                ; is legal to search for BINARY! in ANY-STRING! and vice
                ; versa due to UTF-8 conversion, keywords can also be WORD!,
                ; and neither `parse "abc" [abc]` nor `parse "abc" ['abc]`
                ; will work...so the keyword must be string converted for
                ; the purposes of this rule.
                ;
                if match [integer! word!] keyword [
                    to-string keyword
                ] else [
                    keyword
                ]

                ; GROUP! execution code for remembering which keyword matched.
                ; We want the actual keyword as-is in the MAP! key, not any
                ; variation modified to
                ;
                ; Note also that getting to this point doesn't mean a full
                ; match necessarily happened, as the enclosing rule may have
                ; a `suffix` left to take into account.
                ;
                as group! compose [keyword-match: quote (keyword)]
            ]

            keep [
                |
            ]
        ]
        keep 'fail ;-- add failure if no match, instead of removing last |
    ]

    ; Note that `any-keyword-rule` will look something like:
    ;
    ; [
    ;     "keyword1" (keyword-match: quote keyword1)
    ;     | "keyword2" (keyword-match: quote keyword2)
    ;     | fail
    ; ]

    ; To be used in a parse rule, words must be turned into strings, though
    ; it would be nice if they didn't have to be, e.g.
    ;
    ;     parse "abc" [quote abc] => true
    ;
    ; Integers have to be converted also.
    ;
    if match [integer! word!] prefix [prefix: to-string prefix]
    if match [integer! word!] suffix [suffix: to-string suffix]

    rule: [
        ; Begin marking text to copy verbatim to output
        a:

        any [
            ; Seek to the prefix.  Note that the prefix may be BLANK!, in
            ; which case this is a no-op.
            ;
            to prefix

            ; End marking text to copy verbatim to output
            b:

            ; Consume the prefix (again, this could be a no-op, which means
            ; there's no guarantee we'll be at the start of a match for
            ; an `any-keyword-rule`
            ;
            prefix

            [
                [
                    any-keyword-rule suffix (
                        ;
                        ; Output any leading text before the prefix was seen
                        ;
                        output: insert/part output a b

                        v: select values keyword-match
                        output: insert output case [
                            function? :v [v :keyword-match]
                            block? :v [do :v]
                            true [:v]
                        ]
                    )

                    ; Restart mark of text to copy verbatim to output
                    a:
                ]
                    |
                ; Because we might not be at the head of an any-keyword rule
                ; failure to find a match at this point needs to SKIP to keep
                ; the ANY rule scanning forward.
                ;
                skip
            ]
        ]

        ; Seek to end, just so rule succeeds
        ;
        to end

        ; Finalize the output, such that any remainder is transferred verbatim
        ;
        (output: insert output a)
    ]

    unless parse/(all [case_REWORD 'case]) source rule [
        fail "Unexpected error in REWORD's parse rule, should not happen."
    ]

    ; Return end of output with /into, head otherwise
    ;
    either into [output] [head of output]
]


move: func [
    "Move a value or span of values in a series."
    source [any-series!] "Source series (modified)"
    offset [integer!] "Offset to move by, or index to move to"
    /part "Move part of a series"
    limit [integer!] "The length of the part to move"
    /skip "Treat the series as records of fixed size" ;; SKIP redefined
    size [integer!] "Size of each record"
    /to "Move to an index relative to the head of the series" ;; TO redefined
][
    unless limit [limit: 1]
    if skip [
        if 1 > size [cause-error 'script 'out-of-range size]
        offset: either to [offset - 1 * size + 1] [offset * size]
        limit: limit * size
    ]
    part: take/part source limit
    insert either to [at head of source offset] [
        lib/skip source offset
    ] part
]


extract: func [
    "Extracts a value from a series at regular intervals."
    series [any-series!]
    width [integer!] "Size of each entry (the skip)"
    /index "Extract from an offset position"
    pos "The position(s)" [any-number! logic! block!]
    /default "Use a default value instead of blank"
    value "The value to use (will be called each time if a function)"
    /into "Insert into a buffer instead (returns position after insert)"
    output [any-series!] "The buffer series (modified)"
    <local> len val default_EXTRACT
][  ; Default value is "" for any-string! output

    default_EXTRACT: default
    default: enfix :lib/default

    if zero? width [return any [output make series 0]]  ; To avoid an infinite loop
    len: either positive? width [  ; Length to preallocate
        divide (length of series) width  ; Forward loop, use length
    ][
        divide index of series negate width  ; Backward loop, use position
    ]
    unless index [pos: 1]
    if block? pos [
        parse pos [some [any-number! | logic!]] or [
            cause-error 'Script 'invalid-arg reduce [pos]
        ]
        output: default [make series len * length of pos]
        if not default_EXTRACT and (any-string? output) [value: copy ""]
        for-skip series width [for-next pos [
            val: pick series pos/1 else [value]
            output: insert/only output :val
        ]]
    ] else [
        output: default [make series len]
        if not default_EXTRACT and (any-string? output) [value: copy ""]
        for-skip series width [
            val: pick series pos else [value]
            output: insert/only output :val
        ]
    ]
    either into [output] [head of output]
]


alter: func [
    "Append value if not found, else remove it; returns true if added."

    series [any-series! port! bitset!] {(modified)}
    value
    /case
        "Case-sensitive comparison"
][
    case_ALTER: case
    case: :lib/case

    if bitset? series [
        return either find series :value [
            remove/part series :value false
        ][
            append series :value true
        ]
    ]
    either remove (find/(all [case_ALTER ['case]]) series :value) [
        append series :value
        true
    ][
        false
    ]
]


collect-with: func [
    "Evaluate body, and return block of values collected via keep function."

    return: [any-series!]
    'name [word! lit-word!]
        "Name to which keep function will be assigned (<local> if word!)"
    body [block!]
        "Block to evaluate"
    /into
        "Insert into a buffer instead (returns position after insert)"
    output [any-series!]
        "The buffer series (modified)"

    <local> keeper
][
    output: default [make block! 16]

    keeper: func [
        return: [<opt> any-value!]
        value [<opt> any-value!]
        /only
    ][
        output: insert/(all [only 'only]) output :value
        :value
    ]

    either word? name [
        ;
        ; A word `name` indicates that the body is not already bound to
        ; that word.  FUNC does binding and variable creation so let it
        ; do the work.
        ;
        eval func compose [(name) [function!] <with> return] body :keeper
    ][
        ; A lit-word `name` indicates that the word for the keeper already
        ; exists.  Set the variable and DO the body bound as-is.
        ;
        set name :keeper
        do body
    ]

    either into [output] [head of output]
]


; Classic version of COLLECT which assumes that the word you want to use
; is KEEP, and that the body needs to be deep copied and rebound (via FUNC)
; to a new variable to hold the keeping function.
;
collect: specialize :collect-with [name: 'keep]


format: function [
    "Format a string according to the format dialect."
    rules {A block in the format dialect. E.g. [10 -10 #"-" 4]}
    values
    /pad p
][
    p: default [#" "]
    unless block? :rules [rules: reduce [:rules]]
    unless block? :values [values: reduce [:values]]

    ; Compute size of output (for better mem usage):
    val: 0
    for-each rule rules [
        if word? :rule [rule: get rule]

        val: val + switch type of :rule [
            :integer! [abs rule]
            :string! [length of rule]
            :char! [1]
        ] else [0]
    ]

    out: make string! val
    insert/dup out p val

    ; Process each rule:
    for-each rule rules [
        if word? :rule [rule: get rule]

        switch type of :rule [
            :integer! [
                pad: rule
                val: form first+ values
                clear at val 1 + abs rule
                if negative? rule [
                    pad: rule + length of val
                    if negative? pad [out: skip out negate pad]
                    pad: length of val
                ]
                change out :val
                out: skip out pad ; spacing (remainder)
            ]
            :string!  [out: change out rule]
            :char!    [out: change out rule]
        ]
    ]

    ; Provided enough rules? If not, append rest:
    if not tail? values [append out values]
    head of out
]


printf: proc [
    "Formatted print."
    fmt "Format"
    val "Value or block of values"
][
    print format :fmt :val
]


split: function [
    "Split series in pieces: fixed/variable size, fixed number, or delimited"

    series [any-series!]
        "The series to split"
    dlm [block! integer! char! bitset! any-string!]
        "Split size, delimiter(s), or rule(s)."
    /into
        "If dlm is integer, split in n pieces rather than pieces of length n."
][
    either all [block? dlm | parse dlm [some integer!]] [
        map-each len dlm [
            either positive? len [
                copy/part series series: skip series len
            ][
                series: skip series negate len
                continue ;-- don't add to output
            ]
        ]
    ][
        size: dlm   ; alias for readability

        res: collect [
            parse series identity case [
                all [integer? size | into] [
                    if size < 1 [cause-error 'Script 'invalid-arg size]
                    count: size - 1
                    piece-size: (
                        to integer! round/down divide length of series size
                    )
                    if zero? piece-size [piece-size: 1]
                    [
                        count [copy series piece-size skip (keep/only series)]
                        copy series to end (keep/only series)
                    ]
                ]
                integer? dlm [
                    if size < 1 [cause-error 'Script 'invalid-arg size]
                    [any [copy series 1 size skip (keep/only series)]]
                ]
            ] else [
                ; !!! It appears from the tests that dlm is allowed to be a
                ; block, in which case it acts as a parse rule.  At least,
                ; there was a test that uses the feature.  This would not
                ; apply to parse rules that were all integers, e.g. [1 1 1],
                ; since those style blocks are handled by the other branch.
                ;
                assert [match [bitset! any-string! char! block!] dlm]
                [
                    any [mk1: some [mk2: dlm break | skip] (
                        keep/only copy/part mk1 mk2
                    )]
                ]
            ]
        ]

        ; Special processing, to handle cases where the spec'd more items in
        ; /into than the series contains (so we want to append empty items),
        ; or where the dlm was a char/string/charset and it was the last char
        ; (so we want to append an empty field that the above rule misses).
        ;
        fill-val: does [copy either any-block? series [[]] [""]]
        add-fill-val: does [append/only res fill-val]
        case [
            all [integer? size | into] [
                ;
                ; If the result is too short, i.e., less items than 'size, add
                ; empty items to fill it to 'size.
                ;
                ; We loop here as insert/dup doesn't copy the value inserted.
                ;
                if size > length of res [
                    loop (size - length of res) [add-fill-val]
                ]
            ]
            integer? dlm []
        ]
        else [
            assert [match [bitset! any-string! char! block!] dlm]

            ; If the last thing in the series is a delimiter, there is an
            ; implied empty field after it, which we add here.
            ;
            case [
                bitset? dlm [
                    ;
                    ; ATTEMPT is here because LAST will return void for an
                    ; empty series, and FIND of void is not allowed.
                    ;
                    if attempt [find dlm last series] [add-fill-val]
                ]

                char? dlm [
                    if dlm = last series [add-fill-val]
                ]

                string? dlm [
                    if all [
                        find series dlm
                        empty? find/last/tail series dlm
                    ] [add-fill-val]
                ]

                block? dlm [
                    ;-- nothing was here.
                ]
            ]
        ]


        res
    ]
]


find-all: function [
    "Find all occurrences of a value within a series (allows modification)."

    'series [word!]
        "Variable for block, string, or other series"
    value
    body [block!]
        "Evaluated for each occurrence"
][
    verify [any-series? orig: get series]
    while [any [
        | set series find get series :value
        | (set series orig | false) ;-- reset series and break loop
    ]][
        do body
        series: next series
    ]
]
