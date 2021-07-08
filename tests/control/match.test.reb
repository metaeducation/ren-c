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


('~falsey~ = ^ match blank! _)
(null = match blank! 10)
(null = match blank! false)


; !!! There was once special accounting for where the quoting level of the
; test would match the quoting level of the rule:
;
;    (the 'foo = match the 'word! the 'foo)
;    (null = match the 'word! the foo)
;
;    quoted-word!: quote word!
;    (''foo = match ['quoted-word!] the ''foo)
;    (null = match ['quoted-word!] the '''foo)
;    ('''foo = match the '['quoted-word!] the '''foo)
;
;    even-int: 'integer!/[:even?]
;    (the '304 = match the '[block!/3 even-int] the '304)
;
; This idea was killed off in steps; one step made it so that MATCH itself did
; not take its argument literally so it would not see quotes.  That made it
; less useful.  But then, also there were problems with quoteds not matching
; ANY-TYPE! because their quote levels were different than the quote level on
; the any type typeset.  It was a half-baked experiment that needs rethinking.


; PATH! is AND'ed together, while blocks are OR'd
;
; !!! REVIEW: this is likely not the best idea, should probably be TUPLE!
; with generalized tuple mechanics.  Otherwise it collides with the inline
; MATCH experiment, e.g. `match parse/case "AAA" [some "A"]`.  But tuples
; are not generalized yet.

(1020 = match [integer!/[:even?]] 1020)
(null = match [integer!/[:odd?]] 304)
([a b] = match [block!/2 integer!/[:even?]] [a b])
(null = match [block!/3 integer!/[:even?]] null)
(304 = match [block!/3 integer!/[:even?]] 304)
(null = match [block!/3 integer!/[:even?]] 303)


; Falsey things are turned to BAD-WORD! in order to avoid cases like:
;
;     if match logic! flag [...]
;
; But can still be tested for then? since they are BAD-WORD!, and can be used
; with THEN and ELSE.
[
    ('~falsey~ = ^ match null null)
    ('~falsey~ = ^ match blank! blank)
    (true = match logic! true)
    ('~falsey~ = ^ match logic! false)
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

; NON is an inverted form of MATCH, that fails when the argument *matches*
; It is a specialization of MATCH/NOT
[
    (null = non action! :append)
    (10 = non action! 10)

    (null = non integer! 10)
    (:append = non integer! :append)

    (10 = non null 10)

    ; !!! Review the special handling of this case...no good result can be
    ; returned to signal this failure.  Currently it just errors.
    ;
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
        p: f/(first parameters of action of f)  ; get the first parameter
        if do f [p]  ; evaluate to parameter if operation succeeds
    ]
    true)

    ("aaa" = match+ match-parse "aaa" [some "a"])
    (null = match+ parse "aaa" [some "b"])
]


; Before REFRAMER existed, there was the concept of MAKE FRAME! on VARARGS!
; This is still a potentially useful operation.
;
; Test MAKE FRAME! from a VARARGS! with a test userspace implementation of the
; MATCH operation...
[
    (userspace-match: function [
        {Check value using tests (match types, TRUE or FALSE, or filter)}

        return: "Input if it matched, otherwise null (void if falsey match)"
            [<opt> any-value!]
        'args [<opt> any-value! <variadic>]
        'args-normal [<opt> any-value! <variadic>]
        <local> first-arg
    ][
        test: first args
        switch type of :test [
            word! path! [
                if action? get test [
                    f: make frame! args
                    first-arg: get in f first parameters of action of f
                    return ((match true do f) then [:first-arg])
                ]
            ]
        ]

        match :(take args) (take args-normal)
    ]
    true)

    (userspace-match integer! 10 then [true])
    (userspace-match integer! <tag> else [true])
    (10 = userspace-match even? 10)
    (null = userspace-match even? 7)
]
