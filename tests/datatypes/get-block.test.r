; %get-block.test.r
;
; GET-BLOCK! really just calls REDUCE at the moment.  So instead of saying:
;
;     >> repend [a b c] spread [1 + 2 3 + 4]
;     == [a b c 3 7]
;
; You can write:
;
;     >> append [a b c] spread :[1 + 2 3 + 4]
;     == [a b c 3 7]
;

(get-block? first [:[a b c]])
(get-tuple? first [:[a b c].d])

(chain! = type of first [:[a b c]])
(chain! = type of first [:[a b c].d])

(
    a: 10 b: 20
    [10 20] = :[a b]
)

([a b c 3 7] = append [a b c] spread :[1 + 2 3 + 4])


; If used as a branch, the REDUCE only happens if the branch should run.
; !!! Temporarily (or maybe permanently) changed because ANY-BRANCH? typeset
; taking CHAIN! is not certain
[(
    x: ~
    y: <y>
    z: if ok [
        reduce [elide x: <x>, 1 + 2 3 + 4]
    ] else [
        reduce [elide y: ~, 3 + 4, 1 + 2]
    ]
    all [
        z = [3 7]
        x = <x>
        y = <y>
    ]
)]
