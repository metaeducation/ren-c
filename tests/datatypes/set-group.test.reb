; SET-GROUP! tests
;
; Hesitancy initially surrounded making `(xxx):` a synonym for `set xxx`.  But
; being able to put the concept of setting along with evaluation into a
; single token has powerful uses...first seen with DEFAULT, e.g.
;
;     (...): default [...]
;
; And later shown to great effect with EMIT in uparse:
;
;     uparse ... [gather [varname: across to ..., emit (varname): ...]]
;
; Some weirder ideas, like that SET-GROUP! of an ACTION! will call arity-1
; actions with the right hand side have been axed.


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
    path: 'o.f
    (path): 304
    (path = 'o.f) and (o.f = 304)
)

; Retriggering multi-returns is useful, but not yet implemented.
(
    m: <before>
    o: make object! [f: <before>]
    block: [m o.f]
    error? trap [(block): [1020 304]]
)

; Weird dropped idea: SET-GROUP! running arity-1 functions.  Right hand side
; should be executed before left group gets evaluated.
;
;    count: 0
;    [1] = collect [
;        (if count != 1 [fail] :keep): (count: count + 1)
;    ]
;
