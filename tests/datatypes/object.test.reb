; datatypes/object.r
(object? make object! [x: 1])
(not object? 1)
(object! = type of make object! [x: 1])
; minimum
(object? make object! [])
; literal form
(object? #[object! [[][]]])
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
; "error out" of make object!
(
    error? trap [
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
    f: func [] [a]
    g: :f
    o: make object! [a: 2 g: :f]
    p: make o [a: 3]
    1 == p.g
)]
; object cloning
[#2045 (
    a: 1
    b: [a]
    c: b
    o: make object! [a: 2 c: b]
    p: make o [a: 3]
    1 == do p.c
)]
; appending to objects
[#1979 (
    o: make object! []
    append o [b: 1 b: 2]
    1 == length of words of o
)]
(
    o: make object! [b: 0]
    append o [b: 1 b: 2]
    1 == length of words of o
)
(
    o: make object! []
    c: "c"
    append o compose [b: "b" b: (c)]
    same? c o.b
)
(
    o: make object! [b: "a"]
    c: "c"
    append o compose [b: "b" b: (c)]
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
;     e: trap [append o [self: 1]]
;     e.id = 'hidden
; )]
;
; [#187 (
;     o: make object! [self]
;     [] = words of o
; )]
;
; [#1553 (
;    o: make object! [a: _]
;    same? (binding of in o 'self) (binding of in o 'a)
; )]
;
; [#1756
;     (reeval does [reduce reduce [:self] true])
; ]
;
; [#1528
;     (action? func [self] [])
; ]
(
    o: make object! []
    append o 'self
    '~ = ^ get/any 'o.self
)(
    o: make object! []
    append o [self: 1]
    o.self = 1
)


; Change from R3-Alpha, FUNC and FUNCTION do not by default participate in
; "derived binding" but keep their bindings as-is.  The ACTION! must have a
; binding set up with BIND to get the derived behavior, which is done
; "magically" by METHOD.
(
    o1: make object! [a: 10 b: func [] [f: func [] [a] f]]
    o2: make o1 [a: 20]

    o2.b = 10
)(
    o1: make object! [a: 10 b: meth [] [f: func [] [a] f]]
    o2: make o1 [a: 20]

    o2.b = 20
)

(
    o-big: make object! collect [
        count-up n 256 [
            ;
            ; var-1: 1
            ; var-2: 2
            ; ...
            ; var-256: 256
            ;
            keep compose [
                (as word! unspaced ["var-" n]): (n)
            ]
        ]
        count-up n 256 [
            ;
            ; fun-1: meth [] [var-1]
            ; fun-2: meth [] [var-1 + var-2]
            ; ...
            ; fun-256: meth [] [var-1 + var-2 ... + var-256]
            ;
            keep compose [
                (as word! unspaced ["meth-" n]): meth [] (collect [
                    count-up i n [
                        keep compose [
                            (as word! unspaced ["var-" i]) ((if i <> n '[+]))
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
    did count-up i 2048 [
        derived: make o-big [var-1: 100000 + i]
        if 132639 + i <> derived.meth-255 [
            break
        ]
        true
    ]
)

; object cloning
[#2050 (
    o: make object! [n: 'o b: reduce [func [] [n]]]
    p: make o [n: 'p]
    (o.b).1 = 'o
)]


[
    https://github.com/metaeducation/ren-c/issues/907

    (
        o: make object! []
        true
    )

    (did trap [o.i: 1])
    (did trap [set? 'o.i])
    (did trap [unset? 'o.i])
    (null = in o 'i)
]
