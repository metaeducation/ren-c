; SET-GROUP! tests

(set-group! = type of first [(a b c):])
(set-path! = type of first [a/(b c d):])

(
    m: <before>
    word: 'm
    (word): 1020
    (word = 'm) and (m = 1020)
)

(
    o: make object! [f: <before>]
    path: 'o/f
    (path): 304
    (path = 'o/f) and (o/f = 304)
)

(
    m: <before>
    o: make object! [f: <before>]
    block: [m o/f]
    (block): [1020 304]
    (block = [m o/f]) and (m = 1020) and (o/f = 304)
)

; GET-GROUP! can run arity-1 functions.  Right hand side should be executed
; before left group gets evaluated.
(
    count: 0
    [1] = collect [
        (if count != 1 [fail] :keep/only): count: count + 1
    ]
)
