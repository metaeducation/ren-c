; functions/control/do.r

; By default, DO will not be invisible.  You get an "ornery" return result
; of ~void~ isotope to help remind you that you are not seeing the whole
; picture.  Returning NULL might seem "friendlier" but it is misleading.
[
    ('~void~ = ^(do []))
    ('~void~ = ^ (eval []))
    (blank? ^ (maybe eval []))

    (none? do [])
    (invisible? (maybe eval []))
    (invisible? maybe eval [])
    (3 = (1 + 2 maybe eval []))
    (3 = (1 + 2 maybe unmeta ^ maybe eval []))

    ('~void~ = ^ (10 + 20 eval []))
    ('~void~ = ^ (10 + 20 eval [void]))
    ('~void~ = ^ (10 + 20 eval [comment "hi"]))
    ('~void~ = ^ (10 + 20 eval make frame! :void))

    (didn't do [null])
    ('~null~ = ^ do [if true [null]])
    ('~void~ = ^ do [if false [<a>]])
    ('~void~ = ^ do [10 + 20 if false [<a>]])

    (did all [
        x: <overwritten>
        _ = x: ^ comment "HI" comment "HI"  ; not eval'd in same step
        x = _
    ])

    (did all [
        x: <overwritten>
        '~void~ = (x: ^(comment "HI") ^ do [comment "HI"])
        blank? x
    ])

    ('~void~ = (10 + 20 ^(eval [])))
    ('~void~ = (10 + 20 ^(eval [comment "hi"])))
    ('~void~ = (10 + 20 ^(eval make frame! :void)))
    (didn't ^(eval [null]))
    ('~null~ = ^(eval [if true [null]]))

    (30 = (10 + 20 maybe eval []))
    (30 = (10 + 20 maybe eval [comment "hi"]))
    (30 = (10 + 20 maybe eval make frame! :void))
    (didn't ^(maybe eval [null]))
    ('~null~ = ^(maybe eval [heavy null]))
    ('~null~ = ^(maybe eval [if true [null]]))

    ; Try standalone ^ operator so long as we're at it.
    ('~void~ = ^ eval [])
    ('~void~ = ^ eval [comment "hi"])
    ('~void~ = ^ eval make frame! :void)
    ('~void~ = ^ do :void)

    (didn't ^ maybe eval [null])
    ('~null~ = ^ maybe eval [heavy null])
    ('~null~ = ^ maybe eval [if true [null]])
]


[
    ('~void~ = ^ (1 + 2 eval [comment "HI"]))
    ('~void~ = ^ eval [comment "HI"])

    (3 = (1 + 2 maybe eval [comment "HI"]))
    ('~void~ = ^ eval [comment "HI"])

    (
        x: (1 + 2 y: eval [comment "HI"])
        did all [
            unset? 'x
            unset? 'y
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
    evaluate/next [success: true success: false] #
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
    value: <overwritten>
    did all [
        null? [value @]: evaluate []  ; @ requests position after step (null)
        unset? 'value
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

; Vanishing steps vanish (!)
(
    result: null
    block: [1 + 2 comment "WhOAHhhHAH!"]

    3 = while [[result @block]: evaluate block] [get/any 'result]
)
(
    result: null
    block: [1 + 2 comment "WhOAHhhHAH!"]

    3 = while [[result block]: evaluate block, block] [get/any 'result]
)

; evaluating quoted argument
(
    rtest: func ['op [word!] 'thing] [reeval op thing]
    -1 = rtest negate 1
)
