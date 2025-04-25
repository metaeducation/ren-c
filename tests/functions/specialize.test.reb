; specialize.test.reb
;
; Note: GET-PATH! for partial specialization uses basically the same code
; path as SPECIALIZE does, e.g. these run the same code:
;
;     specialize 'append/dup/part []
;     :append/dup/part

(
    append-123: specialize :append [value: [1 2 3] only: okay]
    [a b c [1 2 3] [1 2 3]] = append-123/dup copy [a b c] 2
)
(
    append-123: specialize :append [value: [1 2 3] only: okay]
    append-123-twice: specialize :append-123 [dup: okay count: 2]
    [a b c [1 2 3] [1 2 3]] = append-123-twice copy [a b c]
)
(
    append-10: specialize 'append [value: 10]
    f: make frame! :append-10
    f/series: copy [a b c]
    eval copy f ;-- COPY before EVAL preserves F, only the copy is "stolen"
    [a b c 10 10] = eval f
)
(
    f: make frame! 'append/only
    f/series: copy [a b c]
    f/value: [d e f]
    [a b c [d e f]] = eval f
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
    aopd3: specialize the (specialize 'append/only [])/part [
        count: 3
        limit: 1
    ]

    r: [a b c [d e] [d e] [d e]]

    all [
        r = aopd3 copy [a b c] [d e]
        r = applique 'aopd3 [series: copy [a b c] value: [d e]]
    ]
)

(
    is-bad: okay

    for-each code [
        [specialize 'append/only/only []]
        [specialize 'append/asdf []]
        [specialize the (specialize 'append/only [])/only []]
    ][
        is-bad: me and ['bad-refine = (sys/util/rescue [eval code])/id]
    ]

    is-bad
)
