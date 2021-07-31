; %uparse.test.reb
;
; UPARSE for (U)sermode PARSE is a new architecture for PARSE based on the
; concept of parser combinators.  The goal is to put together a more coherent
; and modular design in usermode, which can then be extended or altered by
; using a different set of combinators.  Those combinators can be chosen for
; new features, or just to get compatibility with Rebol2/R3-Alpha/Red parse.


; Unless they are "invisible" (like ELIDE), rules return values.  If the
; rule's purpose is not explicitly to generate new series content (like a
; COLLECT) then it tries to return something very cheap...e.g. a value it
; has on hand, like the rule or the match.  This can actually be useful.
[
    (
        x: null
        did all [
            uparse? "a" [x: "a"]
            "a" = x
        ]
    )(
        x: null
        did all [
            uparse? "aaa" [x: some "a"]
            "a" = x  ; SOME doesn't want to be "expensive" on average
        ]
    )(
        x: null
        did all [
            uparse? "aaa" [x: [some "a" | some "b"]]
            "a" = x  ; demonstrates use of the result (which alternate taken)
        ]
    )
]


; GROUP! are value-bearing rules that do not advance the input and get their
; argument literally from a DO evaluation.  They always succeed.
[(
    10 = uparse [aaa] ['aaa (10)]
)(
    null = uparse [aaa] [(10)]
)(
    three: 3
    did all [
        uparse? "" [x: (three)]
        x = 3
    ]
)(
    did all [
        uparse? "" [x: (1 + 2)]
        x = 3
    ]
)]


; SOME and WHILE have become value-bearing; giving back blocks if they match.
;
; Note: currently a WHILE that never matches leads to errors since it is a
; ~void~ isotope in that case (succeeds but reified invisible).  UPARSE is
; not distinguishing at the moment between reified invisibility and total
; invisbility, so that issue has to be tackled before this test works:
;
;     x: ~
;     did all [
;         uparse? "aaa" [x: while "b", while "a"]
;         x = null
;     ]
;
[(
    x: ~
    did all [
        uparse? "aaa" [x: while "a"]
        x = "a"
    ]
)(
    x: ~
    did all [
        uparse? "aaa" [x: opt some "b", while "a"]
        x = null
    ]
)(
    x: ~
    did all [
        uparse? "aaa" [x: opt some "a"]
        x = "a"
    ]
)]


; A TEXT! rule will capture the actual match in a block.  But for a string, it
; will capture the *rule*.
[(
    rule: [x: "a"]
    did all [
        uparse? "a" rule
        same? x second rule
    ]
)(
    data: ["a"]
    rule: [x: "a"]
    did all [
        uparse? data rule
        same? x first data
    ]
)]


; One key feature of UPARSE is that rule chaining is done in such a way that
; it delegates the recognition to the parse engine, meaning that rules do not
; have to be put into blocks as often.
[
    ("a" = uparse ["aaaa"] [into text! some 2 "a"])
    (null = uparse ["aaaa"] [into text! some 3 "a"])
]


; BETWEEN is a new combinator that lets you capture between rules.
[(
    did all [
        uparse? "aaaa(((How cool is this?))aaaa" [
            some "a", x: between some "(" some ")", some "a"
        ]
        x = "How cool is this?"
    ]
)(
    did all [
        uparse? [<a> <b> * * * {Thing!} * * <c>] [
            some tag!, x: between 3 '* 2 '*, some tag!
        ]
        x = [{Thing!}]
    ]
)]


; SET-WORD! rules that do not match should not disrupt the variable, but if
; OPT is used with it then that indicates it should be set to NULL.
[(
    t: "t"
    i: "i"
    did all [
        uparse? [<foo>] [i: integer! | t: tag!]
        i = "i"  ; undisturbed
        t = <foo>
    ]
)(
    t: "t"
    i: "i"
    did all [
        uparse? [<foo>] [i: opt integer!, t: tag!]
        i = null
        t = <foo>
    ]
)]


; If a DATATYPE! is used in a text or binary rule, that is interpreted as a
; desire to TRANSCODE the input.
;
; !!! This feature needs more definition, e.g. to be able to transcode things
; that don't end with space or end of input.  For instance, think about how
; to handle the below rule if it was `1020****` and having a `some "*"` rule
; at the tail as well.
(
    did all [
        uparse? "***{A String} 1020" [some "*", t: text!, i: integer!]
        t = {A String}
        i = 1020
    ]
)


; ACROSS is UPARSE's version of historical PARSE COPY.
; !!! It is likely that this will change back, due to making peace with the
; ambiguity that COPY represents as a term, given all the other ambiguities.
[
    (did all [
        uparse? "aaabbb" [x: across some "a", y: across [some "b"]]
        x = "aaa"
        y = "bbb"
    ])
]


; META-BLOCK! runs a rule, but with "literalizing" result semantics.  If it
; was invisible, it gives ~void~.  This helps write higher level tools
; that might want to know about invisibility status.
[
    (did all  [
        then? uparse "" [synthesized: ^[]]  ; NULL-2 result (success)
        '~void~ = synthesized
    ])
    (did all  [
        then? uparse "" [synthesized: ^[('~void~)]]  ; not-NULL result
        (the '~void~) = synthesized  ; friendly if user made it friendly
    ])
    (did all  [
        then? uparse "" [synthesized: ^[(~void~)]]  ; not-NULL result
        '~void~ = synthesized  ; user didn't quote it, so suggests unfriendly
    ])
    ((the '~friendly~) = ^(uparse [~friendly~] [bad-word!]))
]


; HERE follows Topaz precedent as the new means of capturing positions
; (e.g. POS: HERE).  But it is useful for other purposes, when a rule is
; needed for capturing the current position.
[(
    did all [
        uparse? "aaabbb" [some "a", pos: <here>, some "b"]
        pos = "bbb"
    ]
)(
    did all [
        uparse? "<<<stuff>>>" [
            left: across some "<"
            (n: length of left)
            x: between <here> repeat (n) ">"
        ]
        x = "stuff"
    ]
)]


; COLLECT is currently implemented to conspire with the BLOCK! combinator to
; do rollback between its alternates.  But since anyone can write combinators
; that do alternates (in theory), those would have to participate in the
; protocol of rollback too.
;
; Test the very non-generic rollback mechanism.  The only two constructs that
; do any rollback are BLOCK! and COLLECT itself.
[(
    x: <before>
    did all [
        uparse? [1 2] [x: collect [
            keep integer! keep tag! | keep integer! keep integer!
        ]]
        x = [1 2]
    ]
)(
    x: <before>
    did all [  ; semi-nonsensical use of BETWEEN just because it takes 2 rules
        uparse? "(abc)" [x: collect between keep "(" keep ")"]
        x = ["(" ")"]
    ]
)(
    x: <before>
    did all [  ; semi-nonsensical use of BETWEEN just because it takes 2 rules
        not uparse? "(abc}" [x: collect between "(" keep ")"]
        x = <before>
    ]
)(
    x: <before>
    did all [
        uparse? "aaa" [x: collect [some [
            keep (try if false [<not kept>])
            keep <any>
            keep (try if true [<kept>])
        ]]]
        x = [#a <kept> #a <kept> #a <kept>]
    ]
)

    ; Note potential confusion that SOME KEEP and KEEP SOME are not the same.
    ;
    (["a" "a" "a"] = uparse "aaa" [collect [some keep "a"]])
    (["a"] = uparse "aaa" [collect [keep some "a"]])

(
    result: uparse "abbbbabbab" [collect [
        some [keep "a", keep ^[collect [some keep "b" keep (<hi>)]]]
    ]]
    result = ["a" ["b" "b" "b" "b" <hi>] "a" ["b" "b" <hi>] "a" ["b" <hi>]]
)]


; EMIT is a new idea to try and make it easier to use PARSE rules to bubble
; up objects.  It works with a GATHER and SET-WORD!
[(
    did all [
        uparse? [* * * 1 <foo> * * *] [
            some '*
            g: gather [
                emit i: integer! emit t: text! | emit i: integer! emit t: tag!
            ]
            some '*
        ]
        g/i = 1
        g/t = <foo>
    ]
)(
    let result
    uparse "aaabbb" [
        result: gather [
            emit x: collect some ["a", keep (<a>)]
            emit y: collect some ["b", keep (<b>)]
        ]
    ] else [
       fail "Parse failure"
    ]
    did all [
        result.x = [<a> <a> <a>]
        result.y = [<b> <b> <b>]
    ]
)]


; If you EMIT with no GATHER, it's theorized we'd want to make the UPARSE
; itself emit variable definitions, much like LET.  It's a somewhat sketchy
; idea since it doesn't abstract well.  It worked until UPARSE was refactored
; to be built on top of UPARSE* via ENCLOSE, at whic point the lack of
; abstractability of ADD-LET-BINDING was noticed...so deeper macro magic
; would be involved.  For now a leser version is accomplished by just emitting
; an object that you can USE ("using" for now).  Review.
[(
    i: #i
    t: #t
    if true [
        using uparse [1 <foo>] [emit i: integer!, emit t: tag!]
        assert [i = 1, t = <foo>]
    ]
    did all [
        i = #i
        t = #t
    ]
)(
    base: #base
    extension: #extension
    if true [
       let filename: "demo.txt"
       using uparse filename [
            emit base: between <here> "."
            emit extension: across [thru <end>]
        ] else [
            fail "Not a file with an extension"
        ]
        assert [base = "demo"]
        assert [extension = "txt"]
    ]
    did all [
        base = #base
        extension = #extension
    ]
)]


; UPARSE INTO is arity-2, permitting use of a value-bearing rule to produce
; the thing to recurse the parser into...which can generate a new series, as
; well as pick one out of a block.
[(
    did all [
        uparse? ["aaa"] [into text! [x: across some "a"]]
        x = "aaa"
    ]
)(
    did all [
        uparse? ["aaa"] [into <any> [x: across some "a"]]
        x = "aaa"
    ]
)(
    did all [
        uparse? "((aaa)))" [
            into [between some "(" some ")"] [x: across some "a"]
        ]
        x = "aaa"
    ]
)(
    did all [
        uparse? [| | while while while | | |] [
            content: between some '| some '|
            into (content) [x: collect [some keep ^['while]]]
        ]
        x = [while while while]
    ]
)]


; CHANGE is rethought in UPARSE to work with value-bearing rules.  The rule
; gets the same input that the first argument did.
[(
    str: "aaa"
    did all [
        uparse? str [change [some "a"] (if true ["literally"])]
        str = "literally"
    ]
)(
    str: "(aba)"
    did all [
        uparse? str [
            "("
            change [to ")"] [
                collect [
                    some ["a" keep ("A") | <any>]
                ]
            ]
            ")"
        ]
        str = "(AA)"
    ]
)(
    s: {a}
    did all [
        null = uparse s [opt change "b" ("x")]
        s = {a}
    ]
)(
    s: {aba}
    did all [
        '~changed~ = ^ uparse s [while [
            opt change "b" ("x")
            skip
        ]]
        s = {axa}
    ]
)]


; Mixing SET-WORD! with block returns the last value-bearing rule in the PARSE
; (Note: more on this later in this file; review when breaking out separate
; tests for UPARSE into separate files.)
[(
    x: <before>
    did all [
        uparse? [1 "hello"] [x: [tag! | integer!] text!]
        x = 1  ; not [1]
    ]
)(
    x: <before>
    did all [
        uparse? [1 "hello"] [x: [tag! integer! | integer! text!]]
        x = "hello"
    ]
)(
    x: <before>
    did all [
        uparse? [] [x: [opt integer!]]
        x = null
    ]
)(
    x: <before>
    did all [
        not uparse? [] [x: [integer!]]
        x = <before>
    ]
)(
    x: <before>
    did all [
        uparse? [] [x: opt [integer!]]
        x = null
    ]
)

(
    did all [
        uparse? [1 2 3] [x: collect [some keep integer!]]
        x = [1 2 3]
    ]
)]


; !!! There was an idea that SOME could be called on generators, when the
; META-GROUP! was used as plain GROUP! is but would fail on NULL, and would
; be combined with OPT if you wanted it to succeed.  This was changed, so
; now the idea doesn't work.  Is it necessary to make a "generator friendly"
; NULL-means-stop DO-like operator for PARSE?  It's worth thinking about...
;[(
;    gen: func [<static> n (0)] [
;        if n < 3 [return n: n + 1]
;        return null
;    ]
;
;    did all [
;        "a" = uparse "a" ["a", data: some ^(gen)]
;        data = [1 2 3]
;    ]
;)]


; UPARSE can be used where "heavy" nulls produced by the rule products do not
; trigger ELSE, but match failures do.
;
; !!! Is it worth it to add a way to do something like ^[...] block rules to
; say you don't want the NULL-2, or does that just confuse things?  Is it
; better to just rig that up from the outside?
;
;     uparse data rules then result -> [
;         ; If RESULT is null we can react differently here
;     ] else [
;         ; This is a match failure null
;     ]
;
; Adding additional interface complexity onto UPARSE may not be worth it when
; it's this easy to work around.
[
    (
        x: uparse "aaa" [some "a" (null)] else [fail "Shouldn't be reached"]
        x = null
    )
    (
        did-not-match: false
        uparse "aaa" [some "b"] else [did-not-match: true]
        did-not-match
    )
]

; NOT NOT should be equivalent to AHEAD
; Red at time of writing has trouble with this
; As does Haskell Parsec, e.g. (notFollowedBy . notFollowedBy != lookAhead)
; https://github.com/haskell/parsec/issues/8
[
    ("a" = match-uparse "a" [[ahead "a"] "a"])
    ("a" = match-uparse "a" [[not not "a"] "a"])
]


; Historical Rebol's ANY and SOME operations had a built in notion of checking
; for advancement.  This introduced a subtle and easy to trip up on notion
; of whether a rule advanced, so rules that didn't want to stop the iteration
; but removed input etc. would create an issue.  FURTHER takes the test
; for advancement out and makes it usable with any rule.
[
    (null = uparse "" [further [opt "a" opt "b"] ("at least one")])
    ("at least 1" = uparse "a" [further [opt "a" opt "b"] ("at least 1")])
    ("at least 1" = uparse "a" [further [opt "a" opt "b"] ("at least 1")])
    ("at least 1" = uparse "ab" [further [opt "a" opt "b"] ("at least 1")])

    (uparse? "" [while further [to <end>]])
]


[(
    uparse? "baaabccc" [into [between "b" "b"] [some "a" <end>] to <end>]
)(
    not uparse? "baaabccc" [into [between "b" "b"] ["a" <end>] to <end>]
)(
    not uparse? "baaabccc" [into [between "b" "b"] ["a"] to <end>]
)(
    uparse? "baaabccc" [into [between "b" "b"] ["a" to <end>] "c" to <end>]
)(
    uparse? "aaabccc" [into [across to "b"] [some "a"] to <end>]
)]


; INTO can be mixed with HERE to parse into the same series
[(
    x: match-uparse "aaabbb" [
        some "a"
        into <here> ["bbb" (b: "yep, Bs")]
        "bbb" (bb: "Bs again")
    ]
    did all [
        x = "aaabbb"
        b = "yep, Bs"
        bb = "Bs again"
    ]
)(
    x: match-uparse "aaabbbccc" [
        some "a"
        into <here> ["bbb" to <end> (b: "yep, Bs")]
        "bbb" (bb: "Bs again")
        "ccc" (c: "Here be Cs")
    ]
    did all [
        x = "aaabbbccc"
        b = "yep, Bs"
        bb = "Bs again"
        c = "Here be Cs"
    ]
)]


; INTEGER! has some open questions regarding how it is not very visible when
; used as a rule...that maybe it should need a keyword.  Ranges are not worked
; out in terms of how to make [2 4 rule] range between 2 and 4 occurrences,
; as that breaks the combinator pattern at this time.
[
    (uparse? "" [0 <any>])
    (uparse? "a" [1 "a"])
    (uparse? "aa" [2 "a"])
]


; TO and THRU would be too costly to be implicitly value bearing by making
; copies; you need to use ACROSS.
[(
    "" = uparse "abc" [to <end>]
)(
    '~void~ = ^(uparse "abc" [elide to <end>])  ; ornery void
)(
    "b" = uparse "aaabbb" [thru "b" elide to <end>]
)(
    "b" = uparse "aaabbb" [to "b" elide to <end>]
)]


; GET-GROUP!s will splice rules, null means no rule but succeeds...FALSE is
; useful for failing, and TRUE is a synonym for NULL in this context.
[
    (uparse? "aaa" [:(if false ["bbb"]) "aaa"])
    (uparse? "bbbaaa" [:(if true ["bbb"]) "aaa"])
]


; a BLOCK! rule combined with SET-WORD! will evaluate to the last value-bearing
; result in the rule.  This provides compatibility with the historical idea
; of doing Redbol rules like `set var [integer! | text!]`, but in that case
; it would set to the first item captured from the original input...more like
; `copy data [your rule], (var: first data)`.
;
; https://forum.rebol.info/t/separating-parse-rules-across-contexts/313/6
[
    (2 = uparse [1 2] [[integer! integer!]])
    ("a" = uparse ["a"] [[integer! | text!]])
]


; A BLOCK! rule is allowed to return NULL, distinct from failure
[
    (
        x: <before>
        did all [
            uparse? [1] [x: [integer! opt text!]]
            x = null
        ]
    )

    (
        x: <before>
        did all [
            uparse? [1] [integer! x: [(null)]]
            x = null
        ]
    )
]


; TALLY is a new rule for making counting easier
[
    (3 = uparse "aaa" [tally "a"])

    (null = uparse "aaa" [tally "b"])  ; doesn't finish parse
    (0 = uparse "aaa" [tally "b" elide to <end>])  ; must be at end
    (0 = uparse* "aaa" [tally "b"])  ; alternately, use non-force-completion

    (did all [
        uparse "(((stuff)))" [
            n: tally "("
            inner: between <here> repeat (n) ")"
        ]
        inner = "stuff"
    ])
]


; REPEAT lets you make it more obvious when a rule is being repeated, which is
; hidden by INTEGER! variables.
[(
    var: 3
    rule: "a"
    uparse? "aaa" [repeat (var) rule]  ; clearer than [var rule]
)(
    var: 3
    rule: "a"
    uparse? "aaaaaa" [2 repeat (var) rule]
)(
    uparse? ["b" 3 "b" "b" "b"] [rule: <any>, repeat integer! rule]
)]


; This is a little test made to try adjusting rebol-server's webserver.reb to
; use a PARSE rule instead of an "ITERATE".  Since it was written, keep it.
[
    (did argtest: func [args [block!]] [
        let port: _
        let root-dir: _
        let access-dir: _
        let verbose: _
        let dir
        let arg
        uparse args [while [
            "-a", access-dir: [
                <end> (true)
                | "true" (true)
                | "false" (false)
                | dir: <any>, (to-file dir)
            ]
            |
            ["-h" | "-help" | "--help" || (-help, quit)]
            |
            verbose: [
                "-q" (0)
                | "-v" (2)
            ]
            |
            port: into <any> integer!
            |
            arg: <any> (root-dir: to-file arg)
        ]]
        else [
            return null
        ]
        return reduce [access-dir port root-dir verbose]
    ])

    ([_ _ _ 2] = argtest ["-v"])
    ([#[true] _ _ _] = argtest ["-a"])
    ([#[true] _ _ _] = argtest ["-a" "true"])
    ([#[false] _ _ _] = argtest ["-a" "false"])
    ([%something _ _ _] = argtest ["-a" "something"])
    ([_ 8000 _ _] = argtest ["8000"])
    ([_ 8000 _ 2] = argtest ["8000" "-v"])
    ([_ 8000 %foo/bar 2] = argtest ["8000" "-v" "foo/bar"])
]


; THE-WORD!, THE-GROUP!, and THE-PATH! are all used for literal matching.
; This means to match against the input but don't treat what's given as a rule.
;
; !!! These @-types don't exist yet due to not enough type numbers, but will
; be making a comeback.
;
; [(
;    x: [some rule]
;    data: [[some rule] [some rule]]
;    uparse? data [some @x]
;)(
;    data: [[some rule] [some rule]]
;    uparse? data [some @([some rule])]
;)(
;    obj: make object! [x: [some rule]]
;    uparse? data [some @obj.x]
;)]


; Invisibles are another aspect of UPARSE...where you can have a rule match
; but not contribute its synthesized value to the result.
[
    ("a" = uparse "aaab" [[some "a" elide "b"]])

    (
        j: null
        did all [
            null = uparse "b" [[(1000 + 20) elide (j: 304)]]
            j = 304
        ]
    )

    (
        j: null
        did all [
            1020 = uparse "b" ["b" [(1000 + 20) elide (j: 304)]]
            j = 304
        ]
    )

    (uparse? "a" ["a" (comment "GROUP! content can vaporize too!")])

    ; ELIDE doesn't elide failure...just the result on success.
    ;
    (not uparse? "a" [elide "b"])
]


; SKIP is reimagined as invisible; so <any> should be used to capture a single
; "anything" value...
[
    (<a> == uparse [<a> 10] [tag! skip])  ; why be 10 if it was "skipped"
]


; A WHILE that never actually has a succeeding rule gives back a match that is
; a void isotope, as do 0-iteration REPEAT and INTEGER! rules.
;
; !!! These isotopes currently mean invisibility (in the evaluator, these cases
; would wind up becoming ~stale~ isotopes).  Review the difference.
[
    ("a" = uparse "a" ["a" while "b"])
    ("a" = uparse "a" ["a" repeat (0) "b"])
    ("a" = uparse "a" ["a" 0 "b"])

    ("a" = uparse "a" ["a" [while "b"]])
    ("a" = uparse "a" ["a" [repeat (0) "b"]])
    ("a" = uparse "a" ["a" [0 "b"]])
]


; INLINE SEQUENCING is the idea of using || to treat everyting on the left
; as if it's in a block.
;
;    ["a" | "b" || "c"] <=> [["a" | "b"] "c"]
[
    ("c" = uparse "ac" ["a" | "b" || "c"])
    ("c" = uparse "bc" ["a" | "b" || "c"])
    (null = uparse "xc" ["a" | "b" || "c"])

    ("c" = uparse "ac" ["a" || "b" | "c"])
    ("b" = uparse "ab" ["a" || "b" | "c"])
    (null = uparse "ax" ["a" || "b" | "c"])
]


; No-op rules need thought in UPARSE, in terms of NULL/BLANK! behavior.  But
; empty string should be a no-op on string input, and an empty rule should
; always match.
[
    (uparse? "" [[]])
    ("" = uparse "" [""])
]


; ACTION! combinators are a new idea that if you end a PATH! in a /, then it
; will assume you mean to call an ordinary function.
[(
    -1 = uparse [1] [negate/ integer!]
)(
    data: copy ""
    did all [
        "aa" = uparse ["a"] [append/dup/ (data) text! (2)]
        data = "aa"
    ]
)(
    data: copy ""
    uparse ["abc" <reverse> "DEF" "ghi"] [
        while [
            append/ (data) [
                '<reverse> reverse/ copy/ text!
                | text!
            ]
        ]
    ]
    data = "abcFEDghi"
)(
    ["a" "b"] = uparse ["a" "b" <c>] [collect [some keep text!] elide/ tag!]
)]


; SEEK allows you to seek an arbitrary position in the input series, either
; by integer index or by position.  It tolerates BLANK! to mean no-op.
[
    (uparse? "a" [seek (_) "a"])
    (uparse? "aaabbb" ["a" pos: <here> 2 "a" seek (pos) 2 "a" 3 "b"])
    (uparse? "aaabbb" ["a" 2 "a" seek (2) 2 "a" 3 "b"])
]


; UPARSE2 should have a more comprehensive test as part of Redbol, but until
; that is done here are just a few basics to make sure it's working at all.
[
    (true = uparse2 "aaa" [some "a"])
    (true = uparse2 "aaa" [any "a"])
    (true = uparse2 ["aaa"] [into any "a"])
    (true = uparse2 "aaabbb" [
        pos: some "a" some "b" :pos some "a" some "b"
    ])
    (
        x: null
        did all [
            true = uparse2 "aaabbbccc" [to "b" copy x to "c" some "c"]
            x = "bbb"
        ]
    )
    (
        x: null
        did all [
            true = uparse2 "aaabbbccc" [to "b" set x some "b" thru <end>]
            x = #"b"
        ]
    )
]
