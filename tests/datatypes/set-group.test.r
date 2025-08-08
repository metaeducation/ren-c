; SET-GROUP! tests
;
; Hesitancy initially surrounded making `(xxx):` a synonym for `set xxx`.  But
; being able to put the concept of setting along with evaluation into a
; single token has powerful uses...first seen with DEFAULT, e.g.
;
;     (...): default [...]
;
; And later shown to great effect with EMIT in PARSE
;
;     parse ... [gather [varname: across to ..., emit (varname): ...]]
;
; Some weirder ideas, like that SET-GROUP! of an ACTION! will call arity-1
; actions with the right hand side have been axed.


(set-group? first [(a b c):])
(set-tuple? first [a.(b c d):])

(chain! = type of first [(a b c):])
(chain! = type of first [a.(b c d):])

(
    m: <before>
    word: 'm
    (word): 1020
    (word = 'm) and (m = 1020)
)

(
    o: make object! [f: <before>]
    tuple: 'o.f
    (tuple): 304
    (tuple = 'o.f) and (o.f = 304)
)

; Retriggering multi-returns is useful
(
    value: ~
    o: make object! [rest: ~]
    block: [o.rest {value}]
    all [
        10 = (block): transcode:next "10 20"
        10 = value
        o.rest = " 20"
    ]
)

; Weird dropped idea: SET-GROUP! running arity-1 functions.  Right hand side
; should be executed before left group gets evaluated.
;
;    count: 0
;    [1] = collect [
;        (if count != 1 [panic] :keep): (count: count + 1)
;    ]
;

; VOID is legal in a SET-GROUP!, which helps in some interesting cases
;
(3 = (^void): 1 + 2)

; Example of an interesting use of the void case
[
    (returnproxy: lambda [frame [<unrun> frame!]] [
        enclose (augment frame [:return [word!]]) f -> [
            (opt f.return): eval-free f
        ]
    ], ok)

    (
        y: ~
        test: lambda [x] [x + 1000]
        wrapper: returnproxy test/
        all [
            1020 = wrapper 20
            1020 = wrapper:return 20 $y
            1020 = y
        ]
    )

    (
        test: lambda [x] [x + 1000]
        wrapper: returnproxy :test

        y: ~
        f: make frame! unrun :wrapper
        f.x: 20
        f.return: $y

        all [
            1020 = eval f
            1020 = y
        ]
    )
]

; Tricks are done in EXPORT to allow you to optionally export things.  This
; is used by Redbol at time of writing.
[
    (10 = (^void): 10)
    (
        import module [type: module] [export ten: export (^void): 10]
        ten = 10
    )
]
