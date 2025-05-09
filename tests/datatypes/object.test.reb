; datatypes/object.r
(object? make object! [x: 1])
(not object? 1)
(object! = type of make object! [x: 1])
; minimum
(object? make object! [])
; local words
(
    x: 1
    make object! [x: 2]
    x = 1
)
; BREAK out of make object!
[#846 (
    null? repeat 1 [
        make object! [break]
        2
    ]
)]
; THROW out of make object!
[#847 (
    1 = catch [
        make object! [throw 1]
        2
    ]
)]
~zero-divide~ !! (
    error? trap [  ; not a plain throw-type error, won't TRAP
        make object! [1 / 0]
        2
    ]
)
; RETURN out of make object!
[#848 (
    f: func [return: [integer!]] [
        make object! [return 1]
        2
    ]
    1 = f
)]
; object cloning
[#2045 (
    a: 1
    f: lambda [] [a]
    g: get $f
    o: make object! [a: 2 /g: get $f]
    p: make o [a: 3]
    1 = p/g
)]
; object cloning
[#2045 (
    a: 1
    b: [a]
    c: b
    o: make object! [a: 2 c: b]
    p: make o [a: 3]
    1 = eval p.c
)]

; extending objects
[#1979 (
    o: make object! []
    extend o [b: 1 b: 2]
    1 = length of words of o
)]
(
    o: make object! [b: 0]
    extend o [b: 1 b: 2]
    1 = length of words of o
)
(
    o: make object! []
    c: "c"
    extend o compose [b: "b" b: (c)]
    same? c o.b
)
(
    o: make object! [b: "a"]
    c: "c"
    extend o compose [b: "b" b: (c)]
    same? c o.b
)

; SELF was a word added automatically by the system, which had some strange
; internal guts.  This made it a "reserved word", which is against the
; principles of the core.  While object generators may choose to make a
; SELF (the way FUNC chooses to define RETURN), it's no longer built in.
; So these scenarios no longer apply:
;
; [#2076 (
;     o: make object! [x: 10]
;     e: trap [append o spread [self: 1]]
;     e.id = 'hidden
; )]
;
; [#187 (
;     o: make object! [self]
;     [] = words of o
; )]
;
; [#1553 (
;    o: make object! [a: null]
;    same? (binding of in o 'self) (binding of in o 'a)
; )]
;
; [#1756
;     (reeval does [reduce reduce [:self] okay])
; ]
;
; [#1528
;     (action? func [self] [])
; ]
(
    o: make object! []
    extend o 'self
    '~ = ^ get:any @o.self
)(
    o: make object! []
    extend o [self: 1]
    o.self = 1
)


; Change from R3-Alpha, FUNC and FUNCTION do not do any rebinding of words.
; when derived objects are created.  You have to use METHOD, and you have
; to use .WORD accesses to indicate you want a member variable.
(
    o1: make object! [a: 10 b: func [] [let f: lambda [] [a] return f]]
    o2: make o1 [a: 20]

    o2/b = 10
)(
    o1: make object! [a: 10 b: method [] [let f: lambda [] [.a] return f]]
    o2: make o1 [a: 20]

    o2/b = 20
)

(
    o-big: construct inside [] collect [
        count-up 'n 256 [
            ;
            ; var-1: 1
            ; var-2: 2
            ; ...
            ; var-256: 256
            ;
            keep spread compose [
                (join 'var- [n]): (n)
            ]
        ]
        count-up 'n 256 [
            ;
            ; fun-1: method [] [.var-1]
            ; fun-2: method [] [.var-1 + .var-2]
            ; ...
            ; fun-256: method [] [.var-1 + .var-2 ... + .var-256]
            ;
            keep spread compose [
                (join 'meth- [n]): method [] (collect [
                    keep 'return
                    count-up 'i n [
                        keep spread compose [
                            .(join 'var- [i]) (if i <> n ['+])
                        ]
                    ]
                ])
            ]
        ]
    ]

    ; Note: Because derivation in R3-Alpha requires deep copying and rebinding
    ; bodies of all function members, it will choke on the following.  In
    ; Ren-C it is nearly instantaneous.  Despite not making those copies,
    ; derived binding allows the derived object's methods to see the derived
    ; object's values.
    ;
    count-up 'i 2048 wrap [
        derived: make o-big [var-1: 100000 + i]
        if 132639 + i <> derived/meth-255 [
            panic "Unexpected Sum"
        ]
    ]
    ok
)

; object cloning
[#2050 (
    o: make object! [n: 'o b: reduce [unrun lambda [] [n]]]
    p: make o [n: 'p]
    'o = run (o.b).1
)]


[
    https://github.com/metaeducation/ren-c/issues/907

    (
        o: make object! []
        ok
    )

    ~bad-pick~ !! (o.i: 1)
    ~bad-pick~ !! (set? $o.i)
    ~bad-pick~ !! (unset? $o.i)

    (null = has o 'i)
]

(
    obj: construct [
        x: 10
        foo: method [] [
            let helper: does [.]  ; . looks in frame binding for coupling
            return helper
        ]
    ]
    obj/foo = obj
)
