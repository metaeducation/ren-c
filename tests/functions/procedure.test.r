; %procedure.test.r
;
; PROCEDURE is like LAMBDA except it forces the result to trash.  You can't put
; return type specifications in its spec, and it has no RETURN.

(
    y: ~
    p: procedure [x] [y: x + 20]
    all [
        trash? p 1000
        y = 1020
    ]
)

(
    f: func [x] [
        p: proc [] [return x]  ; predefined shorthand
        p
        panic "unreachable"
    ]
    304 = f 304
)

((return of p/).spec = [])  ; "returns nothing" spec, e.g. TRASH!

~???~ !! (procedure [return: [integer!]] [10])
~???~ !! (procedure [yield: [integer!]] [10])
~???~ !! (procedure [[]: [integer!]] [10])
