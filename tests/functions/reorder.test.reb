; %reorder.test.reb
;
; REORDER leverages the generalized "two-pass" mechanic that lets refinements
; work to create lightweight interfaces that put parameters to an action in
; a different order.

; Ordinary arguments can be named in refinement paths, which pushes them to
; the end of the fulfillment list.  This is the same mechanism that REORDER
; exploits in its implemenation.
;
([a b c <item>] = append/series <item> [a b c])

; Baseline operation of REORDER.  Refinements are still available.
(
    itemfirst: reorder :append [value series]
    did all [
        [a b c <item>] = itemfirst <item> [a b c]
        [value series /part /only /dup /line] = parameters of :itemfirst
    ]
)

; Asking for the original order gives back an equivalent function.
(
    seriesfirst: reorder :append [series value]
    did all [
        [a b c <item>] = seriesfirst [a b c] <item>
        (parameters of :seriesfirst) = (parameters of :append)
    ]
)

; All required arguments must be mentioned in the ordering.
(
    e: trap [reorder :append [series]]
    did all [
        e.id = 'no-arg
        e/arg1 = 'append
        e/arg2 = 'value
    ]
)

; Optional arguments may be incorporated as well
(
    val-dup-ser: reorder :append [value dup series]
    [a b c <item> <item> <item>] = val-dup-ser <item> 3 [a b c]
)

; Naming a refinement more than once is an error
(
    e: trap [reorder :append [series value series]]
    did all [
        e.id = 'bad-parameter
        e/arg1 = 'series
    ]
)

; Unrecognized parameters cause errors
(
    e: trap [reorder :append [series value fhqwhgads]]
    did all [
        e.id = 'bad-parameter
        e/arg1 = 'fhqwhgads
    ]
)

; Functions modified with ADAPT, SPECIALIZE, etc. can be reordered.
(
    aplus: adapt :append [value: value + 1000]
    newaplus: reorder :aplus [value series]
    [a b c 1020] = newaplus 20 [a b c]
)

; Reordered functions also preserve their reordering across compositions.
(
    newa: reorder :append [value series]
    newaplus: adapt :newa [value: value + 1000]
    [a b c 1020] = newaplus 20 [a b c]
)

; Very experimental idea for making REORDER a bit more cooperative with
; existing parameter list formats, to leave in refinements but ignore them.
(
    ar: reorder :append [value /dup series]
    [a b c 10] = ar 10 [a b c]
)

; As with other function derivations, REORDER* is the form that does not
; inherit a copy of the HELP meta-information, while REORDER wraps on top of
; that with the usermode code that does the inherit.
(
    nohelp: reorder* :append [value series]  ; cheaper/faster to create
    did all [
        [a b c <item>] = nohelp <item> [a b c]  ; works the same
        null = meta-of :nohelp  ; ...but has no parameter information
    ]
)

; Weird demo taking advantage of the ignored parameters with a reversing
; macro, to implement something along the lines of Haskell FLIP.
[
    (flip: macro ['name [word!] <local> action] [
        action: ensure action! get name
        reduce [reorder :action (reverse parameters of :action)]
    ]
    true)

    (1000 = flip subtract 20 1020)
    ([a b c <item>] = flip append <item> [a b c])
]
