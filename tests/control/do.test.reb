; functions/control/do.r

[
    (ghost? eval:undecayed [])
    ('~,~ = meta eval:undecayed [])

    (ghost? (eval:undecayed []))
    (3 = (1 + 2 eval:undecayed []))
    (3 = (1 + 2 unmeta ^ eval:undecayed []))

    (''30 = ^ (10 + 20 eval:undecayed []))
    ((meta void) = ^ (10 + 20 eval [void]))
    (''30 = ^ (10 + 20 eval:undecayed [comment "hi"]))
    (''30 = ^ (10 + 20 eval:undecayed make frame! func [] [return ~,~]))

    (else? eval [null])
    ('~[~null~]~ = ^ eval:undecayed [if okay [null]])
    ((meta void) = ^ eval:undecayed [if null [<a>]])
    ((meta void) = ^ eval:undecayed [10 + 20 if null [<a>]])

    (all [
        let x: ~
        '~,~ = x: ^ comment "HI" comment "HI"  ; not eval'd in same step
        x = '~,~
    ])

    (all [
        let x: ~
        '~,~ = (x: (meta comment "HI") meta eval:undecayed [comment "HI"])
        '~,~ = x
    ])

    ('~,~ = (10 + 20 meta (eval:undecayed [])))
    ('~,~ = (10 + 20 meta (eval:undecayed [comment "hi"])))
    ((meta void) = (10 + 20 meta (eval:undecayed make frame! lambda [] [void])))
    (null = ^(meta eval [null]))
    ('~[~null~]~ = meta (eval:undecayed [if okay [null]]))

    (30 = (10 + 20 eval:undecayed []))
    (30 = (10 + 20 eval:undecayed [comment "hi"]))
    (30 = (10 + 20 eval:undecayed make frame! func [] [return ~,~]))
    (null = ^(meta eval:undecayed [null]))
    ('~[~null~]~ = ^ eval:undecayed [heavy null])
    ('~[~null~]~ = ^ eval:undecayed [if okay [null]])

    ; Try standalone ^ operator so long as we're at it.
    ('~,~ = ^ eval:undecayed [])
    ('~,~ = ^ eval:undecayed [comment "hi"])
    ('~,~ = ^ eval:undecayed make frame! func [] [return ~,~])
    ((meta void) = ^ eval:undecayed [void])

    ((meta null) = ^ eval:undecayed [null])
    (null = ^(meta eval:undecayed [null]))
    ((meta null) = ^ (eval:undecayed [null]))
    ((meta null) = meta eval:undecayed [null])

    ('~[~null~]~ = ^ eval:undecayed [heavy null])
    ('~[~null~]~ = meta (eval:undecayed [heavy null]))
    ('~[~null~]~ = ^ (eval:undecayed [heavy null]))
    ('~[~null~]~ = meta eval:undecayed [heavy null])

    ('~[~null~]~ = ^ eval:undecayed [if ok [null]])
]


[
    (''3 = ^ (1 + 2 eval:undecayed [comment "HI"]))
    ('~,~ = ^ eval:undecayed [comment "HI"])

    (3 = (1 + 2 eval:undecayed [comment "HI"]))
    (void? eval:undecayed [comment "HI"])

    (
        y: <overwritten>
        x: (1 + 2 y: (void eval:undecayed [comment "HI"]))
        all [
            void? x
            void? y
        ]
    )
]

(
    success: 'false
    eval [success: 'true]
    true? success
)
~expect-arg~ !! (
    a-value: as blob! "Rebol [] 1 + 1"
    eval a-value  ; strings/binaries not handled by EVAL
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
(0.0 = eval [0.0])
(1.0 = eval [1.0])
(
    a-value: me@here.com
    same? a-value eval reduce [a-value]
)
(warning? eval [trap [1 / 0]])
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
    :a-value = eval reduce [:a-value]
)
(NUL = eval [NUL])

(0 = eval [0])
(1 = eval [1])
(#a = eval [#a])
(
    a-value: first ['a/b]
    :a-value = eval [:a-value]
)
(
    a-value: first ['a]
    :a-value = eval [:a-value]
)
('true = eval ['true])
('false = eval ['false])

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
(same? +/ eval [+/])
(0x0 = eval [0x0])
(
    a-value: 'a/b
    :a-value = eval [:a-value]
)
(
    a-value: make port! http://
    port? eval reduce [:a-value]
)
('/a = eval ['/a])
(
    a-value: first [a.b:]
    :a-value = eval [:a-value]
)
(
    a-value: first [a:]
    :a-value = eval [:a-value]
)
(
    a-value: ""
    same? :a-value eval reduce [:a-value]
)
(
    a-value: to tag! ""
    same? a-value eval reduce [a-value]
)
(0:00 = eval [0:00])
(0.0.0 = eval [0.0.0])
((meta void) = ^ eval [()])
('a = eval ['a])

; !!! Currently, EVAL of an ERROR! is like FAIL; it is not definitional,
; and can only be caught with SYS.UTIL/RESCUE.  Should it be?  Or should a
; EVAL of an ERROR! just make it into a definitional warning?
;
~zero-divide~ !! (warning? trap [eval trap [1 / 0] 1])

(
    a-value: first [(2)]
    2 = eval as block! :a-value
)
(
    a-value: "Rebol [] 1"
    trash? do :a-value
)
(trash? do "Rebol []")
(trash? do "Rebol [] 1")
(2 = do "Rebol [] 1 quit:value 2 3")

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
    success: 'false
    pos: evaluate:step [success: 'true success: 'false]
    (true? success) and (pos = [success: 'false])
)
(
    [b value]: evaluate:step [1 2]
    all [
        1 = value
        [2] = b
    ]
)
(
    all wrap [
        null? [pos :value]: evaluate:step:undecayed []
        pos = null
        null? value
    ]
)
(
    [pos value]: evaluate:step [trap [1 / 0]]
    all [
        warning? value
        pos = []
    ]
)

; META:EXCEPT: intercept errors
[
    (
        result': meta:except eval [1 + 2 1 / 0]
        all [
            warning? result'
            result'.id = 'zero-divide
        ]
    )
    ~zero-divide~ !! (  ; full eval can only trap last error (is that correct?)
        result': meta:except eval [1 / 0 1 + 2]
    )
    (
        result': meta:except [pos {_}]: eval:step [1 / 0 1 + 2]
        all [
            warning? result'
            result'.id = 'zero-divide
            pos = [1 + 2]
        ]
    )
    (
        block: [1 + 2 1 / 0 10 + 20]
        [3 ~zero-divide~ 30] = collect [
            while [[block :^result']: eval:step block] [
                if error? unmeta result' [
                    keep quasi (unquasi result').id
                ] else [
                    keep unmeta result'
                ]
            ]
        ]
    )
]


(
    f1: func [return: [integer!]] [evaluate [return 1 2] 2]
    1 = f1
)
; recursive behaviour
(1 = eval [eval [1]])
(1 = do "Rebol [] quit:value eval [1]")
(1 = 1)
(3 = reeval unrun :reeval unrun :add 1 2)
; infinite recursion for block
(
    <deep-enough> = catch wrap [
        x: 0
        blk: [x: x + 1, if x = 2000 [throw <deep-enough>] eval blk]
        eval blk
    ]
)

; This was supposed to test infinite recursion in strings, but it doesn't
; work using module isolation.  Review.
;
[#1896
    ~not-bound~ !! (
        str: "Rebol [] do str"
        do str
    )
]

; infinite recursion for evaluate
(
    <deep-enough> = catch wrap [
        x: 0
        blk: [x: x + 1, if x = 2000 [throw <deep-enough>] evaluate blk]
        eval blk
    ]
)

; evaluating quoted argument
(
    rtest: lambda ['op [word!] 'thing] [reeval op thing]
    -1 = rtest negate 1
)
