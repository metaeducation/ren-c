; datatypes/word.r
(word? 'a)
(not word? 1)
(word! = kind of 'a)
; literal form
(word? first [a])
; words are active; actions are word-active
(1 == abs -1)
(
    a-value: #{}
    same? :a-value a-value
)
(
    a-value: charset ""
    same? :a-value a-value
)
(
    a-value: []
    same? :a-value a-value
)
(
    a-value: blank!
    same? :a-value a-value
)
(
    a-value: 1/Jan/0000
    same? :a-value a-value
)
(
    a-value: 0.0
    :a-value == a-value
)
(
    a-value: 1.0
    :a-value == a-value
)
(
    a-value: me@here.com
    same? :a-value a-value
)
(
    error? a-value: trap [1 / 0]
    same? :a-value a-value
)
(
    a-value: %""
    same? :a-value a-value
)
; functions are word-active
(
    a-value: does [1]
    1 == a-value
)
(
    a-value: first [:a]
    :a-value == a-value
)
(
    a-value: NUL
    :a-value == a-value
)
(
    a-value: 0
    :a-value == a-value
)
(
    a-value: 1
    :a-value == a-value
)
(
    a-value: null
    same? :a-value a-value
)
; lit-paths aren't word-active
(
    a-value: first ['a/b]
    a-value == :a-value
)
; lit-words aren't word-active
(
    a-value: first ['a]
    a-value == :a-value
)
(:true == true)
(:false == false)
(
    a-value: $1
    :a-value == a-value
)
; natives are word-active
(frame! == kind of unrun :reduce)
(:blank == blank)
; library test?
(
    a-value: make object! []
    same? :a-value a-value
)
(
    a-value: first [()]
    same? :a-value a-value
)
(
    a-value: get $+
    (1 a-value 2) == 3
)
(
    a-value: 0x0
    :a-value == a-value
)
(
    a-value: 'a/b
    :a-value == a-value
)
(
    a-value: make port! http://
    port? a-value
)
(
    a-value: /a
    :a-value == a-value
)
; routine test?
(
    a-value: first [a/b:]
    :a-value == a-value
)
(
    a-value: first [a:]
    :a-value == a-value
)
(
    a-value: ""
    same? :a-value a-value
)
(
    a-value: make tag! ""
    same? :a-value a-value
)
(
    a-value: 0:00
    same? :a-value a-value
)
(
    a-value: 0.0.0
    same? :a-value a-value
)
(
    a-value: ~bad~
    a-value = ~bad~
)
(
    a-value: 'a
    :a-value == a-value
)

[#1461 #1478 (
    for-each str [
        {<>} {<+>} {<|>} {<=>} {<->} {<>>} {<<>}

        {<} '{+} '{=} '{-} {>}  ; tick marks mean unescaped in path

        {|} {|->} {|>} {|>>}

        {>=} {=|<} {<><} {-=>} {<-<=}

        {<<} {>>} {>>=} {<<=} {>>=<->}

        {-<=>-} {-<>-} {>=<}
    ][
        let assert: specialize :assert [
            handler: [echo Failure on: @str]
        ]

        let unescaped-in-path: quoted? str
        str: noquote str

        [pos word]: transcode/next str
        assert [pos = ""]

        assert [word = to word! str]
        assert [str = as text! word]

        if unescaped-in-path [
            [pos path]: transcode/next unspaced ["a/" str "/b"]
            assert [pos = ""]
            assert [path = compose $a/(word)/b]
        ] else [
            ; !!! The logistics for making this work aren't finished, and
            ; there are higher priorities than rewriting the scanner.
            ;
            comment [
                [pos path]: transcode/next unspaced ["a/|" str "|/b"]
            ]
        ]

        [pos block]: transcode/next unspaced ["[" str "]"]
        assert [pos = ""]
        assert [block = reduce [word]]

        [pos q]: transcode/next unspaced ["'" str]
        assert [pos = ""]
        assert [q = quote word]

        [pos s]: transcode/next unspaced [str ":"]
        assert [pos = ""]
        assert [s = as set-word! word]

        [pos g]: transcode/next unspaced [":" str]
        assert [pos = ""]
        assert [g = as get-word! word]

        [pos l]: transcode/next unspaced ["^^" str]
        assert [pos = ""]
        assert [l = as get-word! word]
    ]
    true)
]

[(
    for-each bad [  ; !!! This could be a much longer list of bad things!
        {<ab>cd} {>ab<cd} {<<ab-cd} {>abcd}
    ][
        assert ['scan-invalid = (trap [load bad]).id]
    ]
    true
)]


; `%%` was added as a WORD! to serve as a quoting-based MAKE FILE! operator.
;
; Single % is not a good idea to have as a WORD!, because `%/foo` would be
; ambiguous as a PATH! vs. FILE!.  Sacrificing %% as a shorthand for %"%"
; and giving it to WORD! is worth it.  How many other forms would be worth
; it is up in the air, so only %% is legal for now.
[
    ("%%" = as text! match word! '%%)
    ("%%" = as text! match set-word! '%%:)
    ("%%" = as text! match get-word! ':%%)
    ("%%" = as text! match meta-word! '^%%)
    ("%%" = as text! match word! first [%%])
    ("%%" = as text! match set-word! first [%%:])
    ("%%" = as text! match get-word! first [:%%])
    ("%%" = as text! match meta-word! first [^%%])
]
