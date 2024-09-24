; %parse-action.test.reb
;
; ACTION! combinators are a new idea that if you start a PATH! with /, then it
; will assume you mean to call an ordinary function.

( -1 = parse [1] [/negate integer!])

(
    data: copy ""
    all [
        "aa" = parse ["a"] [/append/dup (data) text! (2)]
        data = "aa"
    ]
)

(
    data: copy ""
    parse ["abc" <reverse> "DEF" "ghi"] [
        some [
            /append (data) [
                '<reverse> /reverse /copy text!
                | text!
            ]
        ]
    ]
    data = "abcFEDghi"
)

(["a" "b"] = parse ["a" "b" <c>] [collect [some keep text!] /elide tag!])
