; functions/control/do.r

[
    (nihil' = ^ (eval/undecayed []))
    (nihil' = ^(eval/undecayed []))

    (nihil? (eval/undecayed []))
    (3 = (1 + 2 eval/undecayed []))
    (3 = (1 + 2 unmeta ^ eval/undecayed []))

    (''30 = ^ (10 + 20 eval/undecayed []))
    (void' = ^ (10 + 20 eval [void]))
    (''30 = ^ (10 + 20 eval/undecayed [comment "hi"]))
    (''30 = ^ (10 + 20 eval/undecayed make frame! :nihil))

    (didn't eval [null])
    ('~[~null~]~ = ^ eval/undecayed [if true [null]])
    (void' = ^ eval/undecayed [if false [<a>]])
    (void' = ^ eval/undecayed [10 + 20 if false [<a>]])

    (all [
        x: <overwritten>
        nihil' = x: ^ comment "HI" comment "HI"  ; not eval'd in same step
        x = nihil'
    ])

    (all [
        x: <overwritten>
        nihil' = (x: ^(comment "HI") ^ eval/undecayed [comment "HI"])
        nihil' = x
    ])

    (nihil' = (10 + 20 ^(eval/undecayed [])))
    (nihil' = (10 + 20 ^(eval/undecayed [comment "hi"])))
    (void' = (10 + 20 ^(eval/undecayed make frame! lambda [] [void])))
    (null' = ^(eval [null]))
    ('~[~null~]~ = ^(eval/undecayed [if true [null]]))

    (30 = (10 + 20 eval/undecayed []))
    (30 = (10 + 20 eval/undecayed [comment "hi"]))
    (30 = (10 + 20 eval/undecayed make frame! :nihil))
    (null' = ^(eval/undecayed [null]))
    ('~[~null~]~ = ^ eval/undecayed [heavy null])
    ('~[~null~]~ = ^ eval/undecayed [if true [null]])

    ; Try standalone ^ operator so long as we're at it.
    (nihil' = ^ eval/undecayed [])
    (nihil' = ^ eval/undecayed [comment "hi"])
    (nihil' = ^ eval/undecayed make frame! :nihil)
    (void' = ^ eval/undecayed [void])

    (null' = ^ eval/undecayed [null])
    (null' = ^(eval/undecayed [null]))
    (null' = ^ (eval/undecayed [null]))
    (null' = meta eval/undecayed [null])

    ('~[~null~]~ = ^ eval/undecayed [heavy null])
    ('~[~null~]~ = ^(eval/undecayed [heavy null]))
    ('~[~null~]~ = ^ (eval/undecayed [heavy null]))
    ('~[~null~]~ = meta eval/undecayed [heavy null])

    ('~[~null~]~ = ^ eval/undecayed [if true [null]])
]


[
    (''3 = ^ (1 + 2 eval/undecayed [comment "HI"]))
    (nihil' = ^ eval/undecayed [comment "HI"])

    (3 = (1 + 2 eval/undecayed [comment "HI"]))
    (nihil? eval/undecayed [comment "HI"])

    (
        y: <overwritten>
        x: (1 + 2 y: (void eval/undecayed [comment "HI"]))
        all [
            void? x
            voided? $y
        ]
    )
]

(
    success: false
    eval [success: true]
    success
)
~expect-arg~ !! (
    a-value: to binary! "Rebol [] 1 + 1"
    eval a-value  ; strings not handled by EVAL
)
(:abs = eval [:abs])
(
    a-value: #{}
    same? a-value eval reduce [a-value]
)
(
    a-value: charset ""
    same? a-value eval reduce [a-value]
)
(
    a-value: []
    same? a-value eval reduce [a-value]
)
(same? blank! eval reduce [blank!])
(1/Jan/0000 = eval [1/Jan/0000])
(0.0 == eval [0.0])
(1.0 == eval [1.0])
(
    a-value: me@here.com
    same? a-value eval reduce [a-value]
)
(error? eval [trap [1 / 0]])
(
    a-value: %""
    same? a-value eval reduce [a-value]
)
(
    a-value: does []
    same? :a-value eval [:a-value]
)
(
    a-value: first [:a-value]
    :a-value == eval reduce [:a-value]
)
(NUL == eval [NUL])

(0 == eval [0])
(1 == eval [1])
(#a == eval [#a])
(
    a-value: first ['a/b]
    :a-value == eval [:a-value]
)
(
    a-value: first ['a]
    :a-value == eval [:a-value]
)
(~true~ == eval [~true~])
(~false~ == eval [~false~])
($1 == eval [$1])
(same? :append eval [:append])
(null? eval [~null~])
(
    a-value: make object! []
    same? :a-value eval reduce [:a-value]
)
(
    a-value: first [()]
    same? :a-value eval [:a-value]
)
(same? get $+ eval [get $+])
(0x0 == eval [0x0])
(
    a-value: 'a/b
    :a-value == eval [:a-value]
)
(
    a-value: make port! http://
    port? eval reduce [:a-value]
)
(/a == eval [/a])
(
    a-value: first [a/b:]
    :a-value == eval [:a-value]
)
(
    a-value: first [a:]
    :a-value == eval [:a-value]
)
(
    a-value: ""
    same? :a-value eval reduce [:a-value]
)
(
    a-value: make tag! ""
    same? :a-value eval reduce [:a-value]
)
(0:00 == eval [0:00])
(0.0.0 == eval [0.0.0])
(void' = ^ eval [()])
('a == eval ['a])

; !!! Currently, EVAL of an ERROR! is like FAIL; it is not definitional,
; and can only be caught with SYS.UTIL.RESCUE.  Should it be?  Or should a
; EVAL of an ERROR! just make it into a definitional error?
;
~zero-divide~ !! (error? trap [eval trap [1 / 0] 1])

(
    a-value: first [(2)]
    2 == eval as block! :a-value
)
(
    a-value: "Rebol [] 1"
    1 == do :a-value
)
(void? do "Rebol []")
(1 = do "Rebol [] 1")
(3 = do "Rebol [] 1 2 3")

; RETURN stops the evaluation
(
    f1: func [return: [integer!]] [eval [return 1 2] 2]
    1 = f1
)
; THROW stops evaluation
(
    1 = catch [
        eval [
            throw 1
            2
        ]
        2
    ]
)
; BREAK stops evaluation
(
    null? repeat 1 [
        eval [
            break
            2
        ]
        2
    ]
)

; evaluate block tests
(
    success: false
    evaluate/next [success: true success: false] $pos
    success and (pos = [success: false])
)
(
    value: evaluate/next [1 2] $b
    all [
        1 = value
        [2] = b
    ]
)
(
    all [
        trash? evaluate/next/undecayed [] $pos
        pos = null
    ]
)
(
    value: evaluate/next [trap [1 / 0]] $pos
    all [
        error? value
        pos = []
    ]
)
(
    f1: func [return: [integer!]] [evaluate [return 1 2] 2]
    1 = f1
)
; recursive behaviour
(1 = eval [eval [1]])
(1 = do "Rebol [] eval [1]")
(1 == 1)
(3 = reeval unrun :reeval unrun :add 1 2)
; infinite recursion for block
(
    x: 0
    blk: [x: x + 1, if x = 2000 [throw <deep-enough>] eval blk]
    <deep-enough> = catch blk
)

; This was supposed to test infinite recursion in strings, but it doesn't
; work using module isolation.  Review.
;
[#1896
    ~unassigned-attach~ !! (
        str: "Rebol [] do str"
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
