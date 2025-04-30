; %parse-path.test.reb
;
; PATH!s only have meaning in the default combinator set when they terminate
; in a BLANK!, and mean "run as action combinator".
;
; When paths represent cascades of functions vs. refinements, there will be
; more to test here.
;
; Other applications would be in Rebol2/Red PARSE emulation to do variable
; lookup, and creative possibilites that do not have anything to do with
; function execution or variable lookup are also possible with custom maps
; of combinators.
;

[https://github.com/red/red/issues/4101
    ('a/b = parse [a/b] ['a/b])

    ~???~ !! (
        parse [a/b] [a/b]
    )
    ~???~ !! (
        parse [a b c] [change repeat 3 word! d/e]
    )
    ~???~ !! (
        parse [a/b c d] [remove a/b]
    )
    ~???~ !! (
        parse [c d] [insert a/b repeat 2 word!]
    )
]
