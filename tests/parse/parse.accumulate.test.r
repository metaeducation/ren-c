; %parse-accumulate.test.r

([a b] = parse ["a" a <a> "b" b <b>] [accumulate [text! word! elide tag!]])

(
    words: ~
    strings: ~
    parse [x: $y z "a" <b> %c] [
        words: accumulate [any-word?/ | set-word?/]
        strings: accumulate any-string?/
    ]
    all [
       words = [x: $y z]
       strings = ["a" <b> %c]
    ]
)

(
    words: ~
    strings: ~
    parse ["a" <b> %c] [
        words: accumulate any-word?/
        strings: accumulate any-string?/
    ]
    all [
       words = []
       strings = ["a" <b> %c]
    ]
)

(
    words: ~
    strings: ~
    b: ~
    parse [x: [$y z] "a" <b> %c] [
        words: accumulate [
            any-word?/ | set-word?/
            | b: subparse block! [some any-word?/ <subinput>] (spread b)
        ]
        strings: accumulate any-string?/
    ]
    all [
       words = [x: $y z]
       strings = ["a" <b> %c]
    ]
)
