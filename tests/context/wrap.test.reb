; %wrap.test.reb
;
; Now that "strict mode" is the default, it's common to want to do an
; evaluation with top-level words gathered into a context.
;
; WRAP does this gathering, and it does it not just for SET-WORD but
; also for SET-BLOCK according to the rules of the dialect (e.g. the
; contents of GROUP!s aren't gathered, but words representing
; variables are).  This will compete with LET so they should not be
; used together

(30 = eval wrap [
    the [a [b (c)] ^d (e)]:  ; nested blocks will mean nested unpack
    set $a 10 set $b 20 set $d 30
])

~not-bound~ !! (
    eval wrap [
        the [a [b (c)] ^d (e)]:
        set $c 40  ; in group, not collected
    ]
)

~not-bound~ !! (
    eval wrap [
        the [a [b (c)] ^d (e)]:
        set $e 50  ; in group, not collected
    ]
)

~not-bound~ !! (30 = eval wrap [
    the (a [b (c)] ^d (e)):  ; SET-GROUPs do not count
    set $a 10
])

~not-bound~ !! (eval wrap [
    [a [b (c)] ^d (e)]  ; plain blocks do not count
    set $a 10
])

~not-bound~ !! (eval wrap [
    '[a [b (c)] ^d (e)]:  ; quoted set-blocks do not count
    set $a 10
])

;~not-bound~ !! (eval wrap [
;    $[a [b (c)] ^d (e)]:  ; binding set-blocks do not count
;    set $a 10
;])
;
; ~not-bound~ !! (eval wrap [
;     @[a [b (c)] ^d (e)]:  ; THE- set-blocks do not count
;    set $a 10
; ])
