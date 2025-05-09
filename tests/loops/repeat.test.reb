; %loops/repeat.test.reb
;
; REPEAT in Ren-C has been standardized across parsing and the main library
; to mean "do this thing a number or range of times, without passing in a
; variable tracking which time we are on".

(
    num: 0
    repeat 10 [num: num + 1]
    10 = num
)
; cycle return value
('false = repeat 1 ['false])
; break cycle
(
    num: 0
    repeat 10 [num: num + 1 break]
    num = 1
)
; break return value
(null? repeat 10 [break])
; continue cycle
(
    success: 'true
    repeat 1 [continue, success: 'false]
    true? success
)
; zero repetition
(
    success: 'true
    repeat 0 [success: 'false]
    true? success
)
(
    success: 'true
    repeat -1 [success: 'false]
    true? success
)
; Test that return stops the loop
(
    f1: func [return: [integer!]] [repeat 1 [return 1 2]]
    1 = f1
)
; Test that errors do not stop the loop and errors can be returned
(
    num: 0
    e: repeat 2 [num: num + 1 trap [1 / 0]]
    all [warning? e num = 2]
)
; loop recursivity
(
    num: 0
    repeat 5 [
        repeat 2 [num: num + 1]
    ]
    num = 10
)
; recursive use of 'break
(
    f: lambda [x] [
        repeat 1 [
            either x = 1 [
                use [break] [
                    break: 1
                    f 2
                    boolean 1 = get $break  ; assigned to integer above
                ]
            ][
                'false
            ]
        ]
    ]
    true? f 1
)


; test that a continue which interrupts code using the mold buffer does not
; leave the gathered material in the mold buffer
;
(
    trash? repeat 2 [unspaced ["abc" continue]]
)

; Test ACTION! as branch.
;
; Note: At one time the branch was:
;
;     branch: does [if nbreak = n [break] n: n + 1]
;
; Definitional BREAK doesn't allow this--that would be a call to a dummy BREAK
; function that says "no loop tied to break".  So there's currently no way for
; a branch expressed as a function to trigger a break (unless it captures the
; specific loop's break somehow).
;
[
    (did branch: does [if nbreak = n [break] n: n + 1])

    (nbreak: ('...), n: 0, void? repeat 0 :branch)
    (nbreak: ('...), n: 0, 3 = repeat 3 :branch)
]

; If body never runs it's void, which acts invisibly in ANY/ALL
[
    (7 = any [repeat 0 [1 + 2], 3 + 4])
    (<vote> = any [
        repeat 1 [<vote>]
        repeat 0 [<abstain>]
    ])
    (7 = any [repeat 10 [break] then [<vote>], 3 + 4])
]
