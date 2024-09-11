; %parse-accumulate.test.reb

([a b] = parse ["a" a <a> "b" b <b>] [accumulate [text! word! elide tag!]])

(
    parse [x: $y z "a" <b> %c] [
        words: accumulate &any-word?
        strings: accumulate &any-string?
    ]
    all [
       words = [x: $y z]
       strings = ["a" <b> %c]
    ]
)

(
    parse ["a" <b> %c] [
        words: accumulate &any-word?
        strings: accumulate &any-string?
    ]
    all [
       words = []
       strings = ["a" <b> %c]
    ]
)

(
    parse [x: [$y z] "a" <b> %c] [
        words: accumulate [
            &any-word?
            | b: subparse block! [some &any-word? <subinput>] (spread b)
        ]
        strings: accumulate &any-string?
    ]
    all [
       words = [x: $y z]
       strings = ["a" <b> %c]
    ]
)
