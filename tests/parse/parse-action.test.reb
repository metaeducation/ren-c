; %parse-action.test.reb
;
; ACTION! combinators are a new idea that if you end a PATH! in a /, then it
; will assume you mean to call an ordinary function.

( -1 = uparse [1] [negate/ integer!])

(
    data: copy ""
    did all [
        "aa" = uparse ["a"] [append/dup/ (data) text! (2)]
        data = "aa"
    ]
)

(
    data: copy ""
    uparse ["abc" <reverse> "DEF" "ghi"] [
        some [
            append/ (data) [
                '<reverse> reverse/ copy/ text!
                | text!
            ]
        ]
    ]
    data = "abcFEDghi"
)

(["a" "b"] = uparse ["a" "b" <c>] [collect [some keep text!] elide/ tag!])
