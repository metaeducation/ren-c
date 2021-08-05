; %parse-between.test.reb
;
; BETWEEN is a new combinator that lets you capture between rules.

(
    did all [
        uparse? "aaaa(((How cool is this?))aaaa" [
            some "a", x: between some "(" some ")", some "a"
        ]
        x = "How cool is this?"
    ]
)

(
    did all [
        uparse? [<a> <b> * * * {Thing!} * * <c>] [
            some tag!, x: between 3 '* 2 '*, some tag!
        ]
        x = [{Thing!}]
    ]
)
