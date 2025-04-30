; %parse-between.test.reb
;
; BETWEEN is a new combinator that lets you capture between rules.

(
    all [
        let x
        "a" = parse "aaaa(((How cool is this?))aaaa" [
            some "a", x: between some "(" some ")", some "a"
        ]
        x = "How cool is this?"
    ]
)

(
    all [
        let x
        <c> = parse [<a> <b> * * * -{Thing!}- * * <c>] [
            some tag!, x: between [repeat 3 '*] [repeat 2 '*], some tag!
        ]
        x = [-{Thing!}-]
    ]
)
