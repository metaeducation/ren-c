;
; %match.test.reb
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

(10 = match :even? 10)
(null = match :even? 3)


('~blank~ = ^ match blank! _)
(null = match blank! 10)
(null = match blank! false)


; Falsey things are turned to BAD-WORD! in order to avoid cases like:
;
;     if match logic! flag [...]
;
; But can still be tested for then? since they are BAD-WORD!, and can be used
; with THEN and ELSE.
[
    ('~null~ = ^ match null null)
    ('~blank~ = ^ match blank! blank)
    (true = match logic! true)
    ('~false~ = ^ match logic! false)
]

[
    (10 = match integer! 10)
    (null = match integer! <tag>)

    ('a/b: = match any-path! 'a/b:)
    ('a/b: = match any-sequence! 'a/b:)
    (null = match any-array! 'a/b:)
]

; ENSURE is a version of MATCH that fails vs. returning NULL on no match
[
    (error? trap [ensure action! 10])
    (10 = ensure integer! 10)
]

; NON is an inverted form of ENSURE, that FAILs when the argument *matches*
[
    (error? trap [non action! :append])
    (10 = non action! 10)

    (error? trap [non integer! 10])
    (:append = non integer! :append)

    (10 = non null 10)

    (error? trap [non null null])
    (error? trap [non logic! false])
]


; MUST is an optimized form of NON NULL
[
    ("bc" = must find "abc" "b")
    (error? trap [must find "abc" "q"])
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
    (match+: reframer func [f [frame!] <local> p] [
        p: f.(first parameters of action of f)  ; get the first parameter
        if do f [p]  ; evaluate to parameter if operation succeeds
    ]
    true)

    ("aaa" = match+ match-parse "aaa" [some "a"])
    (null = match+ parse "aaa" [some "b"])
]
