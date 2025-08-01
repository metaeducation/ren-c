; functions/context/resolve.r
;
; GET:STEPS has the job of creating a reproducible block of steps that can be
; used with SET and GET multiple times, when a TUPLE! or otherwise is used.

(
    obj: make object! [x: 1020]
    steps: ~
    all [
        @[obj x] = steps: get:steps $obj.(first [x y])
        1020 = get steps
    ]
)

; Usermode default is one of the classic use cases for GET:STEPS.
[
    (udefault: infix lambda [
        @target "Word or path which might be set appropriately (or not)"
            [set-group? set-word? set-tuple?]  ; to left of DEFAULT
        @(branch) "If target needs default, this is evaluated and stored there"
            [any-branch?]
        :predicate "Test for what's considered empty (default is null + void)"
            [<unrun> frame!]
        <local> steps
    ][
        unlift (
            (non:lift [vacant?] [steps {_}]: get:steps target) else [
                lift set steps eval branch
            ]
        )
    ],
    ok)

    (
        x: null
        all [
            304 = (x: udefault [300 + 4])
            x = 304
            304 = (x: udefault [1000 + 20])
            x = 304
        ]
    )

    (
        counter: 0
        obj: make object! [x: null]
        all [
            304 = (obj.(counter: me + 1, 'x): udefault [300 + 4])
            counter = 1  ; not 2, e.g. GET and SET phases shared resolved steps
            obj.x = 304
            304 = (obj.(counter: me + 1, 'x): udefault [1000 + 20])
            counter = 2
            obj.x = 304
        ]
    )
]
