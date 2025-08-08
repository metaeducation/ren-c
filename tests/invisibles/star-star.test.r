; star-star.test.r
;
; The idea of using `**` as a variadic comment is redundant with the
; idea of using `|||` to do the same thing.  There may be a wiser
; purpose for one or both of these symbols.

(304 = (1000 + 20 (** foo <baz> (bar)) 300 + 4))

(304 = (1000 + 20 ** (
    foo <baz> (bar)
) 300 + 4))
