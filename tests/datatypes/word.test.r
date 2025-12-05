; datatypes/word.r
(word? 'a)
(not word? 1)
(word! = type of 'a)
; literal form
(word? first [a])
; words are active; actions are word-active
(1 = abs -1)
(
    a-value: #{}
    same? ^a-value a-value
)
(
    a-value: charset ""
    same? ^a-value a-value
)
(
    a-value: []
    same? ^a-value a-value
)
(
    a-value: integer!
    same? ^a-value a-value
)
(
    a-value: 1/Jan/0000
    same? ^a-value a-value
)
(
    a-value: 0.0
    ^a-value = a-value
)
(
    a-value: 1.0
    ^a-value = a-value
)
(
    a-value: me@here.com
    same? ^a-value a-value
)
(
    warning? a-value: rescue [1 / 0]
    same? ^a-value a-value
)
(
    a-value: %""
    same? ^a-value a-value
)
; functions are word-active
(
    a-value: does [1]
    1 = a-value
)
(
    a-value: first [:a]
    ^a-value = a-value
)
(
    a-value: NUL
    ^a-value = a-value
)
(
    a-value: 0
    ^a-value = a-value
)
(
    a-value: 1
    ^a-value = a-value
)
(
    a-value: null
    same? ^a-value a-value
)
; lit-paths aren't word-active
(
    a-value: first ['a/b]
    a-value = ^a-value
)
; lit-words aren't word-active
(
    a-value: first ['a]
    a-value = ^a-value
)
(^okay = okay)
(^null = null)
(
    a-value: $1
    ^a-value = a-value
)
; natives are word-active
(frame! = type of unrun reduce/)
(^space = space)
; library test?
(
    a-value: make object! []
    same? ^a-value a-value
)
(
    a-value: first [()]
    same? ^a-value a-value
)
(
    a-value: +/
    (1 a-value 2) = 3
)
(
    a-value: 0x0
    ^a-value = a-value
)
(
    a-value: 'a/b
    ^a-value = a-value
)
(
    a-value: make port! http://
    port? a-value
)
(
    a-value: '/a
    ^a-value = a-value
)
; routine test?
(
    a-value: first [a.b:]
    ^a-value = a-value
)
(
    a-value: first [a:]
    ^a-value = a-value
)
(
    a-value: ""
    same? ^a-value a-value
)
(
    a-value: to tag! ""
    same? ^a-value a-value
)
(
    a-value: 0:00
    same? ^a-value a-value
)
(
    a-value: 0.0.0
    same? ^a-value a-value
)
~bad-word-get~ !! (
    a-value: ~#bad~
    a-value = ~#bad~
)
(
    a-value: ~#bad~
    (lift get meta $a-value) = '~#bad~
)
(
    a-value: 'a
    ^a-value = a-value
)

[#1461 #1478 (
    for-each 'str [
        -[<>]- -[<+>]- -[<|>]- -[<=>]- -[<->]- -[<>>]- -[<<>]-

        -[<]- '-[+]- '-[=]- '-[-]- -[>]-  ; tick marks mean unescaped in path

        -[|]- -[|->]- -[|>]- -[|>>]-

        -[>=]- -[=|<]- -[<><]- -[-=>]- -[<-<=]-

        -[<<]- -[>>]- -[>>=]- -[<<=]- -[>>=<->]-

        -[-<=>-]- -[-<>-]- -[>=<]-
    ] wrap [
        assert: specialize lib.assert/ [
            handler: [echo Failure on: @str]
        ]

        valid-in-path: quoted? str
        set $str noquote str  ; use SET to avoid WRAP

        [pos word]: transcode:next str
        assert [pos = ""]

        assert [word = to word! str]
        assert [str = as text! word]

        if valid-in-path [
            let ['pos path]: transcode:next unspaced ["a/" str "/b"]
            assert [pos = ""]
            assert [path = compose $a/(word)/b]
        ] else [
            comment [  ; !!! Path scan with arrow words is buggy, scans tags
                let e: rescue [transcode:next unspaced ["a/" str "/b"]]
                assert [e]
            ]
        ]

        [pos block]: transcode:next unspaced ["[" str "]"]
        assert [pos = ""]
        assert [block = reduce [word]]

        [pos q]: transcode:next unspaced ["'" str]
        assert [pos = ""]
        assert [q = quote word]

        [pos s]: transcode:next unspaced [str ":"]
        assert [pos = ""]
        assert [s = setify word]

        [pos g]: transcode:next unspaced [":" str]
        assert [pos = ""]
        assert [g = getify word]

        [pos l]: transcode:next unspaced ["^^" str]
        assert [pos = ""]
        assert [l = meta word]
    ]
    ok)
]

[(
    for-each 'bad [  ; !!! This could be a much longer list of bad things!
        -[<ab>cd]- -[>ab<cd]- -[<<ab-cd]- -[>abcd]-
    ][
        assert ['scan-invalid = (rescue [load bad]).id]
    ]
    ok
)]


; `%%` was added as a WORD! to serve as a quoting-based MAKE FILE! operator
; (essentially obsoleted by interpolation).
;
; Single % may not be a good idea to have as a WORD!, since if you tried to
; pick out of it as a TUPLE! you'd get things like %.git being a directory
; starting with dot, or %/foo/ being a directory etc.
;
; Sacrificing %% and making it a WORD! may still be worth it, vs saying
; that %% is a FILE! named percent?  Unclear.  Review.
[
    ("%%" = as text! match word! '%%)
    ("%%" = as text! match meta-word! '^%%)
    ("%%" = as text! match word! first [%%])
    ("%%" = as text! match meta-word! first [^%%])

    ; REVIEW meanings of %%: and :%% etc.
    ;
    ; %/foo has to be a FILE!, and %.foo should be a FILE!, so we would
    ; probably assume %:foo is a file as well as %foo:
    ;
    ; If you want to assign things like % or %% then use [%]: or [%%]:

    ~???~ !! (transcode "'%%:")
    ~???~ !! (transcode "':%%")
    ~???~ !! (transcode "%%:")
    ~???~ !! (transcode ":%%")

    (
        [%]: 304
        % = 304
    )(
        [%%]: 1020
        %% = 1020
    )
]
