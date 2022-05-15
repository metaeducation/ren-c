; functions/control/do.r

; By default, DO will not be invisible.  You get an "ornery" return result
; of ~none~ isotope to help remind you that you are not seeing the whole
; picture.  Returning NULL might seem "friendlier" but it is misleading.
[
    ('~none~ = ^ (10 + 20 do []))
    ('~none~ = ^ (10 + 20 do [void]))
    ('~none~ = ^ (10 + 20 do [comment "hi"]))
    (''30 = ^ (10 + 20 do make frame! :void))
    (didn't do [null])
    ('~null~ = ^ do [if true [null]])

    (did all [
        '~none~ = x: ^ comment "HI" do [comment "HI"]
        x = '~none~
    ])

    (did all [
        '~none~ = (x: ^(comment "HI") ^ do [comment "HI"])
        x = '~void~
    ])

    ('~none~ = (10 + 20 ^(do [])))
    ('~none~ = (10 + 20 ^(do [comment "hi"])))
    ('~void~ = (10 + 20 ^(do make frame! :void)))
    (didn't ^(do [null]))
    ('~null~ = ^(do [if true [null]]))

    (30 = (10 + 20 none-to-void do []))
    (30 = (10 + 20 none-to-void do [comment "hi"]))
    (30 = (10 + 20 none-to-void do make frame! :void))
    (didn't ^(none-to-void do [null]))
    ('~null~ = ^(none-to-void do [heavy null]))
    ('~null~ = ^(none-to-void do [if true [null]]))

    ; Try standalone ^ operator so long as we're at it.
    ('~void~ = ^ none-to-void do [])
    ('~void~ = ^ none-to-void do [comment "hi"])
    ('~void~ = ^ none-to-void do make frame! :void)
    (didn't ^ none-to-void do [null])
    ('~null~ = ^ none-to-void do [heavy null])
    ('~null~ = ^ none-to-void do [if true [null]])
]


[
    ('~none~ = ^ (1 + 2 do [comment "HI"]))
    ('~none~ = ^ do [comment "HI"])

    (
        x: (1 + 2 y: do [comment "HI"])
        did all [
            '~none~ = ^x
            '~none~ = ^y
        ]
    )
]

(
    success: false
    do [success: true]
    success
)
(
    a-value: to binary! "1 + 1"
    2 == do a-value
)
; do block start
(:abs = do [:abs])
(
    a-value: #{}
    same? a-value do reduce [a-value]
)
(
    a-value: charset ""
    same? a-value do reduce [a-value]
)
(
    a-value: []
    same? a-value do reduce [a-value]
)
(same? blank! do reduce [blank!])
(1/Jan/0000 = do [1/Jan/0000])
(0.0 == do [0.0])
(1.0 == do [1.0])
(
    a-value: me@here.com
    same? a-value do reduce [a-value]
)
(error? do [trap [1 / 0]])
(
    a-value: %""
    same? a-value do reduce [a-value]
)
(
    a-value: does []
    same? :a-value do [:a-value]
)
(
    a-value: first [:a-value]
    :a-value == do reduce [:a-value]
)
(NUL == do [NUL])
(
    a-value: make image! 0x0
    same? a-value do reduce [a-value]
)
(0 == do [0])
(1 == do [1])
(#a == do [#a])
(
    a-value: first ['a/b]
    :a-value == do [:a-value]
)
(
    a-value: first ['a]
    :a-value == do [:a-value]
)
(#[true] == do [#[true]])
(#[false] == do [#[false]])
($1 == do [$1])
(same? :append do [:append])
(blank? do [_])
(
    a-value: make object! []
    same? :a-value do reduce [:a-value]
)
(
    a-value: first [()]
    same? :a-value do [:a-value]
)
(same? get '+ do [get '+])
(0x0 == do [0x0])
(
    a-value: 'a/b
    :a-value == do [:a-value]
)
(
    a-value: make port! http://
    port? do reduce [:a-value]
)
(/a == do [/a])
(
    a-value: first [a/b:]
    :a-value == do [:a-value]
)
(
    a-value: first [a:]
    :a-value == do [:a-value]
)
(
    a-value: ""
    same? :a-value do reduce [:a-value]
)
(
    a-value: make tag! ""
    same? :a-value do reduce [:a-value]
)
(0:00 == do [0:00])
(0.0.0 == do [0.0.0])
('~none~ = ^ do [()])
('a == do ['a])
(error? trap [do trap [1 / 0] 1])
(
    a-value: first [(2)]
    2 == do as block! :a-value
)
(
    a-value: "1"
    1 == do :a-value
)
('~none~ = ^ do "")
(1 = do "1")
(3 = do "1 2 3")

; RETURN stops the evaluation
(
    f1: func [return: [integer!]] [do [return 1 2] 2]
    1 = f1
)
; THROW stops evaluation
(
    1 = catch [
        do [
            throw 1
            2
        ]
        2
    ]
)
; BREAK stops evaluation
(
    null? repeat 1 [
        do [
            break
            2
        ]
        2
    ]
)
; evaluate block tests
(
    success: false
    evaluate [success: true success: false]
    success
)
(
    [value b]: evaluate [1 2]
    did all [
        1 = value
        [2] = b
    ]
)
(
    value: ~
    did all [
        null? ^ [value @]: evaluate []
        '~none~ = ^value
    ]
)
(
    [value #]: evaluate [trap [1 / 0]]
    error? value
)
(
    f1: func [return: [integer!]] [evaluate [return 1 2] 2]
    1 = f1
)
; recursive behaviour
(1 = do [do [1]])
(1 = do "do [1]")
(1 == 1)
(3 = reeval :reeval :add 1 2)
; infinite recursion for block
(
    blk: [do blk]
    error? trap blk
)
; infinite recursion for string
[#1896 (
    str: "do str"
    error? trap [do str]
)]
; infinite recursion for evaluate
(
    blk: [b: evaluate blk]
    error? trap blk
)

; evaluating quoted argument
(
    rtest: func ['op [word!] 'thing] [reeval op thing]
    -1 = rtest negate 1
)
