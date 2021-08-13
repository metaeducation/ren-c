; %macro.test.reb
;
; MACRO is an exposure of an internal mechanism for splicing raw material into
; the stream of execution, used by predicates.
;
;     >> m: macro [x] [return [append x first]]
;
;     >> m [1 2 3] [d e f]
;     == [1 2 3 d]
;
; While expedient, this does have drawbacks.  For instance: while the
; function appears to take one parameter, in practice it will act as
; if it takes two.  It will not have the same composability with things
; like SPECIALIZE or ADAPT that a formally specified function would.

(
    m: macro [return: [block!] x] [return [append x ^ first]]
    [1 2 3 d] = m [1 2 3] [d e f]
)(
    m: enfix macro [discard] [[+ 2]]  ; !!! discard must be present ATM
    1 m = 3
)
