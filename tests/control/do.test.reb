; functions/control/do.r

; By default, DO will not be invisible.  You get an "ornery" return result
; on invisibility to help remind you that you are not seeing the whole
; picture.  Returning NULL might seem "friendlier" but it is misleading.
[
    ('~stale~ = ^ (10 + 20 do []))
    ('~stale~ = ^ (10 + 20 do [void]))
    ('~stale~ = ^ (10 + 20 do [comment "hi"]))
    ('~stale~ = ^ (10 + 20 do make frame! :void))
    (else? do [null])
    ('~null~ = ^ do [if true [null]])

    ('~void~ = ^ comment "HI" do [comment "HI"])

    ('~void~ = (10 + 20 ^(do [])))
    ('~void~ = (10 + 20 ^(do [comment "hi"])))
    ('~void~ = (10 + 20 ^(do make frame! :void)))
    (else? ^(do [null]))
    ('~null~ = ^(do [if true [null]]))

    (30 = (10 + 20 devoid do []))
    (30 = (10 + 20 devoid do [comment "hi"]))
    (30 = (10 + 20 devoid do make frame! :void))
    (else? ^(devoid do [null]))
    ('~null~ = ^(devoid do [heavy null]))
    ('~null~ = ^(devoid do [if true [null]]))

    ; Try standalone ^ operator so long as we're at it.
    ('~void~ = ^ ^ devoid do [])
    ('~void~ = ^ ^ devoid do [comment "hi"])
    ('~void~ = ^ ^ devoid do make frame! :void)
    (else? ^ devoid do [null])
    ('~null~ = ^ devoid do [heavy null])
    ('~null~ = ^ devoid do [if true [null]])
]


[
    ('~stale~ = ^ (1 + 2 do [comment "HI"]))
    ('~void~ = ^ do [comment "HI"])

    (
        x: (1 + 2 y: do [comment "HI"])
        did all [
            '~stale~ = ^x
            '~void~ = ^y
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
('~void~ = ^ do [()])
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
('~void~ = ^ do "")
(1 = do "1")
(3 = do "1 2 3")

; RETURN stops the evaluation
(
    f1: func [] [do [return 1 2] 2]
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
    [b value]: evaluate [1 2]
    did all [
        1 = value
        [2] = b
    ]
)
(
    value: <overwritten>
    did all [
        null? ^ [# value]: evaluate []
        '~void~ = ^value
    ]
)
(
    [# value]: evaluate [trap [1 / 0]]
    error? value
)
(
    f1: func [] [evaluate [return 1 2] 2]
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
