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
        "Will be the type of the first non-null series produced by evaluation"
    block [block!]
        "Values to join together"
    <local> position base
][
    until [
        block: (evaluate/set block 'base) else [return null]
        set? 'base
    ]

    ; !!! It isn't especially compelling that  `join-of 3 "hello"` gives you
    ; `3hello`; defaulting to a string doesn't make obviously more sense than
    ; `[3 "hello"]` when using a series operation.  However, so long as
    ; JOIN-OF is willing to do so, it will be legal to do it here.
    ;
    join-of base block
]


remold: redescribe [
    {Reduces and converts a value to a REBOL-readable string.}
](
    adapt 'mold [
        value: reduce :value
    ]
)

array: func [
    "Makes and initializes a series of a given size."
    size [integer! block!] "Size or block of sizes for each dimension"
    /initial "Specify an initial value for all elements"
    value "Initial value (will be called each time if a function)"
        [any-value!]
    <local> block rest
][
    if block? size [
        if tail? rest: next size [rest: _]
        if not integer? size: first size [
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
        action? :value [
            loop size [block: insert/only block value] ; Called every time
        ]
    ] else [
        insert/dup/only try get 'value size
    ]
    head of block
]


replace: function [
    {Replaces a search value with the replace value within the target series}

    target "Series to replace within (modified)"
        [any-series!]
    pattern "Value to be replaced (converted if necessary)"
        [<opt> any-value!]
    replacement "Value to replace with (called each time if a function)"
        [<opt> any-value!]

    ; !!! Note these refinments alias ALL, CASE, TAIL natives!
    /all "Replace all occurrences"
    /case "Case-sensitive replacement"
    /tail "Return target after the last replacement position"

    ; Consider adding an /any refinement to use find/any, once that works.
][
    if not set? 'pattern [return target]

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
    ;    >> == "cdef" ; should probably be null
    ;
    ;    >> find "ab<cde>f" <cde>
    ;    == "cde>f" ; should be "<cde>f"
    ;
    ; Note that if a FORM actually happens inside of FIND, it could wind up
    ; happening repeatedly in the /ALL case if that happens.

    len: 1 unless case [
        ; leave bitset patterns as-is regardless of target type, len = 1
        bitset? :pattern [1]

        any-string? target [
            if not text? :pattern [pattern: form :pattern]
            length of :pattern
        ]

        binary? target [
            ; Target is binary, pattern is not, make pattern a binary
            if not binary? :pattern [pattern: to-binary :pattern]
            length of :pattern
        ]

        any-array? :pattern [length of :pattern]
    ]

    while [pos: find/(try if case_REPLACE [/case]) target :pattern] [
        either action? :replacement [
            ;
            ; If arity-0 action, value gets replacement and pos discarded
            ; If arity-1 action, pos will be argument to replacement
            ; If arity > 1, end of block will cause an error
            ;
            value: replacement pos

            ; Note: ACTION! parameter couldn't be passed as enfix ("no such
            ; thing as enfix actions, just bindings").  So REPLACEMENT can't
            ; quote backwards and (for instance) fetch value...but it *could*
            ; quote pos and find out it's called `pos` (for instance).
        ][
            value: :replacement ;-- inert value, might be null
        ]

        target: change/part pos :value len

        if not all_REPLACE [break]
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

    out: make (type of source) length of source

    prefix: _
    suffix: _
    switch type of :delimiters [
        null [prefix: "$"]
        block! [
            parse delimiters [
                set prefix delimiter-types
                set suffix opt delimiter-types
                end
            ] or [
                fail ["Invalid /ESCAPE delimiter block" delimiters]
            ]
        ]
    ] else [
        prefix: ensure delimiter-types delimiters
    ]

    ; To be used in a parse rule, words must be turned into strings, though
    ; it would be nice if they didn't have to be, e.g.
    ;
    ;     parse "abc" [quote abc] => true
    ;
    ; Integers have to be converted also.
    ;
    if match [integer! word!] prefix [prefix: to-text prefix]
    if match [integer! word!] suffix [suffix: to-text suffix]

    ; MAKE MAP! will create a map with no duplicates from the input if it
    ; is a BLOCK! (though differing cases of the same key will be preserved).
    ; This might be better with stricter checking, in case later keys
    ; overwrite earlier ones and obscure the invalidity of the earlier keys
    ; (or perhaps MAKE MAP! itself should disallow duplicates)
    ;
    if block? values [
        values: make map! values
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
            if not match keyword-types keyword [
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
                    to-text keyword
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
                compose quote (keyword-match: quote (keyword))
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
                        append/part out a b

                        v: select/(try if case_REWORD [/case]) values keyword-match
                        append out case [
                            action? :v [v :keyword-match]
                            block? :v [do :v]
                            default [:v]
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
        (append out a)
    ]

    parse/(try if case_REWORD [/case]) source rule or [
        fail "Unexpected error in REWORD's parse rule, should not happen."
    ]

    out
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
    limit: default [1]
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
    <local> len val out default_EXTRACT
][  ; Default value is "" for any-string! output

    default_EXTRACT: default
    default: enfix :lib/default

    if zero? width [return make (type of series) 0]  ; avoid an infinite loop
    len: either positive? width [  ; Length to preallocate
        divide (length of series) width  ; Forward loop, use length
    ][
        divide index of series negate width  ; Backward loop, use position
    ]
    if not index [pos: 1]
    if block? pos [
        parse pos [some [any-number! | logic!] end] or [
            cause-error 'Script 'invalid-arg reduce [pos]
        ]
        out: make (type of series) len * length of pos
        if not default_EXTRACT and [any-string? out] [value: copy ""]
        iterate-skip series width [iterate pos [
            val: pick series pos/1 else [value]
            append/only out :val
        ]]
    ] else [
        out: make (type of series) len
        if not default_EXTRACT and [any-string? out] [value: copy ""]
        iterate-skip series width [
            val: pick series pos else [value]
            append/only out :val
        ]
    ]
    out
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
        if find series :value [
            remove/part series :value
            return false
        ]
        append series :value
        return true
    ]
    if remove (find/(try if case_ALTER [/case]) series :value) [
        append series :value
        return true
    ]
    return false
]


collect-with: func [
    "Evaluate body, and return block of values collected via keep function."

    return: "result block, or null if no KEEPs (prevent nulls with KEEP [])"
        [<opt> block!]
    'name [word! lit-word!]
        "Name to which keep function will be assigned (<local> if word!)"
    body [block!]
        "Block to evaluate"

    <local> out keeper
][
    out: null

    ;-- Derive from APPEND to inherit /ONLY, /LINE, /DUP automatically
    keeper: specialize (
        ;-- SPECIALIZE in order to remove series argument

        enclose 'append function [f [frame!] <with> out] [
            ;-- ENCLOSE (vs. ADAPT) in order to alter return result

            if not :f/value [return null] ;-- doesn't "count" as collected

            f/series: out: default [make block! 16] ;-- won't return null now
            :f/value ;-- ELIDE leaves as result (F/VALUE invalid after DO F)
            elide do f
        ]
    )[
        series: <replaced>
    ]

    either word? name [
        ;-- body not bound to word, use FUNC do binding work

        reeval func compose [(name) [action!] <with> return] body :keeper
    ][
        ;-- lit-word! means variable exists, just set it and DO body as-is

        set name :keeper
        do body
    ]

    :out
]


; Classic version of COLLECT which assumes that the word you want to use
; is KEEP, and that the body needs to be deep copied and rebound (via FUNC)
; to a new variable to hold the keeping function.
;
collect: specialize :collect-with [name: 'keep]

collect-lines: adapt 'collect [ ;; https://forum.rebol.info/t/945/1
    body: compose [
        keep: adapt specialize 'keep [
            line: true | only: false | part: false
        ] [value: spaced try :value]
        ((as group! body))
    ]
]

collect-block: chain [ ;; Gives empty block instead of null if no keeps
    :collect
        |
    specialize 'else [branch: [copy []]]
]

collect-text: chain [ ;; https://forum.rebol.info/t/945/2
     adapt 'collect [
         body: compose [
             keep: adapt specialize 'keep [
                 line: false | only: false | part: false
             ][
                 value: unspaced try :value
             ]
             ((as group! body))
         ]
     ]
        |
    :try
        |
    :spaced
        |
    specialize 'else [branch: [copy ""]]
]

