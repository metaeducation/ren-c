; %typechecker.test.r
;
; The typechecker and matcher are functions that need to have their
; implementations merged and streamlined.
;
; It's not entirely clear what circumstances will require a pure
; LOGIC! result like what the typechecker returns (e.g. would FIND
; allow you to use a MATCHER instead of a TYPECHECKER, and consider
; any non-null (and non-void) output to be a "branch trigger"?
;
; Similar questions exist for type constraint functions used in
; type specs.

(all [
    let t: typechecker word!
    okay? t 'abc
    null? t <abc>
    null? t null
    null? t void
])

(all [
    let m: matcher word!
    'abc = m 'abc
    null = m <abc>
    let e: sys.util/recover [m null]
    e.id = 'bad-antiform
])
