; %red-replace.parse.test.r
;
; Red's REPLACE has an invention that seems like a pretty bad idea.  It mixes
; PARSE mechanisms in with REPLACE, but it only works if the source data is
; a string...because it "knows" not to treat the block string-like.  This is
; in contention with many other functions that assume you want to stringify
; blocks when passed, and block parsing can't use the same idea.  If anything
; it sounds like a PARSE feature.
;
; For the sake of completeness, let's show how to overload it to get their
; bad idea implemented.

[
    (/replace: enclose lib.replace/ func [f [frame!] <local> head tail rule] [
        if not all [match [text! blob!] f.target, block? f.pattern] [
            return eval-free f
        ]
        rule: if action? :f.replacement '[
            head: <here>
            change [f.pattern, tail: <here>] (
                apply:relax f.replacement/ [const head, const tail]
            )
        ] else '[
            change f.pattern (f.replacement)
        ]
        parse // [case: f.case, f.target [
            while [thru rule] (
                if f.one [return f.target]
            )
            to <end>
        ]]
        return f.target
    ], ok)

    ; These are the tests Red had demonstrating the feature

    ("!racadabra" = replace:one "abracadabra" ["ra" | "ab"] #"!")
    ("!!cad!!" = replace "abracadabra" ["ra" | "ab"] #"!")
    ("!!cad!!" = replace "abracadabra" ["ra" | "ab"] does ["!"])
    (
        s: ~
        "AbrACAdAbrA" = replace "abracadabra" [s: ["a" | "c"]] does [
            uppercase s.1
        ]
    )
    ("a-babAA-" = replace:case "aAbbabAAAa" ["Ab" | "Aa"] "-")

    ; We actually do better than that, by passing in const references to the
    ; functions for the head and tail of the replacement if desired.

    (
        data: "(real)1020(powerful)0304(magic)"
        all [
            ["(real)" "(powerful)" "(magic)"] = collect [
                replace data [between "(" ")"] func [head tail] [
                    let item: copy:part head tail
                    keep item
                    if item = "(powerful)" [item: copy "(ren-c)"]
                    return uppercase item
                ]
            ]
            data = "(REAL)1020(REN-C)0304(MAGIC)"
       ]
    )
]
