;
; %match.test.r
;
; MATCH started out as a userspace function, but gained frequent enough use
; to become a native.  It is theorized that MATCH will evolve into the
; tool for checking arguments in function specs.
;

(10 = match integer! 10)
(null = match integer! "ten")

("ten" = match [integer! text!] "ten")
(20 = match [integer! text!] 20)
(null = match [integer! text!] <tag>)

(10 = match [even?] 10)
(null = match [even?] 3)


(_ = match [space?] _)
(_ = match [_] _)
(null = match [_] 10)
(null = match [_] 'false)


[
    ~bad-antiform~ !! (match [null?] null)

    (null = match [integer!] ^void)
    ~???~ !! (match [void?] ^void)

    ('true = match [boolean?] 'true)
    ('false = match [boolean?] 'false)

    (''~preserved~ = lift match quasi-word?/ '~preserved~)
]

[
    (10 = match integer! 10)
    (null = match integer! <tag>)

    (null = match tuple! 'a.b:)
    ('a.b: = match chain! 'a.b:)
    ('a.b: = match any-sequence?/ 'a.b:)
    (null = match any-list?/ 'a.b:)
]

; ENSURE is a version of MATCH that panics vs. returning NULL on no match
[
    ~???~ !! (ensure frame! 10)
    (10 = ensure integer! 10)
]

; NON is an inverted form of ENSURE, that FAILs when the argument *matches*
[
    (null = non action! append/)
    (10 = non frame! 10)

    (null = non integer! 10)
    (append/ = non integer! append/)

    ~bad-antiform~ !! (non [null?] null)
    ~???~ !! (non null?/ 10)
    (null = non:lift null null)
    ((the '10) = non:lift null 10)

    (null = non [logic?] okay)
]

; PROHIBIT is an inverted version of ENSURE, where it must not match
; probably needs a better name, even ENSURE-NOT is likely clearer
[
    ~???~ !! (prohibit action! append/)
    (10 = prohibit frame! 10)

    ~???~ !! (prohibit integer! 10)
    (append/ = prohibit integer! append/)

    (10 = prohibit null 10)

    ~???~ !! (prohibit null null)
    ~???~ !! (prohibit [logic?] okay)
]


; MATCH was an early function for trying a REFRAMER-like capacity for
; building a frame of an invocation, stealing its first argument, and then
; returning that in the case of a match.  But now that REFRAMER exists,
; the idea of having that feature implemented in core functions has fallen
; from favor.
;
; Here we see the demo done with a reframer to make MATCH+ as a proof of
; concept of how it would be done if you wanted it.
[
    (match+: reframer func [f [frame!] {p}] [
        p: f.(first words of f)  ; get the first parameter
        eval f except [return null]
        return p  ; evaluate to parameter if operation succeeds
    ]
    ok)

    (null = match+ parse3 "aaa" [some "b"])
    ("aaa" = match+ parse3 "aaa" [some "a"])
]