format: function [
    "Format a string according to the format dialect."
    rules {A block in the format dialect. E.g. [10 -10 #"-" 4]}
    values
    /pad p
][
    p: default [space]
    if not block? :rules [rules: reduce [:rules]]
    if not block? :values [values: reduce [:values]]

    ; Compute size of output (for better mem usage):
    val: 0
    for-each rule rules [
        if word? :rule [rule: get rule]

        val: me + switch type of :rule [
            integer! [abs rule]
            text! [length of rule]
            char! [1]
            default [0]
        ]
    ]

    out: make text! val
    insert/dup out p val

    ; Process each rule:
    for-each rule rules [
        if word? :rule [rule: get rule]

        switch type of :rule [
            integer! [
                pad: rule
                val: form first values
                values: my next
                clear at val 1 + abs rule
                if negative? rule [
                    pad: rule + length of val
                    if negative? pad [out: skip out negate pad]
                    pad: length of val
                ]
                change out :val
                out: skip out pad ; spacing (remainder)
            ]
            text! [out: change out rule]
            char! [out: change out rule]
        ]
    ]

    ; Provided enough rules? If not, append rest:
    if not tail? values [append out values]
    head of out
]


printf: func [
    "Formatted print."
    return: <void>
    fmt "Format"
    val "Value or block of values"
][
    print format :fmt :val
]


split: function [
    {Split series in pieces: fixed/variable size, fixed number, or delimited}

    return: [block!]
    series "The series to split"
        [any-series!]
    dlm "Split size, delimiter(s) (if all integer block), or block rule(s)"
        [block! integer! char! bitset! text! tag!]
    /into "If dlm is integer, split in n pieces (vs. pieces of length n)"
][
    if block? dlm and [parse dlm [some integer! end]] [
        return map-each len dlm [
            if len <= 0 [
                series: skip series negate len
                continue ;-- don't add to output
            ]
            copy/part series series: skip series len
        ]
    ]

    if tag? dlm [dlm: form dlm] ;-- reserve other strings for future meanings

    result: collect-block [
        parse series <- if integer? dlm [
            size: dlm ;-- alias for readability in integer case
            if size < 1 [fail "Bad SPLIT size given:" size]

            if into [
                count: size - 1
                piece-size: to integer! round/down (length of series) / size
                if zero? piece-size [piece-size: 1]

                [
                    count [copy series piece-size skip (keep/only series)]
                    copy series to end (keep/only series)
                ]
            ] else [
                [any [copy series 1 size skip (keep/only series)] end]
            ]
        ] else [
            ; A block that is not all integers, e.g. not `[1 1 1]`, acts as a
            ; PARSE rule (see %split.test.reb)
            ;
            ensure [bitset! text! char! block!] dlm

            [
                any [mk1: some [mk2: dlm break | skip] (
                    keep/only copy/part mk1 mk2
                )]
                end
            ]
        ]
    ]

    ; Special processing, to handle cases where the spec'd more items in
    ; /into than the series contains (so we want to append empty items),
    ; or where the dlm was a char/string/charset and it was the last char
    ; (so we want to append an empty field that the above rule misses).
    ;
    fill-val: does [copy either any-array? series [[]] [""]]
    add-fill-val: does [append/only result fill-val]
    if integer? dlm [
        if into [
            ; If the result is too short, i.e., less items than 'size, add
            ; empty items to fill it to 'size.  Loop here instead of using
            ; INSERT/DUP, because that wouldn't copy the value inserted.
            ;
            if size > length of result [
                loop (size - length of result) [add-fill-val]
            ]
        ]
    ] else [
        ; If the last thing in the series is a delimiter, there is an
        ; implied empty field after it, which we add here.
        ;
        switch type of dlm [
            bitset! [did find dlm try last series]
            char! [dlm = last series]
            text! [
                (did find series dlm) and [empty? find/last/tail series dlm]
            ]
            block! [false]
        ] and [
            add-fill-val
        ]
    ]

    return result
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
