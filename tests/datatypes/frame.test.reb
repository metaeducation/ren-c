; %frame.test.reb
;
; The FRAME! type is foundational to the mechanics of Ren-C vs. historical
; Rebol.  While its underlying storage is similar to an OBJECT!, it has a
; more complex mechanic based on being able to be seen through the lens of
; multiple different "views" based on which phase of a function composition
; it has been captured for.


; Due to some fundamental changes to how parameter lists work, such that
; each action doesn't get its own copy, some tests of parameter hiding no
; longer work.  They were moved into this issue for consideration:
;
; https://github.com/metaeducation/ren-c/issues/393#issuecomment-745730620

; An "original" FRAME! that is created is considered to be phaseless.  When
; you execute that frame, the function will destructively use it as the
; memory backing the invocation.  This means it will treat the argument cells
; as if they were locals, manipulating them in such a way that the frame
; cannot meaningfully be used again.  The system flags such frames so that
; you don't accidentally try to reuse them or assume their arguments can
; act as caches of the input.
(
    f: make frame! unrun :append
    f.series: [a b c]
    f.value: <d>
    all [
        [a b c <d>] = eval copy f  ; making a copy works around the expiration
        f.series = [a b c <d>]
        f.value = <d>
        [a b c <d> <d>] = eval f
        'stale-frame = pick sys.util.rescue [eval f] 'id
        'bad-pick = pick sys.util.rescue [f.series] 'id
        'bad-pick = pick sys.util.rescue [f.value] 'id
    ]
)

[
    (
        foo: func [
            return: [frame!]
            public
            <local> private
        ][
            private: 304
            return binding of $public  ; frame as seen from interior
        ]

        f-outer: make frame! unrun :foo  ; frame as seen from exterior
        f-outer.public: 1020

        f-inner: eval copy f-outer
        true
    )

    (f-outer.public = 1020)  ; public values visible externally
    ~bad-pick~ !! (
        f-outer.private  ; private not visible in external view
    )

    (f-inner.public = 1020)  ; public values still visible internally
    (f-inner.private = 304)  ; returned internal view exposes private fields

    ; === ADAPT ===
    ;
    ; In the adaptation, we should not be able to see or manipulate any
    ; locals of the underlying function.
    ;
    ; !!! Taking advantage of the space available in locals could be
    ; interesting if possible to rename them, and then reset them to
    ; undefined while typechecking for lower level phases.  Think about it.
    (
        f-inner-prelude: '~junk~
        private: <not-in-prelude>
        adapted-foo: adapt :foo [
            f-inner-prelude: binding of $public
            assert [private = <not-in-prelude>]  ; should not be bound
        ]

        f-outer-adapt: make frame! unrun :adapted-foo
        f-outer-adapt.public: 1020

        f-inner-foo: eval copy f-outer-adapt

        true
    )

    (f-outer-adapt.public = 1020)
    ~bad-pick~ !! (
        f-outer-adapt.private
    )

    (f-inner-prelude.public = 1020)
    ~bad-pick~ !! (
        f-inner-prelude.private
    )

    (f-inner-foo.public = 1020)
    (f-inner.private = 304)

    ; === AUGMENT ===
    ;
    ; An augmentation makes a new parameter that is not visible to layers
    ; below it.  Notably, due to information-hiding, this parameter may have
    ; the same name as a hidden implementation detail of the inner portions
    ; of the composition.
    (
        f-inner-augment: ~
        private: <not-in-prelude>

        augmented-foo: adapt (augment :adapted-foo [
            additional [integer!]
            /private [tag!]  ; reusing name, for different variable!
        ]) [
            private: <reused>
            f-inner-augment: binding of $private
        ]

        assert [private = <not-in-prelude>]  ; should be untouched

        f-outer-augment: make frame! unrun :augmented-foo
        f-outer-augment.public: 1020
        f-outer-augment.additional: 1020304

        f-inner-foo: eval copy f-outer-augment

        true
    )

    (f-outer-augment.public = 1020)
    (f-outer-augment.additional = 1020304)
    (unspecialized? f-outer-augment.private)  ; we didn't assign it

    (f-inner-augment.public = 1020)
    (f-inner-augment.additional = 1020304)
    (f-inner-augment.private = <reused>)  ; not 304!

    (f-inner-foo.public = 1020)
    (f-inner-foo.private = 304)  ; not reused!
    ~bad-pick~ !! (
        f-inner-foo.additional
    )
]

[
    (
        foo: func [
            return: [frame!]
            public
            <local> private
        ][
            private: 304
            return binding of $public  ; return FRAME! with the internal view
        ]

        f-prelude: null

        bar: adapt augment :foo [/private [tag!]] [
            f-prelude: binding of $private
        ]

        f-outer: make frame! unrun :bar
        f-outer.public: 1020
        f-outer.private: <different!>

        f-inner: eval f-outer

        true
    )

    (f-prelude.public = 1020)
    (f-prelude.private = <different!>)

    (f-inner.public = 1020)
    (f-inner.private = 304)
]


; FRAME! TUNNELING - It is possible to pass a higher-level view of a frame to
; a lower-level implementation, as a trick to giving access to its variables.
; This is a speculative technique, but it is being tried.
[
    (
        f: lambda [x /augmented [frame!]] [
            reduce [x if augmented [augmented.y]]
        ]
        a: adapt augment :f [y] [augmented: binding of $y]
        all [
            [10] = f 10
            [10 20] = a 10 20
        ]
    )
]
