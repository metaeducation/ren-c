; functions/control/do.r

; !!! Should DO be able to return void?
[
    (void' = ^(do []))
    (void' = ^ (eval []))
    (void' = ^ (maybe eval []))

    (void? do [])
    (void? (eval []))
    (void? maybe eval [])
    (3 = (1 + 2 eval []))
    (3 = (1 + 2 unmeta ^ eval []))

    (''30 = ^ (10 + 20 eval []))
    (''30 = ^ (10 + 20 eval [void]))
    (''30 = ^ (10 + 20 eval [comment "hi"]))
    (''30 = ^ (10 + 20 eval make frame! unrun :void))

    (didn't do [null])
    ('~[~null~]~ = ^ do [if true [null]])
    (void' = ^ do [if false [<a>]])
    (''30 = ^ do [10 + 20 if false [<a>]])

    (did all [
        x: <overwritten>
        void' = x: ^ comment "HI" comment "HI"  ; not eval'd in same step
        x = void'
    ])

    (did all [
        x: <overwritten>
        void' = (x: ^(comment "HI") ^ do [comment "HI"])
        void' = x
    ])

    (void' = (10 + 20 ^(eval [])))
    (void' = (10 + 20 ^(eval [comment "hi"])))
    (void' = (10 + 20 ^(eval make frame! unrun :void)))
    (null' = ^(eval [null]))
    ('~[~null~]~ = ^(eval [if true [null]]))

    (30 = (10 + 20 eval []))
    (30 = (10 + 20 eval [comment "hi"]))
    (30 = (10 + 20 eval make frame! unrun :void))
    (null' = ^(eval [null]))
    ('~[~null~]~ = ^ eval [heavy null])
    ('~[~null~]~ = ^ eval [if true [null]])

    ; Try standalone ^ operator so long as we're at it.
    (void' = ^ eval [])
    (void' = ^ eval [comment "hi"])
    (void' = ^ eval make frame! unrun :void)
    (void' = ^ do :void)

    (null' = ^ eval [null])
    (null' = ^(eval [null]))
    (null' = ^ (eval [null]))
    (null' = meta eval [null])

    ('~[~null~]~ = ^ eval [heavy null])
    ('~[~null~]~ = ^(eval [heavy null]))
    ('~[~null~]~ = ^ (eval [heavy null]))
    ('~[~null~]~ = meta eval [heavy null])

    ('~[~null~]~ = ^ eval [if true [null]])
]


[
    (''3 = ^ (1 + 2 eval [comment "HI"]))
    (void' = ^ eval [comment "HI"])

    (3 = (1 + 2 eval [comment "HI"]))
    (void? eval [comment "HI"])

    (
        y: <overwritten>
        x: (1 + 2 y: eval [comment "HI"])
        did all [
            x = 3
            voided? 'y
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
(~true~ == do [~true~])
(~false~ == do [~false~])
($1 == do [$1])
(same? :append do [:append])
(null? do [~null~])
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
(void' = ^ do [()])
('a == do ['a])

; !!! At time of writing, DO of an ERROR! is like FAIL; it is not definitional,
; and can only be caught with SYS.UTIL.RESCUE.  Should it be?  Or should a
; DO of an ERROR! just make it into a definitional error?
;
~zero-divide~ !! (error? trap [do trap [1 / 0] 1])

(
    a-value: first [(2)]
    2 == do as block! :a-value
)
(
    a-value: "1"
    1 == do :a-value
)
(void? do "")
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
    value: evaluate [1 2] 'b
    did all [
        1 = value
        [2] = b
    ]
)
(
    value: <overwritten>
    did all [
        null? [value @]: evaluate/next []  ; @ requests po after step (null)
        unset? 'value
    ]
)
(
    value: evaluate/next [trap [1 / 0]]
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
    x: 0
    blk: [x: x + 1, if x = 2000 [throw <deep-enough>] do blk]
    <deep-enough> = catch blk
)

; This was supposed to test infinite recursion in strings, but it doesn't
; work using module isolation.  Review.
;
[#1896
    ~unassigned-attach~ !! (
        str: "do str"
        do str
    )
]

; infinite recursion for evaluate
(
    x: 0
    blk: [x: x + 1, if x = 2000 [throw <deep-enough>] b: evaluate blk]
    <deep-enough> = catch blk
)

; evaluating quoted argument
(
    rtest: lambda ['op [word!] 'thing] [reeval op thing]
    -1 = rtest negate 1
)
