; specialize.test.reb
;
; Note: GET-PATH! for partial specialization uses basically the same code
; path as SPECIALIZE does, e.g. these run the same code:
;
;     specialize 'append/dup/part []
;     :append/dup/part

(
    foo: func [/A aa /B bb /C cc] [  ; would be nice if refinements were null
        return compose [
            (maybe any [A]) (maybe aa)  ; ANY makes blanks into nulls
            (maybe any [B]) (maybe bb)
            (maybe any [C]) (maybe cc)
        ]
    ]

    fooBC: :foo/B/C
    fooCB: :foo/C/B

    did all [
        [/B 10 /C 20] = fooBC 10 20
        [/A 30 /B 10 /C 20] = fooBC/A 10 20 30

        [/B 20 /C 10] = fooCB 10 20
        [/A 30 /B 20 /C 10] = fooCB/A 10 20 30

        error? trap [fooBC/B 1 2 3 4 5 6]
        error? trap [fooBC/C 1 2 3 4 5 6]
        error? trap [fooCB/B 1 2 3 4 5 6]
        error? trap [fooCB/C 1 2 3 4 5 6]
    ]
)

(
    append-123: specialize :append [value: [1 2 3] only: true]
    [a b c [1 2 3] [1 2 3]] = append-123/dup copy [a b c] 2
)
(
    append-123: specialize :append [value: [1 2 3] only: true]
    append-123-twice: specialize :append-123 [dup: true count: 2]
    [a b c [1 2 3] [1 2 3]] = append-123-twice copy [a b c]
)
(
    append-10: specialize 'append [value: 10]
    f: make frame! :append-10
    f/series: copy [a b c]
    do copy f ;-- COPY before DO allows reuse of F, only the copy is "stolen"
    [a b c 10 10] = do f
)
(
    f: make frame! 'append/only
    f/series: copy [a b c]
    f/value: [d e f]
    [a b c [d e f]] = do f
)
(
    foo: func [] [
        return-5: specialize 'return [value: 5]
        return-5
        "this shouldn't be returned"
    ]
    foo = 5
)

(
    apd: specialize 'append/part [dup: true]
    apd3: specialize 'apd [count: 3]
    ap2d: specialize 'apd [limit: 2]

    xy: [<X> #Y]
    abc: [A B C]
    r: [<X> #Y A B A B A B]

    did all [
        | r = apd copy xy abc 2 3
        | r = applique 'apd [series: copy xy | value: abc | limit: 2 | count: 3]
        |
        | r = apd3 copy xy abc 2
        | r = applique 'apd3 [series: copy xy | value: abc | limit: 2]
        |
        | r = ap2d copy xy abc 3
        | r = applique 'ap2d [series: copy xy | value: abc | count: 3]
    ]
)(
    adp: specialize 'append/dup [part: true]
    adp2: specialize 'adp [limit: 2]
    ad3p: specialize 'adp [count: 3]

    xy: [<X> #Y]
    abc: [A B C]
    r: [<X> #Y A B A B A B]

    did all [
        | r = adp copy xy abc 3 2
        | r = applique 'adp [series: copy xy | value: abc | count: 3 | limit: 2]
        |
        | r = adp2 copy xy abc 3
        | r = applique 'adp2 [series: copy xy | value: abc | count: 3]
        |
        | r = ad3p copy xy abc 2
        | r = applique 'ad3p [series: copy xy | value: abc | limit: 2]
    ]
)

(
    aopd3: specialize the (specialize 'append/only [])/part [
        count: 3
        limit: 1
    ]

    r: [a b c [d e] [d e] [d e]]

    did all [
        | r = aopd3 copy [a b c] [d e]
        | r = applique 'aopd3 [series: copy [a b c] value: [d e]]
    ]
)

(
    is-bad: true

    for-each code [
        [specialize 'append/only/only []]
        [specialize 'append/asdf []]
        [specialize the (specialize 'append/only [])/only []]
    ][
        is-bad: me and ['bad-refine = (trap [do code])/id]
    ]

    is-bad
)


(
    ap10d: specialize 'append/dup [value: 10]
    f: make frame! :ap10d
    f/series: copy [a b c]
    did all [
        [a b c 10] = do copy f
        f/count: 2
        [a b c 10 10 10] = do f
    ]
)
