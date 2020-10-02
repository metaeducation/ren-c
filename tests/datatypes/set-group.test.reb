; SET-GROUP! tests
;
; This concept is still in flux as to whether it should have meaning, and if
; so what that meaning should be.  :(foo) does not mean the same as `get foo`
; hence it's not clear that `(foo):` should mean the same thing as `set foo`.
;
; One idea is to make `(foo):` the notation for multiple returns so that
; SET-BLOCK! can be inert.  This would be harder mechanically for COMPOSE
; and may be less nice looking.  Review.

(set-group! = type of first [(a b c):])
(set-path! = type of first [a/(b c d):])

(
    m: <before>
    word: 'm
    (word): 1020
    (word = 'm) and [m = 1020]
)

(
    o: make object! [f: <before>]
    path: 'o/f
    (path): 304
    (path = 'o/f) and [o/f = 304]
)

; Retriggering multi-returns is questionable
(
    m: <before>
    o: make object! [f: <before>]
    block: [m o/f]
    error? trap [(block): [1020 304]]
)

; SET-GROUP! can run arity-1 functions.  Right hand side should be executed
; before left group gets evaluated.
(
    count: 0
    [1] = collect [
        (if count != 1 [fail] :keep): (count: count + 1)
    ]
)
