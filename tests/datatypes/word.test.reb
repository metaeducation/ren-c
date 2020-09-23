; datatypes/word.r
(word? 'a)
(not word? 1)
(word! = type of 'a)
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
    a-value: make image! 0x0
    same? :a-value a-value
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
    a-value: _
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
(action! == type of :reduce)
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
    a-value: get '+
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
    a-value: void
    e: trap [a-value]
    e/id = 'need-non-void
)
(
    a-value: 'a
    :a-value == a-value
)

[#1461 #1478 (
    for-each [str] [
        {<} {+} {|} {=} {-} {>}

        {>=} {=|<} {<><} {-=>} {<-<=}

        {<<} {>>} {>>=} {<<=} {>>=<->}

        {|->} {-<=>-} {-<>-} {>=<}
    ][
        [word pos]: transcode str
        assert [pos = ""]

        assert [word = to word! str]
        assert [str = as text! word]

        [path pos]: transcode unspaced ["a/" str "/b"]
        assert [pos = ""]
        assert [path = compose 'a/(word)/b]

        [block pos]: transcode unspaced ["[" str "]"]
        assert [pos = ""]
        assert [block = reduce [word]]

        [q pos]: transcode unspaced ["'" str]
        assert [pos = ""]
        assert [q = quote word]

        [s pos]: transcode unspaced [str ":"]
        assert [pos = ""]
        assert [s = as set-word! word]

        [g pos]: transcode unspaced [":" str]
        assert [pos = ""]
        assert [g = as get-word! word]

        [l pos]: transcode unspaced ["@" str]
        assert [pos = ""]
        assert [l = as get-word! word]
    ]
    true)
]
